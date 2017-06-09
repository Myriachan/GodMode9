#include "common.h"
#include "i2c.h"
#include "region.h"
#include "unittype.h"
#include "vff.h"
#include "nand/essentials.h" // For SecureInfo
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// Table of system models.
// https://www.3dbrew.org/wiki/Cfg:GetSystemModel#System_Model_Values
static const struct {
    char name[12];
    char product_code[4];
} s_modelNames[] = {
    { "Old 3DS",    "CTR" }, // 0
    { "Old 3DS XL", "SPR" }, // 1
    { "New 3DS",    "KTR" }, // 2
    { "Old 2DS",    "FTR" }, // 3
    { "New 3DS XL", "RED" }, // 4
    { "New 2DS XL", "JAN" }, // 5
};
STATIC_ASSERT(countof(s_modelNames) == NUM_MODELS);

// Table of sales regions.
static const struct {
    char serial_char;
    const char* name;
} s_salesRegions[] = {
    // Typical regions.
    { 'J', "Japan" },
    { 'W', "Americas" },    // "W" = worldwide?
    { 'E', "Europe" },
    { 'C', "China" },
    { 'K', "Korea" },
    { 'T', "Taiwan" },
    // Manufacturing regions that have another region's region lock.
    { 'S', "Middle East" }, // "S" = Saudi Arabia?  Singapore?  (Southeast Asia included.)
    { 'A', "Australia" },
};

// Structure of system information.
typedef struct _SysInfo {
    // Internal data to pass among these subroutines.
    uint8_t int_model;

    // From hardware information.
    char model[15 + 1];
    char product_code[3 + 1];
    // From SecureInfo_A/B
    char sub_model[15 + 1];
    char serial[15 + 1];
    char system_region[15 + 1];
    char sales_region[15 + 1];
} SysInfo;


// Read hardware information.
void GetSysInfo_Hardware(SysInfo* info, char nand_drive) {
    (void) nand_drive;

    info->int_model = 0xFF;
    strncpy(info->model, "<unknown>", countof(info->model));
    strncpy(info->product_code, "???", countof(info->product_code));

    // Get MCU system information.
    uint8_t mcu_sysinfo[0x13];
    if (i2cReadRegisterBuffer(I2C_DEV_MCU, 0x7F, mcu_sysinfo, sizeof(mcu_sysinfo))) {
        // System model.
        info->int_model = mcu_sysinfo[0x09];
        if (info->int_model < NUM_MODELS) {
            strncpy(info->model, s_modelNames[info->int_model].name, countof(info->model));
            strncpy(info->product_code, s_modelNames[info->int_model].product_code, countof(info->product_code));
        }
    }
}


