#include "config_store.h"
#include "hal.h"
#include <string.h>
#include <stdio.h>

static config_block_t config;

/* Simple CRC32 (standard polynomial) */
static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static void set_defaults(void)
{
    static const param_def_t defaults[PARAM_COUNT] = DEFAULT_PARAMS;

    config.magic = CONFIG_MAGIC;
    config.version = CONFIG_VERSION;
    config.param_count = PARAM_COUNT;
    memcpy(config.params, defaults, sizeof(defaults));
}

void config_init(void)
{
    /* Try to load from persistent storage */
    if (hal_config_read((uint8_t *)&config, sizeof(config)) == 0) {
        /* Validate */
        uint32_t stored_crc = config.crc32;
        config.crc32 = 0;
        uint32_t calc_crc = crc32((const uint8_t *)&config,
                                   sizeof(config) - sizeof(uint32_t));

        if (config.magic == CONFIG_MAGIC &&
            config.version == CONFIG_VERSION &&
            config.param_count == PARAM_COUNT &&
            stored_crc == calc_crc) {
            config.crc32 = stored_crc;
            printf("[config] Loaded %d parameters from storage\n", PARAM_COUNT);
            return;
        }
    }

    /* Invalid or missing — load defaults */
    printf("[config] Loading factory defaults\n");
    set_defaults();
}

int32_t config_get(param_id_t id)
{
    if (id < 0 || id >= PARAM_COUNT) return 0;
    return config.params[id].value;
}

bool config_set(param_id_t id, int32_t value)
{
    if (id < 0 || id >= PARAM_COUNT) return false;

    param_def_t *p = &config.params[id];
    if (value < p->min_value || value > p->max_value) return false;

    p->value = value;
    return true;
}

const param_def_t *config_get_def(param_id_t id)
{
    if (id < 0 || id >= PARAM_COUNT) return NULL;
    return &config.params[id];
}

int config_find_by_name(const char *name)
{
    for (int i = 0; i < PARAM_COUNT; i++) {
        if (strcmp(config.params[i].name, name) == 0)
            return i;
    }
    return -1;
}

bool config_save(void)
{
    config.crc32 = 0;
    config.crc32 = crc32((const uint8_t *)&config,
                          sizeof(config) - sizeof(uint32_t));

    if (hal_config_write((const uint8_t *)&config, sizeof(config)) == 0) {
        printf("[config] Saved to storage\n");
        return true;
    }
    printf("[config] Save failed!\n");
    return false;
}

void config_reset_defaults(void)
{
    set_defaults();
}

config_block_t *config_get_block(void)
{
    return &config;
}
