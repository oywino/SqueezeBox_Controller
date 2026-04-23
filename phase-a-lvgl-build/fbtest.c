// fbtest.c - Squeezebox Controller fb0 test (RGB565), ARMv5, static-musl friendly
// Draws: red -> green -> blue (full screen), then RGB vertical bars for 10s.

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static void msleep(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, 0);
}

static void fill565(uint8_t *fb, int line_len, int w, int h, uint16_t px) {
  for (int y = 0; y < h; y++) {
    uint16_t *row = (uint16_t *)(fb + (size_t)y * (size_t)line_len);
    for (int x = 0; x < w; x++) row[x] = px;
  }
}

static void bars(uint8_t *fb, int line_len, int w, int h) {
  int third = w / 3;
  for (int y = 0; y < h; y++) {
    uint16_t *row = (uint16_t *)(fb + (size_t)y * (size_t)line_len);
    for (int x = 0; x < w; x++) {
      uint16_t c;
      if (x < third) c = 0xF800;           // Red
      else if (x < 2 * third) c = 0x07E0;  // Green
      else c = 0x001F;                      // Blue
      row[x] = c;
    }
  }
}

int main(void) {
  int fd = open("/dev/fb0", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "open(/dev/fb0) failed: %s\n", strerror(errno));
    return 1;
  }

  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
    fprintf(stderr, "FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
    close(fd);
    return 1;
  }
  if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
    fprintf(stderr, "FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  int w = (int)vinfo.xres;
  int h = (int)vinfo.yres;
  int line_len = (int)finfo.line_length;
  size_t map_len = (size_t)line_len * (size_t)vinfo.yres_virtual;

  printf("fb0: %dx%d, virt %dx%d, bpp=%d, line_len=%d, yoffset=%d, map_len=%lu\n",
         vinfo.xres, vinfo.yres,
         vinfo.xres_virtual, vinfo.yres_virtual,
         vinfo.bits_per_pixel, finfo.line_length, vinfo.yoffset,
         (unsigned long)map_len);

  if (vinfo.bits_per_pixel != 16) {
    fprintf(stderr, "Expected 16bpp, got %d\n", vinfo.bits_per_pixel);
    close(fd);
    return 1;
  }

  uint8_t *fb = (uint8_t *)mmap(0, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (fb == MAP_FAILED) {
    fprintf(stderr, "mmap failed: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  // Respect current yoffset (double-buffer setups)
  uint8_t *vis = fb + (size_t)line_len * (size_t)vinfo.yoffset;

  fill565(vis, line_len, w, h, 0xF800); msleep(800); // red
  fill565(vis, line_len, w, h, 0x07E0); msleep(800); // green
  fill565(vis, line_len, w, h, 0x001F); msleep(800); // blue
  bars(vis, line_len, w, h);                           // RGB bars
  msleep(10000);

  munmap(fb, map_len);
  close(fd);
  return 0;
}
