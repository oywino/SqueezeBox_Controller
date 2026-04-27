#include <stdint.h>
#include <stdio.h>
#include <linux/input.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "input.h"
#include "ha_ws.h"
#include "ui.h"
#include "stockui.h"
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

int main(void)
{
    setbuf(stdout, NULL);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ha_ws_seed((uint32_t)(ts.tv_nsec ^ (uint32_t)ts.tv_sec ^ (uint32_t)getpid()));

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
    input_set_activity_callback(input_activity);

    (void)hal_init();
    (void)status_cache_start();

    lv_timer_create(ha_poll_timer_cb, 100, NULL);

    uint64_t last = ms_now();
    while (!ui_should_exit()) {
        uint64_t now = ms_now();
        uint32_t diff = (uint32_t)(now - last);
        last = now;
        lv_tick_inc(diff);
        power_manager_tick(now);
        lv_timer_handler();
        usleep(5000);
    }

    ha_session_close();
    status_cache_stop();
    input_deinit();
    ui_show_exit_screen();
    fb_deinit();
    stockui_restart_via_pidfile();
    hal_shutdown();
    return 0;
}
