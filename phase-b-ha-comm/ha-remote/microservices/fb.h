#ifndef HA_REMOTE_FB_H
#define HA_REMOTE_FB_H

#include <stddef.h>
#include "lvgl/lvgl.h"

int  fb_init(int *out_w, int *out_h);
void fb_deinit(void);
void fb_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
void lcd_wake(void);

#endif /* HA_REMOTE_FB_H */
