// List of region IDs.

#pragma once

#define REGION_JAP 0
#define REGION_USA 1
#define REGION_EUR 2
#define REGION_AUS 3
#define REGION_CHN 4
#define REGION_KOR 5
#define REGION_TWN 6

#define REGION_MASK_JAP (1u << REGION_JAP)
#define REGION_MASK_USA (1u << REGION_USA)
#define REGION_MASK_EUR (1u << REGION_EUR)
#define REGION_MASK_AUS (1u << REGION_AUS)
#define REGION_MASK_CHN (1u << REGION_CHN)
#define REGION_MASK_KOR (1u << REGION_KOR)
#define REGION_MASK_TWN (1u << REGION_TWN)

#define TWL_REGION_FREE     0xFFFFFFFF

#define SMDH_REGION_FREE    0x7FFFFFFF