// Read SecureInfo_A.
void GetSysInfo_SecureInfo(SysInfo* info, char nand_drive) {
    static char path[] = "_:/rw/sys/SecureInfo__";

    SecureInfo data;

    path[0] = nand_drive;

    strncpy(info->sub_model, "<unknown>", countof(info->sub_model));
    strncpy(info->serial, "<unknown>", countof(info->serial));
    strncpy(info->system_region, "<unknown>", countof(info->system_region));
    strncpy(info->sales_region, "<unknown>", countof(info->sales_region));

    // Try SecureInfo_A then SecureInfo_B.
    bool got_data = false;
    for (char which = 'A'; which <= 'B'; ++which) {
        path[countof(path) - 2] = which;

        UINT got_size;
        if (fvx_qread(path, &data, 0, sizeof(data), &got_size) == FR_OK) {
            if (got_size == sizeof(data)) {
                got_data = true;
                break;
            }
        }
    }

    if (!got_data) {
        return;
    }

    // Decode region.
    if (data.region < SMDH_NUM_REGIONS) {
        strncpy(info->system_region, g_regionNamesLong[data.region], countof(info->system_region));
    }

    // Retrieve serial number.  Set up calculation of check digit.
    STATIC_ASSERT(countof(info->serial) > countof(data.serial));

    bool got_serial = true;
    char second_letter = '\0';
    char first_digit = '\0';
    char second_digit = '\0';
    unsigned digits = 0;
    unsigned letters = 0;
    unsigned odds = 0;
    unsigned evens = 0;

    for (unsigned x = 0; x < 15; ++x) {
        char ch = data.serial[x];
        info->serial[x] = ch;

        if (ch == '\0') {
            break;
        } else if ((ch < ' ') || (ch > '~')) {
            got_serial = false;
            break;
        } else if ((ch >= '0') && (ch <= '9')) {
            // Track the sum of "odds" and "evens" based on their position.
            // The first digit is "odd".
            ++digits;
            if (digits % 2)
                odds += ch - '0';
            else
                evens += ch - '0';

            // Remember the first two digits for submodel check.
            if (digits == 1)
                first_digit = ch;
            else if (digits == 2)
                second_digit = ch;
        } else {
            // Remember the second letter, because that's the sales region.
            ++letters;
            if (letters == 2) {
                second_letter = ch;
            }
        }
    }

    if (!got_serial) {
        return;
    }

    // Copy the serial out.
    strncpy(info->serial, data.serial, countof(data.serial));
    info->serial[countof(data.serial)] = '\0';

    // Append the check digit if the format appears valid.
    size_t length = strlen(info->serial);
    if ((length < countof(info->serial) - 1) && (digits == 8)) {
        unsigned check_value = 10 - (((3 * evens) + odds) % 10);
        char check_digit = (check_value == 10) ? '0' : (char) (check_value + '0');

        info->serial[length] = check_digit;
        info->serial[length + 1] = '\0';
    }

    // Determine the sales region from the second letter of the prefix.
    if (second_letter != '\0') {
        for (unsigned x = 0; x < countof(s_salesRegions); ++x) {
            if (s_salesRegions[x].serial_char == second_letter) {
                strncpy(info->sales_region, s_salesRegions[x].name, countof(info->sales_region));
                break;
            }
        }
    }

    // Determine the sub-model from the first two digits of the digit part.
    if (first_digit && second_digit) {
        if (IS_DEVKIT) {
            if ((first_digit == '9') && (second_digit == '0') && (info->int_model == MODEL_OLD_3DS)) {
                strncpy(info->sub_model, "Partner-CTR", countof(info->sub_model));
            } else if ((first_digit == '9') && (second_digit == '1') && (info->int_model == MODEL_OLD_3DS)) {
                strncpy(info->sub_model, "IS-CTR-BOX", countof(info->sub_model));
            } else if ((first_digit == '9') && (second_digit == '1') && (info->int_model == MODEL_OLD_3DS_XL)) {
                strncpy(info->sub_model, "IS-SPR-BOX", countof(info->sub_model));
            } else if ((first_digit == '9') && (second_digit == '1') && (info->int_model == MODEL_NEW_3DS)) {
                strncpy(info->sub_model, "IS-SNAKE-BOX", countof(info->sub_model));
            } else {
                strncpy(info->sub_model, "panda", countof(info->sub_model));
            }
        } else {
            if ((first_digit == '0') && (second_digit == '1') && !IS_O3DS) {
                strncpy(info->sub_model, "press", countof(info->sub_model));
            } else {
                strncpy(info->sub_model, "retail", countof(info->sub_model));
            }
        }
    }
}


void MeowPrintf(FIL* file, const char* format, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, countof(buffer), format, args);
    va_end(args);

    UINT btw = (UINT) strlen(buffer);
    UINT dummy;
    fvx_write(file, buffer, btw, &dummy);
}


void MyriaSysinfo(void) {
(void) s_modelNames;
    SysInfo info;
    GetSysInfo_Hardware(&info, '1');
    GetSysInfo_SecureInfo(&info, '1');

    FIL meow;
    if (fvx_open(&meow, "0:/meow.txt", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        return;
    }

    MeowPrintf(&meow, "Model: %s (%s)\r\n", info.model, info.sub_model);
    MeowPrintf(&meow, "Serial: %s\r\n", info.serial);
    MeowPrintf(&meow, "Region (system): %s\r\n", info.system_region);
    MeowPrintf(&meow, "Region (sales): %s\r\n", info.sales_region);

    fvx_close(&meow);
}
