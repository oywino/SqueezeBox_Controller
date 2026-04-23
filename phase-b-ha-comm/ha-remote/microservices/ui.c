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
static void (*g_on_start)(void) = NULL;

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

static void btn_probe_cb(lv_event_t *e)
{
  (void)e;
  if(g_on_start) g_on_start();
}

static void btn_exit_cb(lv_event_t *e)
{
  (void)e;
  g_should_exit = 1;
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

void ui_set_on_start(void (*fn)(void))
{
  g_on_start = fn;
}

void ui_init(lv_group_t *grp)
{
  g_should_exit = 0;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  /* Bottom-right power indicator */
  g_power_label = lv_label_create(scr);
  lv_label_set_text(g_power_label, "");
  lv_obj_set_style_text_color(g_power_label, lv_color_white(), 0);
  lv_obj_align(g_power_label, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
  pwr_indicator_update();
  lv_timer_create(pwr_indicator_timer_cb, 1000, NULL);

  /* Button 1: Start HA session */
  lv_obj_t *btn1 = lv_btn_create(scr);
  lv_obj_set_size(btn1, 200, 46);
  lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_border_width(btn1, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(btn1, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(btn1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
  if(grp) lv_group_add_obj(grp, btn1);
  lv_obj_add_event_cb(btn1, btn_probe_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *t1 = lv_label_create(btn1);
  lv_label_set_text(t1, "Start HA Session");
  lv_obj_center(t1);

  /* Button 2: Exit */
  lv_obj_t *btn2 = lv_btn_create(scr);
  lv_obj_set_size(btn2, 200, 46);
  lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 40);
  lv_obj_set_style_border_width(btn2, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(btn2, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(btn2, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
  if(grp) lv_group_add_obj(grp, btn2);
  lv_obj_add_event_cb(btn2, btn_exit_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *t2 = lv_label_create(btn2);
  lv_label_set_text(t2, "Exit");
  lv_obj_center(t2);

  lv_group_focus_obj(btn1);

  /* Keep LVGL refreshing */
  lv_timer_create(keep_visible, 30, NULL);

  /* Poll timer wrapper */
  lv_timer_create(ha_poll_timer_cb, 100, NULL);
}

int ui_should_exit(void)
{
  return g_should_exit ? 1 : 0;
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
