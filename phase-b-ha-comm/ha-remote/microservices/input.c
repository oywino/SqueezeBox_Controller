// microservices/input.c — HAL-driven LVGL adapter (C99)

#include <stdint.h>
#include <linux/input.h>
#include <time.h>

#include "lvgl.h"
#include "hal.h"
#include "input.h"
#include "ha_ws.h"   // ha_session_note_activity(void)

/* Encoder state consumed by LVGL */
static volatile int g_enc_diff    = 0;
static volatile int g_btn_pressed = 0;
static int (*g_activity_cb)(void) = 0;

#define INPUT_LONG_PRESS_MS 1000
#define INPUT_MAX_KEY_BINDINGS 12

struct key_binding {
    int key_code;
    int pressed;
    int long_fired;
    int64_t down_ms;
    void (*short_cb)(void);
    void (*long_cb)(void);
};

static struct key_binding g_key_bindings[INPUT_MAX_KEY_BINDINGS];

static int64_t mono_ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

/* Delegate lifecycle to HAL */
int input_init(void)   { return hal_init(); }
void input_deinit(void){ hal_shutdown(); }

void input_set_key_callbacks(int key_code, void (*short_press)(void), void (*long_press)(void)) {
    int free_slot = -1;

    for (int i = 0; i < INPUT_MAX_KEY_BINDINGS; ++i) {
        if (g_key_bindings[i].key_code == key_code) {
            g_key_bindings[i].short_cb = short_press;
            g_key_bindings[i].long_cb = long_press;
            return;
        }
        if (free_slot < 0 && g_key_bindings[i].key_code == 0) {
            free_slot = i;
        }
    }

    if (free_slot >= 0) {
        g_key_bindings[free_slot].key_code = key_code;
        g_key_bindings[free_slot].short_cb = short_press;
        g_key_bindings[free_slot].long_cb = long_press;
    }
}

void input_set_activity_callback(int (*activity)(void)) {
    g_activity_cb = activity;
}

static struct key_binding *find_key_binding(int key_code) {
    for (int i = 0; i < INPUT_MAX_KEY_BINDINGS; ++i) {
        if (g_key_bindings[i].key_code == key_code) return &g_key_bindings[i];
    }
    return 0;
}

static void input_check_key_longpress(void) {
    int64_t now = mono_ms_now();

    for (int i = 0; i < INPUT_MAX_KEY_BINDINGS; ++i) {
        struct key_binding *binding = &g_key_bindings[i];

        if (binding->key_code == 0 || !binding->pressed || binding->long_fired) continue;

        if ((now - binding->down_ms) >= INPUT_LONG_PRESS_MS) {
            binding->long_fired = 1;
            if (binding->long_cb) binding->long_cb();
        }
    }
}

static int dispatch_key_binding(const struct hal_input_event *ev) {
    struct key_binding *binding = find_key_binding(ev->code);

    if (!binding) return 0;

    if (ev->value == 1) {
        binding->pressed = 1;
        binding->long_fired = 0;
        binding->down_ms = ev->ts_ms ? ev->ts_ms : mono_ms_now();
    } else if (ev->value == 0) {
        if (binding->pressed && !binding->long_fired && binding->short_cb) {
            binding->short_cb();
        }
        binding->pressed = 0;
        binding->long_fired = 0;
    }

    return 1;
}

/* Dispatch one HAL event -> LVGL-facing state + session activity stub */
static inline void dispatch_hal_event(const struct hal_input_event *ev) {
    if (!ev) return;

    if (g_activity_cb && g_activity_cb()) return;

    if (ev->type == EV_REL) {
#ifdef REL_WHEEL
        if (ev->code == REL_WHEEL) g_enc_diff += ev->value;
#endif
#ifdef REL_DIAL
        if (ev->code == REL_DIAL)  g_enc_diff += ev->value;
#endif
    } else if (ev->type == EV_KEY) {
        if (!dispatch_key_binding(ev)) {
            /* Existing behavior: non-Home keys drive encoder push semantics. */
            g_btn_pressed = (ev->value != 0);
        }
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
    input_check_key_longpress();

    int diff = g_enc_diff;
    g_enc_diff = 0;

    data->enc_diff = diff;
    data->state    = g_btn_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
