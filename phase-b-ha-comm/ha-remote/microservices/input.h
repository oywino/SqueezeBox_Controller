#ifndef HA_REMOTE_INPUT_H
#define HA_REMOTE_INPUT_H

/* Ensure LVGL picks up the project root lv_conf.h via "lv_conf.h" */
#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE 1
#endif

#include "lvgl/lvgl.h"

/* Owns input device fds (wheel/keys/gyro), O_NONBLOCK, EVIOCGRAB lifecycle. */
int  input_init(void);     /* returns 0 on success */
void input_deinit(void);

/* LVGL indev read callback (encoder+button). */
void indev_encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

#endif
