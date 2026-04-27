#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "ui.h"
#include "status_cache.h"
#include "ha_ws.h"
#include "assets/jive_assets.h"
#include "src/extra/libs/tiny_ttf/lv_tiny_ttf.h"

#define SCREEN_W 240
#define SCREEN_H 320
#define TOP_H 30
#define TOP_GAP 4
#define FOOTER_H 28
#define FOOTER_Y (SCREEN_H - FOOTER_H)
#define MAIN_X 4
#define MAIN_Y (TOP_H + TOP_GAP)
#define MAIN_W (SCREEN_W - 8)
#define MAIN_H (FOOTER_Y - MAIN_Y - 6)
#define MENU_W 144
#define MENU_X_SHOWN -8
#define MENU_X_HIDDEN -MENU_W
#define MENU_Y (MAIN_Y + 10)
#define MENU_H 188

static int g_should_exit = 0;
static int g_menu_visible = 0;
static lv_obj_t *g_wifi_img = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_power_img = NULL;
static lv_obj_t *g_menu_panel = NULL;
static lv_font_t *g_font_top = NULL;
static lv_font_t *g_font_menu = NULL;
static lv_font_t *g_font_menu_sel = NULL;
static lv_font_t *g_font_title = NULL;
static lv_font_t *g_font_state = NULL;
static lv_font_t *g_font_small = NULL;

static uint64_t ms_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void status_update(void)
{
  if(g_wifi_img) {
    struct hal_wifi_state st;
    if(status_cache_get_wifi(&st) == 0 && st.connected >= 0) {
      lv_img_set_src(g_wifi_img,
                     st.connected == 1 ? &jive_icon_wireless_4 : &jive_icon_wireless_cantconnect);
    }
  }

  if(g_time_label) {
    time_t now = time(NULL);
    struct tm tmv;
    char buf[24];
    localtime_r(&now, &tmv);
    strftime(buf, sizeof(buf), "%d.%m.%y - %H:%M", &tmv);
    lv_label_set_text(g_time_label, buf);
  }

  if(g_power_img) {
    struct hal_power_state st;
    if(status_cache_get_power(&st) != 0) {
      return;
    } else if(st.on_ac == 1) {
      lv_img_set_src(g_power_img, &jive_icon_battery_ac);
    } else if(st.on_ac == 0) {
      lv_img_set_src(g_power_img, &jive_icon_battery_3);
    } else {
      lv_img_set_src(g_power_img, &jive_icon_battery_none);
    }
  }
}

static void status_timer_cb(lv_timer_t *t)
{
  (void)t;
  status_update();
}

static void keep_visible(lv_timer_t *t)
{
  (void)t;
  lv_obj_invalidate(lv_scr_act());
}

static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h, uint32_t color, lv_coord_t radius)
{
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

static void set_label_font(lv_obj_t *label, const lv_font_t *font)
{
  if(font) lv_obj_set_style_text_font(label, font, 0);
}

static void init_fonts(void)
{
  if(g_font_top) return;

  g_font_top = lv_tiny_ttf_create_data(jive_freesans_bold_ttf, jive_freesans_bold_ttf_size, 12);
  g_font_menu = lv_tiny_ttf_create_data(jive_freesans_bold_ttf, jive_freesans_bold_ttf_size, 18);
  g_font_menu_sel = lv_tiny_ttf_create_data(jive_freesans_bold_ttf, jive_freesans_bold_ttf_size, 21);
  g_font_title = lv_tiny_ttf_create_data(jive_freesans_bold_ttf, jive_freesans_bold_ttf_size, 18);
  g_font_state = lv_tiny_ttf_create_data(jive_freesans_ttf, jive_freesans_ttf_size, 14);
  g_font_small = lv_tiny_ttf_create_data(jive_freesans_bold_ttf, jive_freesans_bold_ttf_size, 12);
}

static void build_card(lv_obj_t *main_area, lv_coord_t y, const char *title,
                       const char *state, int active)
{
  lv_obj_t *card = make_panel(main_area, 8, y, MAIN_W - 16, 70,
                              active ? 0xF4F4F4 : 0xD8D8D8, 8);

  lv_obj_t *icon = make_label(card, LV_SYMBOL_HOME, 0x101010);
  set_label_font(icon, g_font_title);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);

  lv_obj_t *name = make_label(card, title, 0x101010);
  set_label_font(name, g_font_title);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 46, 14);

  lv_obj_t *value = make_label(card, state, active ? 0x006DCC : 0x404040);
  set_label_font(value, g_font_state);
  lv_obj_align(value, LV_ALIGN_TOP_LEFT, 46, 38);
}

