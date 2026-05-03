#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "input.h"
#include "ha_config.h"
#include "ha_rest.h"
#include "ha_ws.h"
#include "ui.h"
#include "stockui.h"
#include "audio_feedback.h"
#include "hal.h"
#include "power_manager.h"
#include "status_cache.h"
#include "media_art.h"

#define ENCODER_PUSH_CODE 106
#define SB_KEY_REWIND 165
#define SB_KEY_PAUSE 164
#define SB_KEY_FASTFORWARD 163
#define MVP_LIGHT_ENTITY_ID "light.sov_2_tak"
#define MVP_LIGHT_SERVICE "light.toggle"
#define MVP_COVER_ENTITY_ID "cover.screen_sov_2"
#define MVP_COVER_CLOSE_SERVICE "cover.close_cover"
#define MVP_COVER_STOP_SERVICE "cover.stop_cover"
#define MVP_COVER_OPEN_SERVICE "cover.open_cover"
#define MVP_SWITCH_ENTITY_ID "switch.ikea_power_plug"
#define MVP_SWITCH_SERVICE "switch.toggle"
#define MVP_MEDIA_ENTITY_ID "media_player.squeezebox_boom"
#define MVP_MEDIA_PLAY_PAUSE_SERVICE "media_player.media_play_pause"

static pthread_mutex_t g_action_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_action_in_flight = 0;

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int input_activity(void)
{
    return power_manager_note_activity(ms_now());
}

static int token_is_placeholder(const char *token)
{
    return !token || !*token || token[0] == '<';
}

static int read_token_file(const char *path, char *out, size_t out_size)
{
    FILE *fp;
    size_t len;

    if (!path || !out || out_size == 0) {
        return 0;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    len = fread(out, 1, out_size - 1, fp);
    fclose(fp);
    out[len] = '\0';

    while (len > 0 &&
           (out[len - 1] == '\n' || out[len - 1] == '\r' ||
            out[len - 1] == ' ' || out[len - 1] == '\t')) {
        out[--len] = '\0';
    }

    return len > 0;
}

static void resolve_ha_connection(char *base_url_buf,
                                  size_t base_url_buf_size,
                                  char *token_buf,
                                  size_t token_buf_size,
                                  const char **base_url,
                                  const char **token)
{
    const char *ha_host = getenv("HA_HOST");
    const char *ha_token_env = getenv("HA_TOKEN");

    *base_url = ha_config_get_ha_base_url();
    *token = ha_config_get_ha_access_token();

    if (ha_host && *ha_host) {
        snprintf(base_url_buf, base_url_buf_size, "http://%s:8123", ha_host);
        *base_url = base_url_buf;
    }

    if (ha_token_env && *ha_token_env) {
        *token = ha_token_env;
    } else if (token_is_placeholder(*token)) {
        if (read_token_file("HA_LL_Token.txt", token_buf, token_buf_size) ||
            read_token_file("/mnt/storage/phase-a-lvgl/HA_LL_Token.txt",
                            token_buf,
                            token_buf_size)) {
            *token = token_buf;
        }
    }
}

static void fetch_configured_ha_states(void)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)ha_rest_fetch_configured_states(base_url, token);
}

static void fetch_cover_state(void)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)ha_rest_fetch_state(base_url, token, MVP_COVER_ENTITY_ID);
}

static void start_ha_state_subscription(void)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)ha_session_subscribe_state_changes(base_url, token);
}

static void start_media_art_service(void)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)media_art_start(base_url, token, "media_player.squeezebox_boom");
}

static void toggle_focused_binary_card(void)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];
    const char *focused_entity = ui_focused_card_entity_id();
    const char *service = NULL;

    if (strcmp(focused_entity, MVP_LIGHT_ENTITY_ID) == 0) {
        service = MVP_LIGHT_SERVICE;
    } else if (strcmp(focused_entity, MVP_SWITCH_ENTITY_ID) == 0) {
        service = MVP_SWITCH_SERVICE;
    }

    if (!service) {
        fprintf(stderr,
                "[ha_action] select ignored: focused entity=%s\n",
                focused_entity && *focused_entity ? focused_entity : "<none>");
        return;
    }

    pthread_mutex_lock(&g_action_lock);
    if (g_action_in_flight) {
        pthread_mutex_unlock(&g_action_lock);
        fprintf(stderr, "[ha_action] toggle ignored: action in flight\n");
        return;
    }
    g_action_in_flight = 1;
    pthread_mutex_unlock(&g_action_lock);

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)ha_rest_call_service(base_url,
                               token,
                               service,
                               focused_entity);
    ui_refresh_cards();

    pthread_mutex_lock(&g_action_lock);
    g_action_in_flight = 0;
    pthread_mutex_unlock(&g_action_lock);
}

static void call_cover_service(const char *service, const char *motion)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];
    const char *focused_entity = ui_focused_card_entity_id();

    if (strcmp(focused_entity, MVP_COVER_ENTITY_ID) != 0) {
        fprintf(stderr,
                "[ha_action] cover key ignored: focused entity=%s\n",
                focused_entity && *focused_entity ? focused_entity : "<none>");
        return;
    }

    ui_cover_note_command(MVP_COVER_ENTITY_ID, motion);

    pthread_mutex_lock(&g_action_lock);
    if (g_action_in_flight) {
        pthread_mutex_unlock(&g_action_lock);
        fprintf(stderr, "[ha_action] cover action ignored: action in flight\n");
        return;
    }
    g_action_in_flight = 1;
    pthread_mutex_unlock(&g_action_lock);

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)ha_rest_call_service(base_url, token, service, MVP_COVER_ENTITY_ID);
    ui_refresh_cards();

    pthread_mutex_lock(&g_action_lock);
    g_action_in_flight = 0;
    pthread_mutex_unlock(&g_action_lock);
}

