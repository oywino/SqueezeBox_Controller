#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "fb.h"

static uint8_t *g_fb0 = NULL;
static uint8_t *g_fb1 = NULL;
static uint8_t *g_fb_map = NULL;
static size_t   g_fb_map_len = 0;
static int      g_fb_fd = -1;
static int      g_line_len = 0;
static int      g_w = 0, g_h = 0;

int fb_init(int *out_w, int *out_h)
{
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        printf("open(/dev/fb0) failed: %s\n", strerror(errno));
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
        printf("FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        close(g_fb_fd);
        g_fb_fd = -1;
        return -1;
    }

    if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
        printf("FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
        close(g_fb_fd);
        g_fb_fd = -1;
        return -1;
    }

    g_w = (int)vinfo.xres;
    g_h = (int)vinfo.yres;
    g_line_len = (int)finfo.line_length;

    g_fb_map_len = (size_t)g_line_len * (size_t)vinfo.yres_virtual;
    g_fb_map = mmap(NULL, g_fb_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_map == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        g_fb_map = NULL;
        g_fb_map_len = 0;
        close(g_fb_fd);
        g_fb_fd = -1;
        return -1;
    }

    g_fb0 = g_fb_map + (size_t)g_line_len * 0;
    g_fb1 = g_fb_map + (size_t)g_line_len * (size_t)g_h;

    printf("fb_init: %dx%d bpp=%d line_len=%d yoffset=%d\n",
           vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length, vinfo.yoffset);

    if (out_w) *out_w = g_w;
    if (out_h) *out_h = g_h;

    return 0;
}

void fb_deinit(void)
{
    if (g_fb_map && g_fb_map_len) {
        munmap(g_fb_map, g_fb_map_len);
    }
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
    }

    g_fb0 = g_fb1 = g_fb_map = NULL;
    g_fb_fd = -1;
    g_fb_map_len = 0;
    g_line_len = 0;
    g_w = g_h = 0;
}

void fb_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)drv;

    int32_t x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t x2 = area->x2 >= g_w ? (g_w - 1) : area->x2;
    int32_t y2 = area->y2 >= g_h ? (g_h - 1) : area->y2;

    int32_t copy_w = x2 - x1 + 1;
    if (copy_w <= 0 || y2 < y1) {
        lv_disp_flush_ready(drv);
        return;
    }

    const uint8_t *src = (const uint8_t *)color_p;
    int32_t full_w = (area->x2 - area->x1 + 1);

    if (x1 != area->x1 || y1 != area->y1) {
        src += (size_t)(y1 - area->y1) * (size_t)full_w * 2 +
               (size_t)(x1 - area->x1) * 2;
    }

    for (int32_t y = y1; y <= y2; y++) {
        uint8_t *dst0 = g_fb0 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;
        uint8_t *dst1 = g_fb1 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;
        memcpy(dst0, src, (size_t)copy_w * 2);
        memcpy(dst1, src, (size_t)copy_w * 2);
        src += (size_t)full_w * 2;
    }

    lv_disp_flush_ready(drv);
}

void lcd_wake(void)
{
    FILE *fp = fopen("/sys/class/lcd/ili9320/power", "w");
    if (fp) {
        fputs("0\n", fp);
        fclose(fp);
    }
    if (g_fb_fd >= 0) {
        (void)ioctl(g_fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    }
    (void)system("/usr/bin/jivectl 11 >/dev/null 2>&1");
}

void lcd_sleep(void)
{
    if (g_fb_fd >= 0) {
        (void)ioctl(g_fb_fd, FBIOBLANK, FB_BLANK_NORMAL);
    }
    FILE *fp = fopen("/sys/class/lcd/ili9320/power", "w");
    if (fp) {
        fputs("4\n", fp);
        fclose(fp);
    }
}
