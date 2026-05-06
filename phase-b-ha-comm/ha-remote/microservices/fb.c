#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "lvgl/src/misc/lv_txt.h"

#define FB_TEXT_STRIP_COUNT 4
#define FB_TEXT_STRIP_MAX_W 4096
#define FB_TEXT_STRIP_DELAY_MS 5000
#define FB_TEXT_STRIP_STEP_MS 50
#define FB_TEXT_STRIP_STEP_PX 3
#define FB_TEXT_STRIP_TEXT_MAX 256

struct fb_text_strip {
    int enabled;
    int dirty;
    int x, y, w, h;
    int text_w;
    int offset_x;
    int active;
    uint64_t pause_until_ms;
    uint64_t last_step_ms;
    lv_color_t fg;
    lv_color_t bg;
    lv_color_t *pixels;
    char text[FB_TEXT_STRIP_TEXT_MAX];
};

static uint8_t *g_fb0 = NULL;
static uint8_t *g_fb1 = NULL;
static uint8_t *g_fb_map = NULL;
static size_t   g_fb_map_len = 0;
static int      g_fb_fd = -1;
static int      g_line_len = 0;
static int      g_w = 0, g_h = 0;
static pthread_mutex_t g_fb_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_text_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_text_thread;
static int g_text_thread_running = 0;
static int g_text_thread_started = 0;
static int g_text_strips_blocked = 0;
static int g_text_occluder_active = 0;
static int g_text_occluder_x1 = 0;
static int g_text_occluder_y1 = 0;
static int g_text_occluder_x2 = -1;
static int g_text_occluder_y2 = -1;
static struct fb_text_strip g_text_strips[FB_TEXT_STRIP_COUNT];

static uint64_t fb_ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int area_intersects(int x1, int y1, int x2, int y2,
                           int rx1, int ry1, int rx2, int ry2)
{
    return x1 <= rx2 && x2 >= rx1 && y1 <= ry2 && y2 >= ry1;
}

static int text_occluder_contains_locked(int x, int y)
{
    return g_text_occluder_active &&
           x >= g_text_occluder_x1 && x <= g_text_occluder_x2 &&
           y >= g_text_occluder_y1 && y <= g_text_occluder_y2;
}

static void draw_strip_to_fb(uint8_t *fb, const struct fb_text_strip *s)
{
    int row;
    int col;

    if(!fb || !s || !s->enabled) return;

    for(row = 0; row < s->h; row++) {
        int sy = s->y + row;
        uint8_t *dst;

        if(sy < 0 || sy >= g_h) continue;
        if(s->x >= g_w || s->x + s->w <= 0) continue;

        for(col = 0; col < s->w; col++) {
            int sx = s->x + col;
            int src_x = col - s->offset_x;
            lv_color_t color = s->bg;

            if(sx < 0 || sx >= g_w) continue;
            if(text_occluder_contains_locked(sx, sy)) continue;
            if(!g_text_strips_blocked && s->pixels && src_x >= 0 && src_x < s->text_w) {
                color = s->pixels[(size_t)row * (size_t)s->text_w + (size_t)src_x];
            }
            dst = fb + (size_t)sy * (size_t)g_line_len + (size_t)sx * 2;
            memcpy(dst, &color.full, 2);
        }
    }
}

static lv_color_t text_strip_color_at_locked(int x, int y, int *hit)
{
    for(unsigned int i = 0; i < FB_TEXT_STRIP_COUNT; i++) {
        const struct fb_text_strip *s = &g_text_strips[i];
        int local_x;
        int local_y;
        int src_x;

        if(!s->enabled) continue;
        if(x < s->x || x >= s->x + s->w || y < s->y || y >= s->y + s->h) continue;
        if(text_occluder_contains_locked(x, y)) continue;

        local_x = x - s->x;
        local_y = y - s->y;
        src_x = local_x - s->offset_x;
        *hit = 1;
        if(!g_text_strips_blocked && s->pixels && src_x >= 0 && src_x < s->text_w) {
            return s->pixels[(size_t)local_y * (size_t)s->text_w + (size_t)src_x];
        }
        return s->bg;
    }

    *hit = 0;
    return lv_color_black();
}