static void close_focused_cover(void)
{
    call_cover_service(MVP_COVER_CLOSE_SERVICE, "closing");
}

static void stop_focused_cover(void)
{
    call_cover_service(MVP_COVER_STOP_SERVICE, "stopped");
}

static void play_pause_focused_media(void)
{
    const char *base_url;
    const char *token;
    char base_url_buf[128];
    char token_buf[256];
    const char *focused_entity = ui_focused_card_entity_id();

    if (strcmp(focused_entity, MVP_MEDIA_ENTITY_ID) != 0) {
        fprintf(stderr,
                "[ha_action] media play/pause ignored: focused entity=%s\n",
                focused_entity && *focused_entity ? focused_entity : "<none>");
        return;
    }

    pthread_mutex_lock(&g_action_lock);
    if (g_action_in_flight) {
        pthread_mutex_unlock(&g_action_lock);
        fprintf(stderr, "[ha_action] media play/pause ignored: action in flight\n");
        return;
    }
    g_action_in_flight = 1;
    pthread_mutex_unlock(&g_action_lock);

    resolve_ha_connection(base_url_buf,
                          sizeof(base_url_buf),
                          token_buf,
                          sizeof(token_buf),
                          &base_url,
                          &token);

    (void)ha_rest_call_service(base_url,
                               token,
                               MVP_MEDIA_PLAY_PAUSE_SERVICE,
                               MVP_MEDIA_ENTITY_ID);
    ui_refresh_cards();

    pthread_mutex_lock(&g_action_lock);
    g_action_in_flight = 0;
    pthread_mutex_unlock(&g_action_lock);
}

static void pause_key_action(void)
{
    const char *focused_entity = ui_focused_card_entity_id();

    if (strcmp(focused_entity, MVP_COVER_ENTITY_ID) == 0) {
        stop_focused_cover();
    } else if (strcmp(focused_entity, MVP_MEDIA_ENTITY_ID) == 0) {
        play_pause_focused_media();
    } else {
        fprintf(stderr,
                "[ha_action] pause key ignored: focused entity=%s\n",
                focused_entity && *focused_entity ? focused_entity : "<none>");
    }
}

static void open_focused_cover(void)
{
    call_cover_service(MVP_COVER_OPEN_SERVICE, "opening");
}

int main(void)
{
    setbuf(stdout, NULL);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ha_ws_seed((uint32_t)(ts.tv_nsec ^ (uint32_t)ts.tv_sec ^ (uint32_t)getpid()));

    int config_loaded = ha_config_load(NULL);

    int fb_w = 0, fb_h = 0;
    if (fb_init(&fb_w, &fb_h) != 0) {
        printf("fb_init failed\n");
        return 1;
    }

    lcd_wake();
    power_manager_init(ms_now());

    if (input_init() != 0) {
        printf("input_init failed\n");
        fb_deinit();
        return 1;
    }

    lv_init();

    static lv_color_t buf1[240 * 40];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, (uint32_t)(240 * 40));

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = fb_w;
    disp_drv.ver_res = fb_h;
    disp_drv.flush_cb = fb_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = indev_encoder_read;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    lv_group_t *grp = lv_group_create();
    lv_group_set_default(grp);
    lv_indev_set_group(indev, grp);

    ui_init(grp);
    ui_set_cover_refresh_callback(fetch_cover_state);
    input_set_key_callbacks(KEY_HOME, ui_toggle_menu, ui_emergency_exit);
    input_set_key_callbacks(ENCODER_PUSH_CODE, toggle_focused_binary_card, NULL);
    input_set_key_callbacks(SB_KEY_REWIND, open_focused_cover, NULL);
    input_set_key_callbacks(SB_KEY_PAUSE, pause_key_action, NULL);
    input_set_key_callbacks(SB_KEY_FASTFORWARD, close_focused_cover, NULL);
    input_set_wheel_callback(ui_menu_wheel);
    input_set_activity_callback(input_activity);

    (void)hal_init();
    (void)audio_feedback_start();
    (void)status_cache_start();

    if (config_loaded) {
        fetch_configured_ha_states();
        ui_refresh_cards();
        start_media_art_service();
        start_ha_state_subscription();
    }

    lv_timer_create(ha_poll_timer_cb, 100, NULL);

    uint64_t last = ms_now();
    while (!ui_should_exit()) {
        uint64_t now = ms_now();
        uint32_t diff = (uint32_t)(now - last);
        last = now;
        power_manager_tick(now);
        input_pump_events();
        if (power_manager_is_sleeping()) {
            usleep(100000);
            continue;
        }
        lv_tick_inc(diff);
        lv_timer_handler();
        usleep(5000);
    }

    ha_session_close();
    media_art_stop();
    status_cache_stop();
    audio_feedback_stop();
    input_deinit();
    ui_show_exit_screen();
    fb_deinit();
    stockui_restart_via_pidfile();
    hal_shutdown();
    return 0;
}
