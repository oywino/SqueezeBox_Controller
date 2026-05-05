// microservices/input.c — HAL-driven LVGL adapter (C99)

#include <stdint.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include "lvgl.h"
#include "audio_feedback.h"
#include "hal.h"
#include "input.h"
#include "ha_ws.h"   // ha_session_note_activity(void)

/* Encoder state consumed by LVGL */
static volatile int g_enc_diff    = 0;
static volatile int g_btn_pressed = 0;
static int (*g_activity_cb)(void) = 0;
static int (*g_wheel_cb)(int diff) = 0;

#define INPUT_LONG_PRESS_MS 1000
#define INPUT_MAX_KEY_BINDINGS 12
#define INPUT_EVENT_QUEUE_LEN 32

struct key_binding {
    int key_code;
    int pressed;
    int long_fired;
    int short_on_down;
    int64_t down_ms;
    void (*short_cb)(void);
    void (*long_cb)(void);
};

static struct key_binding g_key_bindings[INPUT_MAX_KEY_BINDINGS];

static pthread_t g_input_thread;
static pthread_mutex_t g_event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_wheel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_latency_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_input_thread_running = 0;
static int g_input_thread_started = 0;
static struct hal_input_event g_event_queue[INPUT_EVENT_QUEUE_LEN];
static unsigned int g_event_head = 0;
static unsigned int g_event_tail = 0;
static int g_pending_menu_wheel_delta = 0;
static int64_t g_latency_wheel_first_ms = 0;
static int g_latency_wheel_count = 0;
static int g_latency_waiting_for_lvgl = 0;

static int64_t mono_ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static int event_queue_empty(void) {
    return g_event_head == g_event_tail;
}

static int event_queue_full(void) {
    return ((g_event_tail + 1U) % INPUT_EVENT_QUEUE_LEN) == g_event_head;
}

static int wheel_delta_from_event(const struct hal_input_event *ev) {
    int diff = 0;

    if (!ev || ev->type != EV_REL) return 0;
#ifdef REL_WHEEL
    if (ev->code == REL_WHEEL) diff = ev->value;
#endif
#ifdef REL_DIAL
    if (ev->code == REL_DIAL)  diff = ev->value;
#endif
    return diff;
}

static void latency_note_wheel_read(const struct hal_input_event *ev) {
    int diff = wheel_delta_from_event(ev);

    if (diff == 0) return;

    pthread_mutex_lock(&g_latency_lock);
    if (g_latency_wheel_first_ms == 0) {
        g_latency_wheel_first_ms = ev->ts_ms ? ev->ts_ms : mono_ms_now();
    }
    g_latency_wheel_count += diff > 0 ? diff : -diff;
    pthread_mutex_unlock(&g_latency_lock);
}

static void latency_note_wheel_flushed(void) {
    pthread_mutex_lock(&g_latency_lock);
    if (g_latency_wheel_first_ms != 0) {
        g_latency_waiting_for_lvgl = 1;
    }
    pthread_mutex_unlock(&g_latency_lock);
}

static void event_queue_push(const struct hal_input_event *ev) {
    pthread_mutex_lock(&g_event_lock);
    if (event_queue_full()) {
        g_event_head = (g_event_head + 1U) % INPUT_EVENT_QUEUE_LEN;
    }
    g_event_queue[g_event_tail] = *ev;
    g_event_tail = (g_event_tail + 1U) % INPUT_EVENT_QUEUE_LEN;
    pthread_mutex_unlock(&g_event_lock);
}

static void pending_wheel_add(int diff) {
    pthread_mutex_lock(&g_wheel_lock);
    g_pending_menu_wheel_delta += diff;
    pthread_mutex_unlock(&g_wheel_lock);
}

static int pending_wheel_take(void) {
    int diff;

    pthread_mutex_lock(&g_wheel_lock);
    diff = g_pending_menu_wheel_delta;
    g_pending_menu_wheel_delta = 0;
    pthread_mutex_unlock(&g_wheel_lock);
    return diff;
}

static int event_queue_pop(struct hal_input_event *ev) {
    int have = 0;
    pthread_mutex_lock(&g_event_lock);
    if (!event_queue_empty()) {
        *ev = g_event_queue[g_event_head];
        g_event_head = (g_event_head + 1U) % INPUT_EVENT_QUEUE_LEN;
        have = 1;
    }
    pthread_mutex_unlock(&g_event_lock);
    return have;
}

static void *input_thread_main(void *arg) {
    (void)arg;

    while (g_input_thread_running) {
        struct hal_input_event ev;
        int rc = hal_poll_input(&ev, 100);
        if (rc > 0) {
            int wheel_diff = wheel_delta_from_event(&ev);
            if (wheel_diff != 0 && g_wheel_cb) {
                latency_note_wheel_read(&ev);
                pending_wheel_add(wheel_diff);
            } else {
                event_queue_push(&ev);
            }
        }
    }

    return NULL;
}

