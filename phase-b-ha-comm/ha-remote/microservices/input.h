#ifndef HA_REMOTE_INPUT_H
#define HA_REMOTE_INPUT_H

/* Ensure LVGL picks up the project root lv_conf.h via "lv_conf.h" */
#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE 1
#endif

#include <stdint.h>

#include "lvgl/lvgl.h"

/* Owns input device fds (wheel/keys/gyro), O_NONBLOCK, EVIOCGRAB lifecycle. */
int  input_init(void);     /* returns 0 on success */
void input_deinit(void);

/* Key callbacks: short on press, long after the hold threshold while still pressed. */
void input_set_key_callbacks(int key_code, void (*short_press)(void), void (*long_press)(void));

/* Wheel callback. Return nonzero to consume wheel input before LVGL encoder fallback. */
void input_set_wheel_callback(int (*wheel)(int diff));

/* Called on every input event. Return nonzero to consume the event. */
void input_set_activity_callback(int (*activity)(void));

/* Drain queued Linux input events from the UI/main loop. */
void input_pump_events(void);

/* Temporary latency instrumentation: call after the LVGL cycle completes. */
void input_note_lvgl_cycle_complete(uint64_t now_ms);

/* LVGL indev read callback (encoder+button). */
void indev_encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

#endif