static void compose_text_strips_locked(const lv_area_t *area)
{
    unsigned int i;

    pthread_mutex_lock(&g_text_lock);
    for(i = 0; i < FB_TEXT_STRIP_COUNT; i++) {
        struct fb_text_strip *s = &g_text_strips[i];

        if(!s->enabled) continue;
        if(area && !area_intersects(s->x,
                                    s->y,
                                    s->x + s->w - 1,
                                    s->y + s->h - 1,
                                    area->x1,
                                    area->y1,
                                    area->x2,
                                    area->y2)) {
            continue;
        }
        draw_strip_to_fb(g_fb0, s);
        draw_strip_to_fb(g_fb1, s);
        s->dirty = 0;
    }
    pthread_mutex_unlock(&g_text_lock);
}

static uint32_t glyph_bitmap_bits(const uint8_t *map, uint32_t bit_pos, uint8_t bpp)
{
    uint32_t v = 0;
    uint8_t i;

    for(i = 0; i < bpp; i++) {
        uint32_t bit = bit_pos + i;
        v = (v << 1) | ((map[bit >> 3] >> (7 - (bit & 7))) & 1U);
    }

    return v;
}

static uint8_t glyph_alpha(uint32_t value, uint8_t bpp)
{
    uint32_t max_v;

    if(value == 0) return 0;
    if(bpp == 8) return (uint8_t)value;
    max_v = (1U << bpp) - 1U;
    return (uint8_t)((value * 255U) / max_v);
}

static void render_glyph_to_strip(lv_color_t *dst,
                                  int dst_w,
                                  int dst_h,
                                  int pen_x,
                                  int line_y,
                                  const lv_font_t *font,
                                  uint32_t letter,
                                  uint32_t letter_next,
                                  lv_color_t fg)
{
    lv_font_glyph_dsc_t gdsc;
    const uint8_t *map;
    int gx;
    int gy;
    int row;
    int col;
    uint8_t bpp;

    if(!dst || !font) return;
    if(!lv_font_get_glyph_dsc(font, &gdsc, letter, letter_next)) return;
    if(gdsc.box_w == 0 || gdsc.box_h == 0) return;

    map = lv_font_get_glyph_bitmap(gdsc.resolved_font, letter);
    if(!map) return;

    bpp = gdsc.bpp == 3 ? 4 : gdsc.bpp;
    if(bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) return;

    gx = pen_x + gdsc.ofs_x;
    gy = line_y + (font->line_height - font->base_line) - gdsc.box_h - gdsc.ofs_y;

    for(row = 0; row < gdsc.box_h; row++) {
        int dy = gy + row;

        if(dy < 0 || dy >= dst_h) continue;
        for(col = 0; col < gdsc.box_w; col++) {
            int dx = gx + col;
            uint32_t bit_pos;
            uint32_t value;
            uint8_t alpha;

            if(dx < 0 || dx >= dst_w) continue;
            bit_pos = (uint32_t)(row * gdsc.box_w + col) * (uint32_t)bpp;
            value = glyph_bitmap_bits(map, bit_pos, bpp);
            alpha = glyph_alpha(value, bpp);
            if(alpha == 0) continue;
            dst[(size_t)dy * (size_t)dst_w + (size_t)dx] =
                lv_color_mix(fg, dst[(size_t)dy * (size_t)dst_w + (size_t)dx], alpha);
        }
    }
}

static lv_color_t *render_text_strip(const lv_font_t *font,
                                     const char *text,
                                     int strip_w,
                                     int strip_h,
                                     lv_color_t fg,
                                     lv_color_t bg)
{
    lv_color_t *pixels;
    uint32_t ofs = 0;
    int pen_x = 0;
    int line_y;
    size_t count;

    if(strip_w <= 0 || strip_h <= 0) return NULL;
    count = (size_t)strip_w * (size_t)strip_h;
    pixels = malloc(count * sizeof(*pixels));
    if(!pixels) return NULL;

    for(size_t i = 0; i < count; i++) {
        pixels[i] = bg;
    }

    line_y = font && font->line_height < strip_h ? (strip_h - font->line_height) / 2 : 0;

    while(font && text && text[ofs] != '\0') {
        uint32_t next_ofs;
        uint32_t letter;
        uint32_t letter_next;
        lv_font_glyph_dsc_t gdsc;

        letter = _lv_txt_encoded_next(text, &ofs);
        if(letter == 0) break;
        next_ofs = ofs;
        letter_next = text[next_ofs] ? _lv_txt_encoded_next(text, &next_ofs) : 0;
        render_glyph_to_strip(pixels, strip_w, strip_h, pen_x, line_y, font, letter, letter_next, fg);
        if(lv_font_get_glyph_dsc(font, &gdsc, letter, letter_next)) {
            pen_x += gdsc.adv_w;
        }
    }

    return pixels;
}

