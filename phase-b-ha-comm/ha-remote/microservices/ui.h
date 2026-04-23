#ifndef HA_REMOTE_UI_H
#define HA_REMOTE_UI_H

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE 1
#endif

#include "lvgl/lvgl.h"

/* Optional callback set by main (e.g. start/restart HA session). */
void ui_set_on_start(void (*fn)(void));

/* Build LVGL screen: buttons, focus border, AC/BAT indicator. */
void ui_init(lv_group_t *grp);

/* UI->main signal: exit pressed. */
int ui_should_exit(void);

/* Backend → UI bridge for HA status updates. */
void ui_status_set(const char *s, int connected, int have_states);

/* Show exit screen. */
void ui_show_exit_screen(void);

/* LVGL timer wrapper for backend poll. */
void ha_poll_timer_cb(lv_timer_t *t);

#endif /* HA_REMOTE_UI_H */
