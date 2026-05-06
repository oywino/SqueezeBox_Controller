#ifndef HA_REMOTE_UI_H
#define HA_REMOTE_UI_H

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE 1
#endif

#include "lvgl/lvgl.h"

/* Build LVGL screen: buttons, focus border, AC/BAT indicator. */
void ui_init(lv_group_t *grp);

/* UI->main signal: exit pressed. */
int ui_should_exit(void);

/* Home key actions. */
void ui_toggle_menu(void);
void ui_emergency_exit(void);
int ui_menu_wheel(int diff);
int ui_menu_is_visible(void);
void ui_refresh_cards(void);
void ui_cover_note_command(const char *entity_id, const char *command);
void ui_set_cover_refresh_callback(void (*refresh)(void));
const char *ui_focused_card_entity_id(void);

/* Backend -> UI bridge for HA status updates. */
void ui_status_set(const char *s, int connected, int have_states);

/* Show exit screen. */
void ui_show_exit_screen(void);

#endif /* HA_REMOTE_UI_H */
