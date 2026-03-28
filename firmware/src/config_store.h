#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "../config/fit7_params.h"

/*
 * Configuration Store
 *
 * On Pico: last 4KB flash sector with CRC32 validation
 * On simulator: file-backed JSON
 */

#define CONFIG_MAGIC    0xCF16CF16
#define CONFIG_VERSION  1

typedef struct {
    uint32_t    magic;
    uint16_t    version;
    uint16_t    param_count;
    param_def_t params[PARAM_COUNT];
    uint32_t    crc32;
} config_block_t;

/* Initialize config store — loads from storage or sets defaults */
void    config_init(void);

/* Get parameter value by ID */
int32_t config_get(param_id_t id);

/* Set parameter value by ID. Returns true if value was valid and set. */
bool    config_set(param_id_t id, int32_t value);

/* Get parameter definition (for name, limits, etc.) */
const param_def_t *config_get_def(param_id_t id);

/* Find parameter ID by command name. Returns -1 if not found. */
int     config_find_by_name(const char *name);

/* Save all parameters to persistent storage (TDD1 command) */
bool    config_save(void);

/* Reset all parameters to factory defaults */
void    config_reset_defaults(void);

/* Get pointer to the full config block (for direct access) */
config_block_t *config_get_block(void);

#endif /* CONFIG_STORE_H */
