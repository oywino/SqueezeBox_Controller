#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "ui.h"
#include "audio_feedback.h"
#include "ha_rest.h"
#include "status_cache.h"
#include "ha_ws.h"
#include "media_art.h"
#include "assets/jive_assets.h"
#include "src/draw/lv_draw_triangle.h"
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
#define MENU_ROW_COUNT 5
#define CARD_COUNT 4
#define CARD_VISIBLE_COUNT 3
#define CARD_H 70
#define CARD_Y0 8
#define CARD_STEP 78
#define CARD_SCROLL_MIN_MS 400
#define COVER_ANIM_FIRST_MS 500
#define COVER_ANIM_GAP_MS 250
#define COVER_ANIM_CYCLE_MS (COVER_ANIM_FIRST_MS + COVER_ANIM_GAP_MS + COVER_ANIM_FIRST_MS + COVER_ANIM_GAP_MS)
#define COVER_ANIM_COLOR 0xFFFFFF

static int g_should_exit = 0;
static int g_menu_visible = 0;
static int g_menu_selected = 0;
static int g_card_top = 0;
static int g_card_focus = 0;
static int g_cover_motion = 0;
static uint64_t g_last_cover_refresh_ms = 0;
static uint64_t g_last_card_scroll_ms = 0;
static lv_obj_t *g_wifi_img = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_power_img = NULL;
static lv_obj_t *g_card_panels[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_card_icons[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_card_titles[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_card_states[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_card_toggle_tracks[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_card_toggle_knobs[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_cover_left_1[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_cover_left_2[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_cover_pause[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_cover_right_1[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_cover_right_2[CARD_VISIBLE_COUNT] = { NULL };
static lv_obj_t *g_media_view = NULL;
static lv_obj_t *g_media_title = NULL;
static lv_obj_t *g_media_pause_icon = NULL;
static lv_obj_t *g_media_play_icon = NULL;
static lv_obj_t *g_media_artist_album = NULL;
static lv_obj_t *g_media_elapsed = NULL;
static lv_obj_t *g_media_remaining = NULL;
static lv_obj_t *g_media_progress_fill = NULL;
static lv_obj_t *g_media_art_panel = NULL;
static lv_obj_t *g_media_art_img = NULL;
static unsigned long g_media_art_version = 0;
static lv_obj_t *g_menu_panel = NULL;
static lv_obj_t *g_menu_rows[MENU_ROW_COUNT] = { NULL };
static lv_obj_t *g_menu_labels[MENU_ROW_COUNT] = { NULL };
static lv_font_t *g_font_top = NULL;
static lv_font_t *g_font_menu = NULL;
static lv_font_t *g_font_menu_sel = NULL;
static lv_font_t *g_font_title = NULL;
static lv_font_t *g_font_state = NULL;
static lv_font_t *g_font_small = NULL;
static void (*g_cover_refresh_cb)(void) = NULL;

struct cover_triangle_state {
  int direction;
  uint32_t color;
};

static struct cover_triangle_state g_cover_triangle_state[CARD_VISIBLE_COUNT][4];
static struct cover_triangle_state g_media_play_triangle_state = { 1, 0xEBF4FF };

struct ui_card_def {
  const char *title;
  const char *entity_id;
};

static const struct ui_card_def g_cards[CARD_COUNT] = {
  { "Light", "light.sov_2_tak" },
  { "Cover", "cover.screen_sov_2" },
  { "Switch", "switch.ikea_power_plug" },
  { "Media", "media_player.squeezebox_boom" }
};

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

static void cover_triangle_draw_cb(lv_event_t *e)
{
  struct cover_triangle_state *state = lv_event_get_user_data(e);
  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
  lv_obj_t *obj = lv_event_get_target(e);
  lv_area_t a;
  lv_point_t points[3];
  lv_draw_rect_dsc_t draw_dsc;

  if(!state || !draw_ctx || !obj) return;

  lv_obj_get_coords(obj, &a);
  if(state->direction < 0) {
    points[0].x = a.x2; points[0].y = a.y1;
    points[1].x = a.x1; points[1].y = a.y1 + (a.y2 - a.y1) / 2;
    points[2].x = a.x2; points[2].y = a.y2;
  } else {
    points[0].x = a.x1; points[0].y = a.y1;
    points[1].x = a.x2; points[1].y = a.y1 + (a.y2 - a.y1) / 2;
    points[2].x = a.x1; points[2].y = a.y2;
  }

  lv_draw_rect_dsc_init(&draw_dsc);
  draw_dsc.bg_color = lv_color_hex(state->color);
  draw_dsc.bg_opa = LV_OPA_COVER;
  lv_draw_triangle(draw_ctx, &draw_dsc, points);
}

static void set_triangle_color(lv_obj_t *obj, uint32_t color)
{
  struct cover_triangle_state *state;
  if(!obj) return;
  state = lv_obj_get_event_user_data(obj, cover_triangle_draw_cb);
  if(!state) return;
  if(state->color == color) return;
  state->color = color;
  lv_obj_invalidate(obj);
}

static lv_obj_t *make_triangle(lv_obj_t *parent, int slot, int idx, int direction, uint32_t color)
{
  lv_obj_t *triangle = lv_obj_create(parent);
  lv_obj_remove_style_all(triangle);
  lv_obj_set_size(triangle, 18, 22);
  lv_obj_clear_flag(triangle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(triangle, LV_OBJ_FLAG_SCROLLABLE);
  g_cover_triangle_state[slot][idx].direction = direction;
  g_cover_triangle_state[slot][idx].color = color;
  lv_obj_add_event_cb(triangle, cover_triangle_draw_cb, LV_EVENT_DRAW_MAIN, &g_cover_triangle_state[slot][idx]);
  return triangle;
}

static lv_obj_t *make_pause_symbol(lv_obj_t *parent, uint32_t color)
{
  lv_obj_t *pause = lv_obj_create(parent);
  lv_obj_remove_style_all(pause);
  lv_obj_set_size(pause, 22, 24);
  lv_obj_clear_flag(pause, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(pause, LV_OBJ_FLAG_SCROLLABLE);
  (void)make_panel(pause, 4, 1, 5, 22, color, 0);
  (void)make_panel(pause, 13, 1, 5, 22, color, 0);
  return pause;
}

static lv_obj_t *make_small_pause_symbol(lv_obj_t *parent, uint32_t color)
{
  lv_obj_t *pause = lv_obj_create(parent);
  lv_obj_remove_style_all(pause);
  lv_obj_set_size(pause, 12, 14);
  lv_obj_clear_flag(pause, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(pause, LV_OBJ_FLAG_SCROLLABLE);
  (void)make_panel(pause, 2, 1, 3, 12, color, 0);
  (void)make_panel(pause, 7, 1, 3, 12, color, 0);
  return pause;
}

static void set_label_font(lv_obj_t *label, const lv_font_t *font)
{
  if(font) lv_obj_set_style_text_font(label, font, 0);
}

static void fmt_time(int seconds, char *out, size_t out_size, int negative)
{
  int min;
  int sec;
  if(!out || out_size == 0) return;
  if(seconds < 0) seconds = 0;
  min = seconds / 60;
  sec = seconds % 60;
  snprintf(out, out_size, "%s%d:%02d", negative ? "-" : "", min, sec);
}

static int media_state_loaded(const char *state)
{
  if(!state || !*state) return 0;
  if(strcmp(state, "idle") == 0) return 0;
  if(strcmp(state, "off") == 0) return 0;
  if(strcmp(state, "unavailable") == 0) return 0;
  if(strcmp(state, "unknown") == 0) return 0;
  return 1;
}

static int media_loaded_now(void)
{
  const char *state = ha_rest_get_cached_state("media_player.squeezebox_boom");
  const char *title = ha_rest_get_cached_media_title("media_player.squeezebox_boom");
  return media_state_loaded(state) && title && *title;
}

static void set_obj_hidden(lv_obj_t *obj, int hidden)
{
  if(!obj) return;
  if(hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
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

static void set_card_slot(int slot)
{
  int card_idx = g_card_top + slot;
  int active = (slot == g_card_focus);
  int is_cover = 0;
  int is_light = 0;
  int is_switch = 0;
  int is_media = 0;
  int toggle_on = 0;
  const char *cached_state = NULL;
  const char *media_title = NULL;
  uint32_t fill = active ? 0xF4F4F4 : 0xD8D8D8;
  uint32_t state_color = active ? 0x006DCC : 0x404040;

  if(slot < 0 || slot >= CARD_VISIBLE_COUNT || card_idx >= CARD_COUNT) return;
  if(!g_card_panels[slot] || !g_card_titles[slot] || !g_card_states[slot] ||
     !g_card_icons[slot] ||
     !g_card_toggle_tracks[slot] || !g_card_toggle_knobs[slot] ||
     !g_cover_left_1[slot] || !g_cover_left_2[slot] ||
     !g_cover_pause[slot] || !g_cover_right_1[slot] || !g_cover_right_2[slot]) return;

  is_cover = strcmp(g_cards[card_idx].entity_id, "cover.screen_sov_2") == 0;
  is_light = strcmp(g_cards[card_idx].entity_id, "light.sov_2_tak") == 0;
  is_switch = strcmp(g_cards[card_idx].entity_id, "switch.ikea_power_plug") == 0;
  is_media = strcmp(g_cards[card_idx].entity_id, "media_player.squeezebox_boom") == 0;

  lv_obj_set_style_bg_color(g_card_panels[slot], lv_color_hex(fill), 0);
  lv_obj_set_style_text_color(g_card_titles[slot], lv_color_hex(0x101010), 0);
  lv_obj_clear_flag(g_card_panels[slot], LV_OBJ_FLAG_HIDDEN);

  set_obj_hidden(g_cover_left_1[slot], !is_cover);
  set_obj_hidden(g_cover_left_2[slot], !is_cover);
  set_obj_hidden(g_cover_pause[slot], !is_cover);
  set_obj_hidden(g_cover_right_1[slot], !is_cover);
  set_obj_hidden(g_cover_right_2[slot], !is_cover);

  if(is_cover) {
    lv_obj_add_flag(g_card_icons[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_card_toggle_tracks[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_card_toggle_knobs[slot], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_card_titles[slot], "Sov 2 Screen");
    lv_obj_set_width(g_card_titles[slot], MAIN_W - 16);
    lv_obj_set_style_text_align(g_card_titles[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_card_titles[slot], LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(g_card_states[slot], "");
    lv_obj_add_flag(g_card_states[slot], LV_OBJ_FLAG_HIDDEN);
  } else if(is_light || is_switch) {
    cached_state = ha_rest_get_cached_state(g_cards[card_idx].entity_id);
    toggle_on = cached_state && strcmp(cached_state, "on") == 0;

    lv_obj_add_flag(g_card_icons[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_card_toggle_tracks[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_card_toggle_knobs[slot], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_card_titles[slot], is_light ? "Sov 2 Tak" : "IKEA Power Plug");
    lv_obj_set_width(g_card_titles[slot], 140);
    lv_obj_set_style_text_align(g_card_titles[slot], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_card_titles[slot], LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_text(g_card_states[slot], "");
    lv_obj_add_flag(g_card_states[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_card_toggle_tracks[slot],
                              lv_color_hex(toggle_on ? 0x006DCC : 0x8A8A8A),
                              0);
    lv_obj_set_x(g_card_toggle_knobs[slot], toggle_on ? 18 : 2);
  } else if(is_media) {
    lv_obj_add_flag(g_card_icons[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_card_toggle_tracks[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_card_toggle_knobs[slot], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_card_titles[slot], "Squeezebox Boom");
    lv_obj_set_width(g_card_titles[slot], MAIN_W - 24);
    lv_obj_set_style_text_align(g_card_titles[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_card_titles[slot], LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_clear_flag(g_card_states[slot], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_card_states[slot], "Nothing");
    lv_obj_set_width(g_card_states[slot], MAIN_W - 24);
    lv_obj_set_style_text_align(g_card_states[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_card_states[slot], LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_color(g_card_states[slot], lv_color_hex(state_color), 0);
  } else {
    lv_obj_clear_flag(g_card_icons[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_card_toggle_tracks[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_card_toggle_knobs[slot], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_card_icons[slot], LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(g_card_icons[slot], lv_color_hex(0x101010), 0);
    lv_obj_set_width(g_card_titles[slot], 140);
    lv_obj_set_style_text_align(g_card_titles[slot], LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(g_card_titles[slot], g_cards[card_idx].title);
    lv_obj_align(g_card_titles[slot], LV_ALIGN_TOP_LEFT, 46, 14);
    lv_obj_clear_flag(g_card_states[slot], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_card_states[slot], g_cards[card_idx].entity_id);
    lv_obj_set_style_text_color(g_card_states[slot], lv_color_hex(state_color), 0);
  }
}

static void update_media_view(void)
{
  const char *entity_id = "media_player.squeezebox_boom";
  const char *state = ha_rest_get_cached_state(entity_id);
  const char *title = ha_rest_get_cached_media_title(entity_id);
  const char *artist = ha_rest_get_cached_media_artist(entity_id);
  const char *album = ha_rest_get_cached_media_album(entity_id);
  int selected = g_card_top + g_card_focus;
  int position = 0;
  int duration = 0;
  int have_position = ha_rest_get_cached_media_position(entity_id, &position);
  int have_duration = ha_rest_get_cached_media_duration(entity_id, &duration);
  char elapsed[16];
  char remaining[16];
  char artist_album[192];
  int fill_w = 0;
  unsigned long art_version;
  const lv_img_dsc_t *art_image;

  if(!g_media_view) return;

  if(selected != 3 || !media_state_loaded(state) || !title || !*title) {
    lv_obj_add_flag(g_media_view, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(g_media_view, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_media_title, title);
  if(state && strcmp(state, "playing") == 0) {
    lv_obj_add_flag(g_media_play_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_media_pause_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(g_media_play_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_media_pause_icon, LV_OBJ_FLAG_HIDDEN);
  }

  if(artist && album) snprintf(artist_album, sizeof(artist_album), "%s • %s", artist, album);
  else if(artist) snprintf(artist_album, sizeof(artist_album), "%s", artist);
  else if(album) snprintf(artist_album, sizeof(artist_album), "%s", album);
  else snprintf(artist_album, sizeof(artist_album), "%s", "");
  lv_label_set_text(g_media_artist_album, artist_album);

  fmt_time(have_position ? position : 0, elapsed, sizeof(elapsed), 0);
  fmt_time(have_duration && have_position ? duration - position : 0, remaining, sizeof(remaining), 1);
  lv_label_set_text(g_media_elapsed, elapsed);
  lv_label_set_text(g_media_remaining, remaining);

  if(have_duration && duration > 0 && have_position) {
    fill_w = (position * 129) / duration;
    if(fill_w < 0) fill_w = 0;
    if(fill_w > 129) fill_w = 129;
  }
  lv_obj_set_width(g_media_progress_fill, fill_w);

  art_image = media_art_get_image(&art_version);
  if(g_media_art_img && g_media_art_version != art_version) {
    g_media_art_version = art_version;
    if(art_image) {
      lv_img_set_zoom(g_media_art_img, LV_IMG_ZOOM_NONE);
      lv_img_cache_invalidate_src(art_image);
      lv_img_set_src(g_media_art_img, art_image);
      lv_obj_clear_flag(g_media_art_img, LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(g_media_art_img);
      lv_obj_align(g_media_art_img, LV_ALIGN_TOP_MID, 0, 0);
    } else {
      lv_obj_add_flag(g_media_art_img, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void update_cover_animation(void)
{
  uint64_t now = ms_now();
  int phase = (int)(now % COVER_ANIM_CYCLE_MS);

  for(int slot = 0; slot < CARD_VISIBLE_COUNT; ++slot) {
    int card_idx = g_card_top + slot;
    const char *state;
    int opening;
    int closing;
    int position = -1;
    lv_obj_t *first = NULL;
    lv_obj_t *second = NULL;

    if(card_idx < 0 || card_idx >= CARD_COUNT) continue;
    if(strcmp(g_cards[card_idx].entity_id, "cover.screen_sov_2") != 0) continue;

    state = ha_rest_get_cached_state("cover.screen_sov_2");
    if(g_cover_motion != 0 &&
       ha_rest_get_cached_position("cover.screen_sov_2", &position) &&
       ((g_cover_motion < 0 && position >= 100) ||
        (g_cover_motion > 0 && position <= 0))) {
      g_cover_motion = 0;
    }
    if(g_cover_motion != 0 && g_cover_refresh_cb &&
       now - g_last_cover_refresh_ms >= 1000) {
      g_last_cover_refresh_ms = now;
      g_cover_refresh_cb();
    }
    opening = g_cover_motion < 0 || (state && strcmp(state, "opening") == 0);
    closing = g_cover_motion > 0 || (state && strcmp(state, "closing") == 0);

    set_triangle_color(g_cover_left_1[slot], 0x101010);
    set_triangle_color(g_cover_left_2[slot], 0x101010);
    set_triangle_color(g_cover_right_1[slot], 0x101010);
    set_triangle_color(g_cover_right_2[slot], 0x101010);
    lv_obj_set_style_text_color(g_cover_pause[slot], lv_color_hex(0x101010), 0);

    if(closing) {
      first = g_cover_right_1[slot];
      second = g_cover_right_2[slot];
    } else if(opening) {
      first = g_cover_left_1[slot];
      second = g_cover_left_2[slot];
    } else {
      continue;
    }

    if(phase < COVER_ANIM_FIRST_MS) {
      set_triangle_color(first, COVER_ANIM_COLOR);
    } else if(phase < COVER_ANIM_FIRST_MS + COVER_ANIM_GAP_MS) {
      /* both black */
    } else if(phase < COVER_ANIM_FIRST_MS + COVER_ANIM_GAP_MS + COVER_ANIM_FIRST_MS) {
      set_triangle_color(second, COVER_ANIM_COLOR);
    }
  }
}

static void cover_anim_timer_cb(lv_timer_t *t)
{
  (void)t;
  update_cover_animation();
}

static void media_view_timer_cb(lv_timer_t *t)
{
  (void)t;
  update_media_view();
}

static void refresh_cards(void)
{
  if(media_loaded_now() && g_card_top + g_card_focus != 3) {
    g_card_top = 0;
    if(g_card_focus > 2) g_card_focus = 2;
  }
  for(int i = 0; i < CARD_VISIBLE_COUNT; ++i) {
    lv_obj_clear_flag(g_card_panels[i], LV_OBJ_FLAG_HIDDEN);
    set_card_slot(i);
  }
  update_media_view();
}

static void build_card_slot(lv_obj_t *main_area, int slot)
{
  lv_obj_t *card = make_panel(main_area, 8, CARD_Y0 + slot * CARD_STEP,
                              MAIN_W - 16, CARD_H, 0xD8D8D8, 8);

  lv_obj_t *icon = make_label(card, LV_SYMBOL_HOME, 0x101010);
  set_label_font(icon, g_font_title);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);

  lv_obj_t *name = make_label(card, "", 0x101010);
  set_label_font(name, g_font_title);
  lv_obj_set_width(name, 140);
  lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 46, 14);

  lv_obj_t *value = make_label(card, "", 0x404040);
  set_label_font(value, g_font_state);
  lv_obj_set_width(value, 150);
  lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
  lv_obj_align(value, LV_ALIGN_TOP_LEFT, 46, 38);

  lv_obj_t *toggle = make_panel(card, 164, 24, 36, 20, 0x006DCC, 10);
  lv_obj_t *knob = make_panel(toggle, 18, 2, 16, 16, 0xFFFFFF, 8);
  lv_obj_add_flag(toggle, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(knob, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *left1 = make_triangle(card, slot, 0, -1, 0x101010);
  lv_obj_t *left2 = make_triangle(card, slot, 1, -1, 0x101010);
  lv_obj_t *pause = make_pause_symbol(card, 0x101010);
  lv_obj_t *right1 = make_triangle(card, slot, 2, 1, 0x101010);
  lv_obj_t *right2 = make_triangle(card, slot, 3, 1, 0x101010);
  lv_obj_align(left1, LV_ALIGN_TOP_MID, -57, 38);
  lv_obj_align(left2, LV_ALIGN_TOP_MID, -36, 38);
  lv_obj_align(pause, LV_ALIGN_TOP_MID, 0, 37);
  lv_obj_align(right1, LV_ALIGN_TOP_MID, 36, 38);
  lv_obj_align(right2, LV_ALIGN_TOP_MID, 57, 38);
  lv_obj_add_flag(left1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(left2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(pause, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(right1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(right2, LV_OBJ_FLAG_HIDDEN);

  g_card_panels[slot] = card;
  g_card_icons[slot] = icon;
  g_card_titles[slot] = name;
  g_card_states[slot] = value;
  g_card_toggle_tracks[slot] = toggle;
  g_card_toggle_knobs[slot] = knob;
  g_cover_left_1[slot] = left1;
  g_cover_left_2[slot] = left2;
  g_cover_pause[slot] = pause;
  g_cover_right_1[slot] = right1;
  g_cover_right_2[slot] = right2;
  set_card_slot(slot);
}

static void set_menu_row_selected(int idx, int selected)
{
  if(idx < 0 || idx >= MENU_ROW_COUNT || !g_menu_rows[idx] || !g_menu_labels[idx]) return;

  lv_obj_set_style_bg_color(g_menu_rows[idx], lv_color_hex(selected ? 0x006DCC : 0xFFFFFF), 0);
  lv_obj_set_style_text_color(g_menu_labels[idx], lv_color_hex(selected ? 0xFFFFFF : 0x000000), 0);
  set_label_font(g_menu_labels[idx], selected ? g_font_menu_sel : g_font_menu);
}

static void set_menu_selected(int selected)
{
  if(selected < 0) selected = 0;
  if(selected >= MENU_ROW_COUNT) selected = MENU_ROW_COUNT - 1;
  if(selected == g_menu_selected) return;

  set_menu_row_selected(g_menu_selected, 0);
  g_menu_selected = selected;
  set_menu_row_selected(g_menu_selected, 1);
}

static lv_obj_t *build_menu_row(lv_obj_t *parent, int idx, lv_coord_t y, const char *text)
{
  int selected = (idx == g_menu_selected);
  lv_obj_t *row = make_panel(parent, 10, y, MENU_W - 24, 28,
                             selected ? 0x006DCC : 0xFFFFFF, 5);

  lv_obj_t *label = make_label(row, text, selected ? 0xFFFFFF : 0x000000);
  set_label_font(label, selected ? g_font_menu_sel : g_font_menu);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
  if(idx >= 0 && idx < MENU_ROW_COUNT) {
    g_menu_rows[idx] = row;
    g_menu_labels[idx] = label;
  }
  return row;
}

static void build_menu(lv_obj_t *scr)
{
  g_menu_panel = make_panel(scr, MENU_X_HIDDEN, MENU_Y, MENU_W, MENU_H, 0xFFFFFF, 10);
  lv_obj_set_style_border_width(g_menu_panel, 0, 0);

  build_menu_row(g_menu_panel, 0, 12, "Cards");
  build_menu_row(g_menu_panel, 1, 46, "Refresh");
  build_menu_row(g_menu_panel, 2, 80, "Status");
  build_menu_row(g_menu_panel, 3, 114, "Config");
  build_menu_row(g_menu_panel, 4, 148, "More");
}

static void build_media_view(lv_obj_t *scr)
{
  g_media_view = make_panel(scr, 0, TOP_H, SCREEN_W, SCREEN_H - TOP_H, 0x16181E, 0);
  lv_obj_add_flag(g_media_view, LV_OBJ_FLAG_HIDDEN);

  g_media_title = make_label(g_media_view, "", 0xEBF4FF);
  set_label_font(g_media_title, g_font_title);
  lv_obj_set_width(g_media_title, 170);
  lv_label_set_long_mode(g_media_title, LV_LABEL_LONG_CLIP);
  lv_obj_align(g_media_title, LV_ALIGN_TOP_LEFT, 8, 6);

  g_media_pause_icon = make_small_pause_symbol(g_media_view, 0xEBF4FF);
  lv_obj_align(g_media_pause_icon, LV_ALIGN_TOP_RIGHT, -15, 17);

  g_media_play_icon = lv_obj_create(g_media_view);
  lv_obj_remove_style_all(g_media_play_icon);
  lv_obj_set_size(g_media_play_icon, 10, 12);
  lv_obj_clear_flag(g_media_play_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(g_media_play_icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_media_play_icon, cover_triangle_draw_cb, LV_EVENT_DRAW_MAIN, &g_media_play_triangle_state);
  lv_obj_align(g_media_play_icon, LV_ALIGN_TOP_RIGHT, -16, 18);
  lv_obj_add_flag(g_media_play_icon, LV_OBJ_FLAG_HIDDEN);

  g_media_artist_album = make_label(g_media_view, "", 0xAFC0D2);
  set_label_font(g_media_artist_album, g_font_state);
  lv_obj_set_width(g_media_artist_album, 220);
  lv_label_set_long_mode(g_media_artist_album, LV_LABEL_LONG_CLIP);
  lv_obj_align(g_media_artist_album, LV_ALIGN_TOP_LEFT, 9, 39);

  g_media_elapsed = make_label(g_media_view, "0:00", 0xEBF4FF);
  set_label_font(g_media_elapsed, g_font_small);
  lv_obj_align(g_media_elapsed, LV_ALIGN_TOP_LEFT, 9, 65);

  g_media_remaining = make_label(g_media_view, "-0:00", 0xEBF4FF);
  set_label_font(g_media_remaining, g_font_small);
  lv_obj_align(g_media_remaining, LV_ALIGN_TOP_RIGHT, -6, 65);

  (void)make_panel(g_media_view, 61, 75, 129, 2, 0x788796, 1);
  g_media_progress_fill = make_panel(g_media_view, 61, 75, 0, 2, 0xEBF4FF, 1);
  g_media_art_panel = make_panel(g_media_view, 0, 86, 240, 204, 0x30343A, 0);
  g_media_art_img = lv_img_create(g_media_art_panel);
  lv_obj_add_flag(g_media_art_img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(g_media_art_img, LV_ALIGN_CENTER, 0, 0);
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
  g_menu_selected = 0;
  g_card_top = 0;
  g_card_focus = 0;
  g_last_card_scroll_ms = 0;
  g_media_view = NULL;
  g_media_title = NULL;
  g_media_pause_icon = NULL;
  g_media_play_icon = NULL;
  g_media_artist_album = NULL;
  g_media_elapsed = NULL;
  g_media_remaining = NULL;
  g_media_progress_fill = NULL;
  g_media_art_panel = NULL;
  g_media_art_img = NULL;
  g_media_art_version = 0;
  for(int i = 0; i < CARD_VISIBLE_COUNT; ++i) {
    g_card_panels[i] = NULL;
    g_card_icons[i] = NULL;
    g_card_titles[i] = NULL;
    g_card_states[i] = NULL;
    g_card_toggle_tracks[i] = NULL;
    g_card_toggle_knobs[i] = NULL;
    g_cover_left_1[i] = NULL;
    g_cover_left_2[i] = NULL;
    g_cover_pause[i] = NULL;
    g_cover_right_1[i] = NULL;
    g_cover_right_2[i] = NULL;
  }
  for(int i = 0; i < MENU_ROW_COUNT; ++i) {
    g_menu_rows[i] = NULL;
    g_menu_labels[i] = NULL;
  }

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
  build_card_slot(main_area, 0);
  build_card_slot(main_area, 1);
  build_card_slot(main_area, 2);

  lv_obj_t *footer = make_panel(scr, 0, FOOTER_Y, SCREEN_W, FOOTER_H, 0x000000, 0);
  lv_obj_t *hint = make_label(footer, "Home: Menu", 0xFFFFFF);
  set_label_font(hint, g_font_small);
  lv_obj_align(hint, LV_ALIGN_LEFT_MID, 8, 0);

  build_media_view(scr);
  build_menu(scr);
  status_update();
  lv_timer_create(status_timer_cb, 1000, NULL);
  lv_timer_create(ha_poll_timer_cb, 100, NULL);
  lv_timer_create(cover_anim_timer_cb, 20, NULL);
  lv_timer_create(media_view_timer_cb, 250, NULL);

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

void ui_cover_note_command(const char *entity_id, const char *command)
{
  int position = -1;
  int have_position;
  if(!entity_id || strcmp(entity_id, "cover.screen_sov_2") != 0 || !command) return;

  have_position = ha_rest_get_cached_position("cover.screen_sov_2", &position);

  if(strcmp(command, "opening") == 0) {
    g_cover_motion = have_position && position >= 100 ? 0 : -1;
    if(g_cover_motion != 0) ha_rest_clear_cached_position("cover.screen_sov_2");
  } else if(strcmp(command, "closing") == 0) {
    g_cover_motion = have_position && position <= 0 ? 0 : 1;
    if(g_cover_motion != 0) ha_rest_clear_cached_position("cover.screen_sov_2");
  } else {
    g_cover_motion = 0;
    ha_rest_clear_cached_position("cover.screen_sov_2");
  }
  fprintf(stderr, "[ui_cover] command=%s cached_position=%d motion=%d\n",
          command,
          position,
          g_cover_motion);
}

void ui_set_cover_refresh_callback(void (*refresh)(void))
{
  g_cover_refresh_cb = refresh;
}

int ui_menu_wheel(int diff)
{
  if(diff == 0) return 0;

  if(!g_menu_visible) {
    int old_top = g_card_top;
    int old_focus = g_card_focus;
    int selected = g_card_top + g_card_focus;
    int media_loaded = media_loaded_now();
    uint64_t now = ms_now();

    if(g_last_card_scroll_ms != 0 && now - g_last_card_scroll_ms < CARD_SCROLL_MIN_MS) {
      return 1;
    }

    if(diff > 0 && selected < CARD_COUNT - 1) {
      if(g_card_focus < CARD_VISIBLE_COUNT - 1) {
        g_card_focus++;
      } else {
        g_card_top++;
      }
    } else if(diff < 0 && selected > 0) {
      if(media_loaded && selected == 3) {
        g_card_top = 0;
        g_card_focus = 2;
      } else if(g_card_focus > 0) {
        g_card_focus--;
      } else {
        g_card_top--;
      }
    }

    if(g_card_top != old_top || g_card_focus != old_focus) {
      g_last_card_scroll_ms = now;
      refresh_cards();
      audio_feedback_play(AUDIO_FEEDBACK_MOVE);
    }
    return 1;
  }

  int old_selected = g_menu_selected;
  set_menu_selected(g_menu_selected + (diff > 0 ? 1 : -1));
  if(g_menu_selected != old_selected) audio_feedback_play(AUDIO_FEEDBACK_MOVE);
  return 1;
}

int ui_menu_is_visible(void)
{
  return g_menu_visible ? 1 : 0;
}

void ui_refresh_cards(void)
{
  refresh_cards();
}

const char *ui_focused_card_entity_id(void)
{
  int idx = g_card_top + g_card_focus;
  if(idx < 0 || idx >= CARD_COUNT) return "";
  return g_cards[idx].entity_id;
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