static void *text_strip_thread_main(void *arg)
{
    (void)arg;

    while(g_text_thread_running) {
        uint64_t now = fb_ms_now();
        int have_draw = 0;

        pthread_mutex_lock(&g_fb_lock);
        pthread_mutex_lock(&g_text_lock);
        for(unsigned int i = 0; i < FB_TEXT_STRIP_COUNT; i++) {
            struct fb_text_strip *s = &g_text_strips[i];

            if(g_text_strips_blocked || !s->enabled) continue;
            if(s->active) {
                if(s->pause_until_ms != 0 && now >= s->pause_until_ms) {
                    s->pause_until_ms = 0;
                    s->last_step_ms = now;
                }
                if(s->pause_until_ms == 0) {
                    if(s->last_step_ms == 0 || now < s->last_step_ms) {
                        s->last_step_ms = now;
                    }
                    while(now - s->last_step_ms >= FB_TEXT_STRIP_STEP_MS) {
                        int old_x = s->offset_x;
                        int x = old_x - FB_TEXT_STRIP_STEP_PX;

                        if(x < -s->text_w) {
                            x = s->w;
                        } else if(old_x > 0 && x <= 0) {
                            x = 0;
                            s->pause_until_ms = now + FB_TEXT_STRIP_DELAY_MS;
                        }
                        s->offset_x = x;
                        s->last_step_ms += FB_TEXT_STRIP_STEP_MS;
                        s->dirty = 1;
                        if(s->pause_until_ms != 0) break;
                    }
                }
            }
            if(s->dirty) {
                draw_strip_to_fb(g_fb0, s);
                draw_strip_to_fb(g_fb1, s);
                s->dirty = 0;
                have_draw = 1;
            }
        }
        pthread_mutex_unlock(&g_text_lock);
        pthread_mutex_unlock(&g_fb_lock);

        (void)have_draw;
        usleep(20000);
    }

    return NULL;
}

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

    g_text_thread_running = 1;
    if (pthread_create(&g_text_thread, NULL, text_strip_thread_main, NULL) == 0) {
        g_text_thread_started = 1;
    } else {
        g_text_thread_running = 0;
        printf("text strip thread start failed\n");
    }

    return 0;
}

void fb_deinit(void)
{
    g_text_thread_running = 0;
    if (g_text_thread_started) {
        pthread_join(g_text_thread, NULL);
        g_text_thread_started = 0;
    }

    pthread_mutex_lock(&g_text_lock);
    for (unsigned int i = 0; i < FB_TEXT_STRIP_COUNT; i++) {
        free(g_text_strips[i].pixels);
        g_text_strips[i].pixels = NULL;
        g_text_strips[i].enabled = 0;
    }
    pthread_mutex_unlock(&g_text_lock);

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

    pthread_mutex_lock(&g_fb_lock);
    pthread_mutex_lock(&g_text_lock);
    for (int32_t y = y1; y <= y2; y++) {
        uint8_t *dst0 = g_fb0 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;
        uint8_t *dst1 = g_fb1 + (size_t)y * (size_t)g_line_len + (size_t)x1 * 2;
        memcpy(dst0, src, (size_t)copy_w * 2);
        memcpy(dst1, src, (size_t)copy_w * 2);
        for(int32_t x = x1; x <= x2; x++) {
            int hit;
            lv_color_t c = text_strip_color_at_locked(x, y, &hit);
            if(hit) {
                size_t off = (size_t)(x - x1) * 2;
                memcpy(dst0 + off, &c.full, 2);
                memcpy(dst1 + off, &c.full, 2);
            }
        }
        src += (size_t)full_w * 2;
    }
    for(unsigned int i = 0; i < FB_TEXT_STRIP_COUNT; i++) {
        g_text_strips[i].dirty = 0;
    }
    pthread_mutex_unlock(&g_text_lock);
    pthread_mutex_unlock(&g_fb_lock);

    lv_disp_flush_ready(drv);
}

