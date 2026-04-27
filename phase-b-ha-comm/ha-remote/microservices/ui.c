#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "ui.h"
#include "hal.h"   /* For HAL power telemetry */
#include "ha_ws.h" /* For ha_poll_timer() */

/* UI state (encapsulated here) */
static int g_should_exit = 0;
static lv_obj_t *g_power_label = NULL;
static lv_obj_t *g_menu_panel = NULL;
static lv_obj_t *g_menu_scrim = NULL;
static int g_menu_visible = 0;

/* --- Internal helpers (all static) --- */

/* Copied directly from monolithic main.c */
static uint64_t ms_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void pwr_indicator_update(void)
{
  if(!g_power_label) return;

  struct hal_power_state st;
  if (hal_get_power(&st) != 0) {
    lv_label_set_text(g_power_label, "PWR?");
    return;
  }

  const char *src  = (st.on_ac == 1) ? "AC" :
                     (st.on_ac == 0) ? "BAT" : "?";
  const char *plus = (st.charging == 1) ? "+" :
                     (st.charging == 0) ? "" : "?";

  char s[8];
  snprintf(s, sizeof(s), "%s%s", src, plus);
  lv_label_set_text(g_power_label, s);
}

static void pwr_indicator_timer_cb(lv_timer_t *t)
{
  (void)t;
  pwr_indicator_update();
}

static void keep_visible(lv_timer_t *t)
{
  (void)t;
  lv_obj_invalidate(lv_scr_act());
}

static lv_obj_t *make_section(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h)
{
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x404040), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

static void build_menu(lv_obj_t *scr)
{
  g_menu_scrim = lv_obj_create(scr);
  lv_obj_set_size(g_menu_scrim, 240, 320);
  lv_obj_set_pos(g_menu_scrim, 0, 0);
  lv_obj_set_style_bg_color(g_menu_scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_menu_scrim, LV_OPA_50, 0);
  lv_obj_set_style_border_width(g_menu_scrim, 0, 0);
  lv_obj_add_flag(g_menu_scrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_menu_scrim, LV_OBJ_FLAG_SCROLLABLE);

  g_menu_panel = lv_obj_create(scr);
  lv_obj_set_size(g_menu_panel, 132, 320);
  lv_obj_set_pos(g_menu_panel, 0, 0);
  lv_obj_set_style_bg_color(g_menu_panel, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(g_menu_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_menu_panel, lv_color_hex(0xFFFF00), 0);
  lv_obj_set_style_border_width(g_menu_panel, 1, 0);
  lv_obj_set_style_radius(g_menu_panel, 0, 0);
  lv_obj_add_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_menu_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(g_menu_panel);
  lv_label_set_text(title, "Menu");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFF00), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

  static const char *items[] = { "Cards", "Refresh", "Status" };
  for (int i = 0; i < 3; i++) {
    lv_obj_t *item = lv_label_create(g_menu_panel);
    lv_label_set_text(item, items[i]);
    lv_obj_set_style_text_color(item, lv_color_white(), 0);
    lv_obj_align(item, LV_ALIGN_TOP_LEFT, 12, 42 + (i * 30));
  }
}

static void set_menu_visible(int visible)
{
  g_menu_visible = visible ? 1 : 0;
  if (!g_menu_panel || !g_menu_scrim) return;

  if (g_menu_visible) {
    lv_obj_clear_flag(g_menu_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_menu_scrim, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_invalidate(lv_scr_act());
}

/* Suppressed: no LCD output for HA status */
static void ui_update_ha_status(const char *status, int connected, int have_states)
{
  (void)status;
  (void)connected;
  (void)have_states;
  /* Intentionally no LCD output */
}

/* --- Public API --- */

void ui_init(lv_group_t *grp)
{
  g_should_exit = 0;
  g_menu_visible = 0;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *header = make_section(scr, 0, 0, 240, 34);
  lv_obj_t *main_area = make_section(scr, 0, 34, 240, 232);
  lv_obj_t *footer = make_section(scr, 0, 266, 240, 54);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "HA Remote");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t *state = lv_label_create(main_area);
  lv_label_set_text(state, "Card area");
  lv_obj_set_style_text_color(state, lv_color_hex(0xB0B0B0), 0);
  lv_obj_center(state);

  lv_obj_t *hint = lv_label_create(footer);
  lv_label_set_text(hint, "Home: Menu");
  lv_obj_set_style_text_color(hint, lv_color_hex(0xB0B0B0), 0);
  lv_obj_align(hint, LV_ALIGN_LEFT_MID, 8, 0);

  /* Bottom-right power indicator */
  g_power_label = lv_label_create(scr);
  lv_label_set_text(g_power_label, "");
  lv_obj_set_style_text_color(g_power_label, lv_color_white(), 0);
  lv_obj_align(g_power_label, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
  pwr_indicator_update();
  lv_timer_create(pwr_indicator_timer_cb, 1000, NULL);
  build_menu(scr);
  (void)grp;

  /* Keep LVGL refreshing */
  lv_timer_create(keep_visible, 30, NULL);

  /* Poll timer wrapper */
  lv_timer_create(ha_poll_timer_cb, 100, NULL);
}

int ui_should_exit(void)
{
  return g_should_exit ? 1 : 0;
}

void ui_toggle_menu(void)
{
  set_menu_visible(!g_menu_visible);
}

void ui_emergency_exit(void)
{
  g_should_exit = 1;
}

void ui_show_exit_screen(void)
{
  g_power_label = NULL;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl,
                    "Terminating HA-Remote\n"
                    "Restarting Jive......... (please wait)");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl);

  uint64_t start = ms_now();
  uint64_t last = start;
  while(ms_now() - start < 400) {
    uint64_t now = ms_now();
    lv_tick_inc((uint32_t)(now - last));
    last = now;
    lv_timer_handler();
    usleep(5000);
  }
}

/* LVGL timer wrapper for backend poll */
void ha_poll_timer_cb(lv_timer_t *t)
{
  (void)t;
  ha_poll_timer();
}

/* Bridge used by backend: delegates to UI formatter above */
void ui_status_set(const char *s, int connected, int have_states)
{
  ui_update_ha_status(s, connected, have_states);
}