static lv_obj_t *build_menu_row(lv_obj_t *parent, lv_coord_t y, const char *text, int selected)
{
  lv_obj_t *row = make_panel(parent, 10, y, MENU_W - 24, 28,
                             selected ? 0x006DCC : 0xFFFFFF, 5);

  lv_obj_t *label = make_label(row, text, selected ? 0xFFFFFF : 0x000000);
  set_label_font(label, selected ? g_font_menu_sel : g_font_menu);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
  return row;
}

static void build_menu(lv_obj_t *scr)
{
  g_menu_panel = make_panel(scr, MENU_X_HIDDEN, MENU_Y, MENU_W, MENU_H, 0xFFFFFF, 10);
  lv_obj_set_style_border_width(g_menu_panel, 0, 0);

  lv_obj_t *title = make_label(g_menu_panel, "Menu", 0x000000);
  set_label_font(title, g_font_title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 12);

  build_menu_row(g_menu_panel, 42, "Cards", 1);
  build_menu_row(g_menu_panel, 76, "Refresh", 0);
  build_menu_row(g_menu_panel, 110, "Status", 0);
  build_menu_row(g_menu_panel, 144, "Config", 0);
}

static void set_menu_x(void *obj, int32_t x)
{
  lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)x);
}

static void set_menu_visible(int visible)
{
  if(!g_menu_panel) return;
  g_menu_visible = visible ? 1 : 0;

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, g_menu_panel);
  lv_anim_set_values(&anim,
                     lv_obj_get_x(g_menu_panel),
                     g_menu_visible ? MENU_X_SHOWN : MENU_X_HIDDEN);
  lv_anim_set_time(&anim, 180);
  lv_anim_set_exec_cb(&anim, set_menu_x);
  lv_anim_start(&anim);
}

static void ui_update_ha_status(const char *status, int connected, int have_states)
{
  (void)status;
  (void)connected;
  (void)have_states;
}

void ui_init(lv_group_t *grp)
{
  g_should_exit = 0;
  g_menu_visible = 0;

  lv_obj_t *scr = lv_scr_act();
  init_fonts();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *top = make_panel(scr, 0, 0, SCREEN_W, TOP_H, 0x006DCC, 0);
  g_wifi_img = lv_img_create(top);
  lv_img_set_src(g_wifi_img, &jive_icon_wireless_4);
  lv_obj_align(g_wifi_img, LV_ALIGN_LEFT_MID, 8, 0);

  g_time_label = make_label(top, "", 0xFFFFFF);
  set_label_font(g_time_label, g_font_top);
  lv_obj_align(g_time_label, LV_ALIGN_CENTER, 0, 0);

  g_power_img = lv_img_create(top);
  lv_img_set_src(g_power_img, &jive_icon_battery_ac);
  lv_obj_align(g_power_img, LV_ALIGN_RIGHT_MID, -8, 0);

  lv_obj_t *main_area = make_panel(scr, MAIN_X, MAIN_Y, MAIN_W, MAIN_H, 0x404040, 8);
  build_card(main_area, 8, "Living Room", "On", 1);
  build_card(main_area, 86, "Media", "Paused", 0);
  build_card(main_area, 164, "Cover", "Closed", 0);

  lv_obj_t *footer = make_panel(scr, 0, FOOTER_Y, SCREEN_W, FOOTER_H, 0x000000, 0);
  lv_obj_t *hint = make_label(footer, "Home: Menu", 0xFFFFFF);
  set_label_font(hint, g_font_small);
  lv_obj_align(hint, LV_ALIGN_LEFT_MID, 8, 0);

  build_menu(scr);
  status_update();
  lv_timer_create(status_timer_cb, 1000, NULL);
  lv_timer_create(keep_visible, 30, NULL);
  lv_timer_create(ha_poll_timer_cb, 100, NULL);

  (void)grp;
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
  g_wifi_img = NULL;
  g_time_label = NULL;
  g_power_img = NULL;
  g_menu_panel = NULL;

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

void ha_poll_timer_cb(lv_timer_t *t)
{
  (void)t;
  ha_poll_timer();
}

void ui_status_set(const char *s, int connected, int have_states)
{
  ui_update_ha_status(s, connected, have_states);
}