int fb_text_strip_set(unsigned int slot,
                      int x,
                      int y,
                      int w,
                      int h,
                      const lv_font_t *font,
                      const char *text,
                      uint32_t fg_hex,
                      uint32_t bg_hex)
{
    struct fb_text_strip tmp;
    struct fb_text_strip *s;
    lv_color_t *old_pixels = NULL;
    lv_point_t size;

    if(slot >= FB_TEXT_STRIP_COUNT || w <= 0 || h <= 0) return -1;
    if(!text) text = "";
    if(w > g_w) w = g_w;

    memset(&tmp, 0, sizeof(tmp));
    tmp.enabled = 1;
    tmp.dirty = 1;
    tmp.x = x;
    tmp.y = y;
    tmp.w = w;
    tmp.h = h;
    tmp.fg = lv_color_hex(fg_hex);
    tmp.bg = lv_color_hex(bg_hex);
    snprintf(tmp.text, sizeof(tmp.text), "%s", text);

    pthread_mutex_lock(&g_text_lock);
    s = &g_text_strips[slot];
    if(s->enabled &&
       s->x == tmp.x &&
       s->y == tmp.y &&
       s->w == tmp.w &&
       s->h == tmp.h &&
       s->fg.full == tmp.fg.full &&
       s->bg.full == tmp.bg.full &&
       strcmp(s->text, tmp.text) == 0) {
        pthread_mutex_unlock(&g_text_lock);
        return 0;
    }
    pthread_mutex_unlock(&g_text_lock);

    if(font && *text) {
        lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        tmp.text_w = size.x > 0 ? size.x : 0;
        if(tmp.text_w > FB_TEXT_STRIP_MAX_W) tmp.text_w = FB_TEXT_STRIP_MAX_W;
    }

    if(tmp.text_w > 0) {
        tmp.pixels = render_text_strip(font, tmp.text, tmp.text_w, tmp.h, tmp.fg, tmp.bg);
    }
    tmp.active = tmp.text_w > tmp.w;
    tmp.offset_x = 0;
    tmp.pause_until_ms = tmp.active ? fb_ms_now() + FB_TEXT_STRIP_DELAY_MS : 0;
    tmp.last_step_ms = 0;

    pthread_mutex_lock(&g_text_lock);
    s = &g_text_strips[slot];
    old_pixels = s->pixels;
    *s = tmp;
    tmp.pixels = NULL;
    pthread_mutex_unlock(&g_text_lock);

    free(old_pixels);
    return 0;
}

void fb_text_strip_disable(unsigned int slot)
{
    int old_blocked;

    if(slot >= FB_TEXT_STRIP_COUNT) return;

    pthread_mutex_lock(&g_fb_lock);
    pthread_mutex_lock(&g_text_lock);
    if(g_text_strips[slot].enabled) {
        old_blocked = g_text_strips_blocked;
        g_text_strips_blocked = 1;
        draw_strip_to_fb(g_fb0, &g_text_strips[slot]);
        draw_strip_to_fb(g_fb1, &g_text_strips[slot]);
        g_text_strips_blocked = old_blocked;
        g_text_strips[slot].enabled = 0;
        g_text_strips[slot].dirty = 0;
    }
    pthread_mutex_unlock(&g_text_lock);
    pthread_mutex_unlock(&g_fb_lock);
}

void fb_text_strip_set_blocked(int blocked)
{
    unsigned int i;

    pthread_mutex_lock(&g_fb_lock);
    pthread_mutex_lock(&g_text_lock);
    g_text_strips_blocked = blocked ? 1 : 0;
    for(i = 0; i < FB_TEXT_STRIP_COUNT; i++) {
        struct fb_text_strip *s = &g_text_strips[i];
        if(!s->enabled) continue;
        draw_strip_to_fb(g_fb0, s);
        draw_strip_to_fb(g_fb1, s);
        s->dirty = 0;
    }
    pthread_mutex_unlock(&g_text_lock);
    pthread_mutex_unlock(&g_fb_lock);
}

void fb_text_strip_set_occluder(int active, int x, int y, int w, int h)
{
    pthread_mutex_lock(&g_text_lock);
    if(active && w > 0 && h > 0) {
        g_text_occluder_active = 1;
        g_text_occluder_x1 = x;
        g_text_occluder_y1 = y;
        g_text_occluder_x2 = x + w - 1;
        g_text_occluder_y2 = y + h - 1;
    } else {
        g_text_occluder_active = 0;
        g_text_occluder_x1 = 0;
        g_text_occluder_y1 = 0;
        g_text_occluder_x2 = -1;
        g_text_occluder_y2 = -1;
    }
    pthread_mutex_unlock(&g_text_lock);
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