/* Delegate device lifecycle to HAL; input thread owns blocking input waits. */
int input_init(void) {
    int rc = hal_init();
    if (rc != 0) return rc;

    pthread_mutex_lock(&g_event_lock);
    g_event_head = 0;
    g_event_tail = 0;
    g_input_thread_running = 1;
    pthread_mutex_unlock(&g_event_lock);

    if (pthread_create(&g_input_thread, NULL, input_thread_main, NULL) != 0) {
        pthread_mutex_lock(&g_event_lock);
        g_input_thread_running = 0;
        pthread_mutex_unlock(&g_event_lock);
        hal_shutdown();
        return -1;
    }

    g_input_thread_started = 1;
    return 0;
}

void input_deinit(void) {
    pthread_mutex_lock(&g_event_lock);
    g_input_thread_running = 0;
    pthread_mutex_unlock(&g_event_lock);

    if (g_input_thread_started) {
        pthread_join(g_input_thread, NULL);
        g_input_thread_started = 0;
    }

    hal_shutdown();
}

void input_set_key_callbacks(int key_code, void (*short_press)(void), void (*long_press)(void)) {
    int free_slot = -1;

    for (int i = 0; i < INPUT_MAX_KEY_BINDINGS; ++i) {
        if (g_key_bindings[i].key_code == key_code) {
            g_key_bindings[i].short_cb = short_press;
            g_key_bindings[i].long_cb = long_press;
            g_key_bindings[i].short_on_down = 1;
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
        g_key_bindings[free_slot].short_on_down = 1;
    }
}

void input_set_activity_callback(int (*activity)(void)) {
    g_activity_cb = activity;
}

void input_set_wheel_callback(int (*wheel)(int diff)) {
    g_wheel_cb = wheel;
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
        audio_feedback_play(AUDIO_FEEDBACK_PRESS);
        if (binding->short_on_down && binding->short_cb) {
            binding->short_cb();
        }
    } else if (ev->value == 0) {
        if (!binding->short_on_down && binding->pressed && !binding->long_fired && binding->short_cb) {
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
        int diff = 0;
#ifdef REL_WHEEL
        if (ev->code == REL_WHEEL) diff = ev->value;
#endif
#ifdef REL_DIAL
        if (ev->code == REL_DIAL)  diff = ev->value;
#endif
        if (diff != 0) {
            int step = diff > 0 ? 1 : -1;
            if (g_wheel_cb && g_wheel_cb(step)) {
                return;
            }
            g_enc_diff += step;
        }
    } else if (ev->type == EV_KEY) {
        if (!dispatch_key_binding(ev)) {
            /* Existing behavior: non-Home keys drive encoder push semantics. */
            if (ev->value == 1) audio_feedback_play(AUDIO_FEEDBACK_PRESS);
            g_btn_pressed = (ev->value != 0);
        }
    }
    /* EV_ABS (accel) does not change LVGL encoder state */

    /* Session keepalive (stub signature is void) */
    ha_session_note_activity();
}

void input_pump_events(void) {
    struct hal_input_event ev;
    int wheel_delta = pending_wheel_take();

    while (event_queue_pop(&ev)) {
        dispatch_hal_event(&ev);
    }

    if (wheel_delta != 0) {
        struct hal_input_event wheel_ev;
        wheel_ev.source = HAL_INPUT_SRC_WHEEL;
        wheel_ev.type = EV_REL;
#ifdef REL_WHEEL
        wheel_ev.code = REL_WHEEL;
#else
        wheel_ev.code = 0;
#endif
        wheel_ev.value = wheel_delta;
        wheel_ev.ts_ms = mono_ms_now();
        dispatch_hal_event(&wheel_ev);
        latency_note_wheel_flushed();
    }
    input_check_key_longpress();
}

void input_note_lvgl_cycle_complete(uint64_t now_ms) {
    int64_t first_ms;
    int count;
    int waiting;

    pthread_mutex_lock(&g_latency_lock);
    first_ms = g_latency_wheel_first_ms;
    count = g_latency_wheel_count;
    waiting = g_latency_waiting_for_lvgl;
    if (waiting && first_ms != 0) {
        g_latency_wheel_first_ms = 0;
        g_latency_wheel_count = 0;
        g_latency_waiting_for_lvgl = 0;
    }
    pthread_mutex_unlock(&g_latency_lock);

    if (waiting && first_ms != 0) {
        fprintf(stderr,
                "[wheel_latency] detents=%d latency_ms=%llu\n",
                count,
                (unsigned long long)(now_ms - (uint64_t)first_ms));
    }
}

/* LVGL v8 read callback: report already-dispatched encoder/button state. */
void indev_encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;

    int diff = g_enc_diff;
    g_enc_diff = 0;

    data->enc_diff = diff;
    data->state    = g_btn_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
