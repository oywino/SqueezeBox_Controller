#ifndef HA_REMOTE_HA_REST_H
#define HA_REMOTE_HA_REST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HA_REST_MAX_STATE 64

int ha_rest_fetch_configured_states(const char *base_url, const char *token);
int ha_rest_fetch_state(const char *base_url, const char *token, const char *entity_id);
int ha_rest_call_service(const char *base_url,
                         const char *token,
                         const char *service,
                         const char *entity_id);
const char *ha_rest_get_cached_state(const char *entity_id);
const char *ha_rest_get_cached_media_title(const char *entity_id);
const char *ha_rest_get_cached_media_artist(const char *entity_id);
const char *ha_rest_get_cached_media_album(const char *entity_id);
const char *ha_rest_get_cached_media_channel(const char *entity_id);
const char *ha_rest_get_cached_media_picture(const char *entity_id);
int ha_rest_get_cached_media_position(const char *entity_id, int *position);
int ha_rest_get_cached_media_duration(const char *entity_id, int *duration);
unsigned long ha_rest_get_cached_version(const char *entity_id);
int ha_rest_get_cached_position(const char *entity_id, int *position);
void ha_rest_set_cached_state(const char *entity_id, const char *state);
void ha_rest_set_cached_media_title(const char *entity_id, const char *title);
void ha_rest_set_cached_media_artist(const char *entity_id, const char *artist);
void ha_rest_set_cached_media_album(const char *entity_id, const char *album);
void ha_rest_set_cached_media_channel(const char *entity_id, const char *channel);
void ha_rest_set_cached_media_picture(const char *entity_id, const char *picture);
void ha_rest_set_cached_media_position(const char *entity_id, int position);
void ha_rest_set_cached_media_duration(const char *entity_id, int duration);
void ha_rest_set_cached_position(const char *entity_id, int position);
void ha_rest_clear_cached_position(const char *entity_id);
size_t ha_rest_get_cached_count(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_REMOTE_HA_REST_H */
