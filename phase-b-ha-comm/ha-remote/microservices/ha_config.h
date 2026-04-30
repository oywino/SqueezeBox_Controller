#ifndef HA_CONFIG_H
#define HA_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HA_CONFIG_MAX_ACTION_DATA 8
#define HA_CONFIG_MAX_CARDS 16
#define HA_CONFIG_MAX_TRACKED_ENTITIES 32
#define HA_CONFIG_MAX_BASE_URL 128
#define HA_CONFIG_MAX_ACCESS_TOKEN 256

typedef struct {
    double brightness_step;
    double color_temp_step;
    double temp_step;
    int has_brightness_step;
    int has_color_temp_step;
    int has_temp_step;
} ha_config_options_t;

typedef struct {
    char key[32];
    char value[64];
} ha_config_kv_t;

typedef struct {
    char service[64];
    ha_config_kv_t data[HA_CONFIG_MAX_ACTION_DATA];
    int data_count;
} ha_config_action_t;

typedef struct {
    char type[16];
    char entity_id[64];
    char title[32];
    ha_config_options_t options;
    ha_config_action_t primary;
    ha_config_action_t secondary;
} ha_config_card_t;

/*
 * Load configuration from JSON file.
 * If path is NULL, defaults to "config.example.json".
 * Returns 1 on success, 0 on failure (in which case
 * card and tracked-entity lists are cleared).
 */
int ha_config_load(const char *path);

/* Enumerate parsed cards (read-only). */
size_t ha_config_get_card_count(void);
const ha_config_card_t *ha_config_get_card(size_t index);

/* Enumerate tracked entities (read-only). */
size_t ha_config_get_tracked_entity_count(void);
const char *ha_config_get_tracked_entity(size_t index);

/* Home Assistant connection settings (read-only). */
const char *ha_config_get_ha_base_url(void);
const char *ha_config_get_ha_access_token(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_CONFIG_H */
