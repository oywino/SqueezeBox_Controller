#ifndef HA_REMOTE_MEDIA_ART_H
#define HA_REMOTE_MEDIA_ART_H

#include <stddef.h>
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

int media_art_start(const char *base_url, const char *token, const char *entity_id);
void media_art_stop(void);
unsigned long media_art_get_path(char *out, size_t out_size);
const lv_img_dsc_t *media_art_get_image(unsigned long *version);

#ifdef __cplusplus
}
#endif

#endif /* HA_REMOTE_MEDIA_ART_H */
