#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
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

static void fetch_configured_ha_states(void)
{
    const char *ha_host = getenv("HA_HOST");
    const char *ha_token_env = getenv("HA_TOKEN");
    const char *base_url = ha_config_get_ha_base_url();
    const char *token = ha_config_get_ha_access_token();
    char base_url_buf[128];
    char token_buf[256];

    if (ha_host && *ha_host) {
        snprintf(base_url_buf, sizeof(base_url_buf), "http://%s:8123", ha_host);
        base_url = base_url_buf;
    }

    if (ha_token_env && *ha_token_env) {
        token = ha_token_env;
    } else if (token_is_placeholder(token)) {
        if (read_token_file("HA_LL_Token.txt", token_buf, sizeof(token_buf)) ||
            read_token_file("/mnt/storage/phase-a-lvgl/HA_LL_Token.txt",
                            token_buf,
                            sizeof(token_buf))) {
            token = token_buf;
        }
    }

    (void)ha_rest_fetch_configured_states(base_url, token);
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
    input_set_key_callbacks(KEY_HOME, ui_toggle_menu, ui_emergency_exit);
    input_set_wheel_callback(ui_menu_wheel);
    input_set_activity_callback(input_activity);

    (void)hal_init();
    (void)audio_feedback_start();
    (void)status_cache_start();

    if (config_loaded) {
        fetch_configured_ha_states();
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
    status_cache_stop();
    audio_feedback_stop();
    input_deinit();
    ui_show_exit_screen();
    fb_deinit();
    stockui_restart_via_pidfile();
    hal_shutdown();
    return 0;
}
