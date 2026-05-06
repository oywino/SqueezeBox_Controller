#ifndef HA_REMOTE_FB_H
#define HA_REMOTE_FB_H

#include <stddef.h>
#include "lvgl/lvgl.h"

int  fb_init(int *out_w, int *out_h);
void fb_deinit(void);
void fb_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
int  fb_text_strip_set(unsigned int slot,
                       int x,
                       int y,
                       int w,
                       int h,
                       const lv_font_t *font,
                       const char *text,
                       uint32_t fg_hex,
                       uint32_t bg_hex);
void fb_text_strip_disable(unsigned int slot);
void fb_text_strip_set_blocked(int blocked);
void fb_text_strip_set_occluder(int active, int x, int y, int w, int h);
void lcd_wake(void);
void lcd_sleep(void);

#endif /* HA_REMOTE_FB_H */
