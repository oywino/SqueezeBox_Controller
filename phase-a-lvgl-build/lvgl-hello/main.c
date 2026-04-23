#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lvgl/lvgl.h"

static uint8_t *g_fb0 = NULL;
static uint8_t *g_fb1 = NULL;
static int g_line_len = 0;
static int g_w = 0, g_h = 0;

/* Input: event1 Wheel (REL), event2 Matrix (KEY) */
static int g_fd_wheel = -1;
static int g_fd_keys  = -1;
static int32_t g_enc_diff = 0;
static int g_btn_pressed = 0;

static uint64_t ms_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void fb_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  (void)drv;

  int32_t x1 = area->x1 < 0 ? 0 : area->x1;
  int32_t y1 = area->y1 < 0 ? 0 : area->y1;
  int32_t x2 = area->x2 >= g_w ? (g_w - 1) : area->x2;
  int32_t y2 = area->y2 >= g_h ? (g_h - 1) : area->y2;

  int32_t copy_w = x2 - x1 + 1;
  if(copy_w <= 0 || y2 < y1) {
    lv_disp_flush_ready(drv);
    return;
  }

  const uint8_t *src = (const uint8_t *)color_p;

  int32_t full_w = (area->x2 - area->x1 + 1);
  if(x1 != area->x1 || y1 != area->y1) {
    src += (size_t)(y1 - area->y1) * (size_t)full_w * 2 + (size_t)(x1 - area->x1) * 2;
  }

  for(int32_t y = y1; y <= y2; y++) {
    uint8_t *dst0 = g_fb0 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;
    uint8_t *dst1 = g_fb1 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;
    memcpy(dst0, src, (size_t)copy_w * 2);
    memcpy(dst1, src, (size_t)copy_w * 2);
    src += (size_t)full_w * 2;
  }

  lv_disp_flush_ready(drv);
}

static void drain_input(int fd) {
  if(fd < 0) return;
  for(;;) {
    struct input_event ev;
    ssize_t n = read(fd, &ev, sizeof(ev));
    if(n < 0) {
      if(errno == EAGAIN || errno == EWOULDBLOCK) break;
      break;
    }
    if(n != sizeof(ev)) break;

    if(ev.type == EV_REL && ev.code == REL_WHEEL) {
      g_enc_diff += (int32_t)ev.value;
    } else if(ev.type == EV_KEY) {
      if(ev.value == 1) g_btn_pressed = 1;       /* press */
      else if(ev.value == 0) g_btn_pressed = 0;  /* release */
    }
  }
}

static void indev_encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  drain_input(g_fd_wheel);
  drain_input(g_fd_keys);
  data->enc_diff = g_enc_diff;
  g_enc_diff = 0;
  data->state = g_btn_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void keep_visible(lv_timer_t *t) {
  (void)t;
  lv_obj_invalidate(lv_scr_act());
}

int main(void) {
  setbuf(stdout, NULL);

  g_fd_wheel = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
  g_fd_keys  = open("/dev/input/event2", O_RDONLY | O_NONBLOCK);
  printf("input: wheel_fd=%d keys_fd=%d\n", g_fd_wheel, g_fd_keys);
  if(g_fd_wheel >= 0) ioctl(g_fd_wheel, EVIOCGRAB, 1);
  if(g_fd_keys  >= 0) ioctl(g_fd_keys,  EVIOCGRAB, 1);

  int fd = open("/dev/fb0", O_RDWR);
  if(fd < 0) {
    printf("open(/dev/fb0) failed: %s\n", strerror(errno));
    return 1;
  }

  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  if(ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
    printf("FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
    close(fd);
    return 1;
  }
  if(ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
    printf("FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  g_w = (int)vinfo.xres;
  g_h = (int)vinfo.yres;
  g_line_len = (int)finfo.line_length;

  size_t map_len = (size_t)g_line_len * (size_t)vinfo.yres_virtual;
  uint8_t *fb = (uint8_t *)mmap(0, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(fb == MAP_FAILED) {
    printf("mmap failed: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  g_fb0 = fb + (size_t)g_line_len * 0;

  g_fb1 = fb + (size_t)g_line_len * (size_t)g_h;

  printf("LVGL fb0: %dx%d, bpp=%d, line_len=%d, yoffset=%d\n",
         vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length, vinfo.yoffset);

  lv_init();

  static lv_color_t buf1[240 * 40];
  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, (uint32_t)(240 * 40));

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = g_w;
  disp_drv.ver_res = g_h;
  disp_drv.flush_cb = fb_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_ENCODER;
  indev_drv.read_cb = indev_encoder_read;
  lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

  lv_group_t *grp = lv_group_create();
  lv_indev_set_group(indev, grp);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "Wheel = focus\nAny key = press");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *btn1 = lv_btn_create(scr);
  lv_obj_set_size(btn1, 160, 46);
  lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -20);
  lv_group_add_obj(grp, btn1);
  lv_obj_t *t1 = lv_label_create(btn1);
  lv_label_set_text(t1, "Button A");
  lv_obj_center(t1);

  lv_obj_t *btn2 = lv_btn_create(scr);
  lv_obj_set_size(btn2, 160, 46);
  lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 40);
  lv_group_add_obj(grp, btn2);
  lv_obj_t *t2 = lv_label_create(btn2);
  lv_label_set_text(t2, "Button B");
  lv_obj_center(t2);

  lv_group_focus_obj(btn1);

  lv_timer_create(keep_visible, 30, NULL);

  uint64_t last = ms_now();
  while(1) {
    uint64_t now = ms_now();
    uint32_t diff = (uint32_t)(now - last);
    last = now;
    lv_tick_inc(diff);
    lv_timer_handler();
    usleep(5000);
  }

  return 0;
}
