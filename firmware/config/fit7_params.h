#ifndef FIT7_PARAMS_H
#define FIT7_PARAMS_H

#include <stdint.h>

/*
 * FIT7 Parameter Table
 * Defines all parameters the device supports, matching the subset
 * used by the DCH overhead sizing system (from HBMLoadCell.cpp).
 */

/* Parameter IDs */
typedef enum {
    PARAM_ASF = 0,      /* Amplifier Signal Filter (lowpass) */
    PARAM_FMD,          /* Filter Mode */
    PARAM_ICR,          /* Internal Conversion Rate */
    PARAM_CWT,          /* Calibration Weight */
    PARAM_LDW,          /* Load Cell Dead Load Weight */
    PARAM_LWT,          /* Load Cell Live Weight */
    PARAM_NOV,          /* Nominal Value */
    PARAM_RSN,          /* Resolution */
    PARAM_MTD,          /* Motion Detection */
    PARAM_LIC0,         /* Linearization Coefficient 0 */
    PARAM_LIC1,         /* Linearization Coefficient 1 */
    PARAM_LIC2,         /* Linearization Coefficient 2 */
    PARAM_LIC3,         /* Linearization Coefficient 3 */
    PARAM_ZTR,          /* Zero Tracking */
    PARAM_ZSE,          /* Zero Setting */
    PARAM_TRC1,         /* Trigger Command param 1 */
    PARAM_TRC2,         /* Trigger Command param 2 */
    PARAM_TRC3,         /* Trigger Command param 3 */
    PARAM_TRC4,         /* Trigger Command param 4 */
    PARAM_TRC5,         /* Trigger Command param 5 */
    PARAM_COF,          /* Configure Output Format */
    PARAM_CSM,          /* Checksum enable */
    PARAM_STR,          /* Termination Resistor */
    PARAM_ADR,          /* Device Address */
    PARAM_COUNT
} param_id_t;

/* Parameter definition */
typedef struct {
    const char *name;       /* 3-letter command shortform */
    int32_t     value;      /* Current value */
    int32_t     def_value;  /* Factory default */
    int32_t     min_value;
    int32_t     max_value;
    uint8_t     password;   /* 1 = requires SPW before write */
    uint8_t     save_nvm;   /* 1 = should be saved with TDD1 */
} param_def_t;

/* Default parameter values matching typical FIT7 setup for DCH system */
#define DEFAULT_PARAMS { \
    /* name    value   default   min       max      pwd  nvm */ \
    { "ASF",       6,       6,    2,        10,      0,   1 }, \
    { "FMD",       1,       1,    0,         5,      0,   1 }, \
    { "ICR",       4,       4,    0,         7,      0,   1 }, \
    { "CWT",  1000000, 1000000,   0,   9999999,      1,   1 }, \
    { "LDW",       0,       0,   0,    9999999,      1,   1 }, \
    { "LWT",  1000000, 1000000,   0,   9999999,      1,   1 }, \
    { "NOV",       0,       0,   0,    9999999,      0,   1 }, \
    { "RSN",       1,       1,    0,      6000,      0,   1 }, \
    { "MTD",       0,       0,    0,        99,      0,   1 }, \
    { "LIC0",      0,       0,   -9999999, 9999999,  1,   1 }, \
    { "LIC1", 1000000, 1000000,  -9999999, 9999999,  1,   1 }, \
    { "LIC2",      0,       0,   -9999999, 9999999,  1,   1 }, \
    { "LIC3",      0,       0,   -9999999, 9999999,  1,   1 }, \
    { "ZTR",       0,       0,    0,         3,      0,   1 }, \
    { "ZSE",       0,       0,    0,        99,      0,   1 }, \
    { "TRC1",      0,       0,    0,         5,      0,   1 }, \
    { "TRC2",      0,       0,    0,         2,      0,   1 }, \
    { "TRC3",      0,       0,    0,   9999999,      0,   1 }, \
    { "TRC4",      0,       0,    0,        99,      0,   1 }, \
    { "TRC5",      0,       0,    0,        99,      0,   1 }, \
    { "COF",       8,       8,    0,       143,      0,   1 }, \
    { "CSM",       1,       1,    0,         1,      0,   1 }, \
    { "STR",       1,       1,    0,         1,      0,   1 }, \
    { "ADR",      31,      31,    0,       128,      0,   1 }, \
}

#endif /* FIT7_PARAMS_H */
