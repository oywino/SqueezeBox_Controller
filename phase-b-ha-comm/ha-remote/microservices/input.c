// microservices/input.c — HAL-driven LVGL adapter (C99)

#include <stdint.h>
#include <linux/input.h>

#include "lvgl.h"
#include "hal.h"
#include "input.h"
#include "ha_ws.h"   // ha_session_note_activity(void)

/* Encoder state consumed by LVGL */
static volatile int g_enc_diff    = 0;
static volatile int g_btn_pressed = 0;

/* Delegate lifecycle to HAL */
int input_init(void)   { return hal_init(); }
void input_deinit(void){ hal_shutdown(); }

/* Dispatch one HAL event -> LVGL-facing state + session activity stub */
static inline void dispatch_hal_event(const struct hal_input_event *ev) {
    if (!ev) return;

    if (ev->type == EV_REL) {
#ifdef REL_WHEEL
        if (ev->code == REL_WHEEL) g_enc_diff += ev->value;
#endif
#ifdef REL_DIAL
        if (ev->code == REL_DIAL)  g_enc_diff += ev->value;
#endif
    } else if (ev->type == EV_KEY) {
        /* Any key press/release drives encoder push semantics */
        g_btn_pressed = (ev->value != 0);
    }
    /* EV_ABS (accel) does not change LVGL encoder state */

    /* Session keepalive (stub signature is void) */
    ha_session_note_activity();
}

/* LVGL v8 read callback: drain HAL events (non-blocking) and report state */
void indev_encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;

    struct hal_input_event ev;
    for (;;) {
        int rc = hal_poll_input(&ev, 0);   /* non-blocking drain */
        if (rc <= 0) break;                /* 0: none, <0: error */
        dispatch_hal_event(&ev);
    }

    int diff = g_enc_diff;
    g_enc_diff = 0;

    data->enc_diff = diff;
    data->state    = g_btn_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
