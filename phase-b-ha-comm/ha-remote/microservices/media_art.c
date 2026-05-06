#include "media_art.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ha_rest.h"
#include "ws_io.h"
#include "src/extra/libs/png/lodepng.h"
#include "src/extra/libs/sjpg/tjpgd.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#include "third_party/stb_image.h"

#define MEDIA_ART_TMP_PATH "/tmp/ha_album_art.tmp"
#define MEDIA_ART_PATH "/tmp/ha_album_art.jpg"
#define MEDIA_ART_LVGL_PATH "S:/tmp/ha_album_art.jpg"
#define MEDIA_ART_W 240
#define MEDIA_ART_H 204
#define MEDIA_ART_SLEEP_US 500000
#define MEDIA_ART_BUF_SIZE 2048
#define MEDIA_ART_HEADER_SIZE 4096
#define MEDIA_ART_JPEG_WORKBUF_SIZE 4096
#define MEDIA_ART_FALLBACK_PATH "fallback"
#define MEDIA_ART_DECODED_PATH "decoded"

typedef struct {
    char host[96];
    char port[8];
} media_art_url_t;

static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_started = 0;
static int g_running = 0;
static char g_base_url[128];
static char g_token[256];
static char g_entity_id[64];
static char g_path[64];
static uint16_t g_pixels[MEDIA_ART_W * MEDIA_ART_H];
static lv_img_dsc_t g_image = {
    .header.always_zero = 0,
    .header.w = MEDIA_ART_W,
    .header.h = MEDIA_ART_H,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data_size = MEDIA_ART_W * MEDIA_ART_H * 2,
    .data = (const uint8_t *)g_pixels
};
static unsigned long g_version = 0;
static int g_fallback_ready = 0;
static uint16_t g_fallback_pixels[MEDIA_ART_W * MEDIA_ART_H];

typedef struct {
    FILE *fp;
    uint8_t *rgb;
    int width;
    int height;
} jpeg_decode_t;

static void scale_crop_rgb_to_rgb565(const uint8_t *rgb, int src_w, int src_h, uint16_t *out)
{
    int crop_x = 0;
    int crop_y = 0;
    int crop_w = src_w;
    int crop_h = (src_w * MEDIA_ART_H) / MEDIA_ART_W;

    if(crop_h <= 0) crop_h = src_h;
    if(crop_h <= src_h) {
        crop_y = (src_h - crop_h) / 2;
    } else {
        crop_h = src_h;
        crop_w = (src_h * MEDIA_ART_W) / MEDIA_ART_H;
        if(crop_w <= 0 || crop_w > src_w) crop_w = src_w;
        crop_x = (src_w - crop_w) / 2;
    }

    for(int y = 0; y < MEDIA_ART_H; y++) {
        int sy = crop_y + (y * crop_h) / MEDIA_ART_H;
        if(sy < 0) sy = 0;
        if(sy >= src_h) sy = src_h - 1;
        for(int x = 0; x < MEDIA_ART_W; x++) {
            int sx = crop_x + (x * crop_w) / MEDIA_ART_W;
            const uint8_t *p;
            uint16_t c;
            if(sx < 0) sx = 0;
            if(sx >= src_w) sx = src_w - 1;
            p = rgb + ((size_t)sy * (size_t)src_w + (size_t)sx) * 3U;
            c = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
            out[(size_t)y * MEDIA_ART_W + (size_t)x] = c;
        }
    }
}

static int file_signature(const char *path, uint8_t *sig, size_t sig_size)
{
    FILE *fp;
    size_t got;

    if(!path || !sig || sig_size == 0) return 0;
    fp = fopen(path, "rb");
    if(!fp) return 0;
    got = fread(sig, 1, sig_size, fp);
    fclose(fp);
    return got == sig_size;
}

static int file_is_png(const char *path)
{
    uint8_t sig[8];
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    return file_signature(path, sig, sizeof(sig)) && memcmp(sig, png_sig, sizeof(sig)) == 0;
}

static int file_is_jpeg(const char *path)
{
    uint8_t sig[3];
    return file_signature(path, sig, sizeof(sig)) && sig[0] == 0xff && sig[1] == 0xd8 && sig[2] == 0xff;
}

static int read_whole_file(const char *path, uint8_t **out, size_t *out_size)
{
    FILE *fp;
    long size;
    uint8_t *buf;
    size_t got;

    if(!path || !out || !out_size) return 0;
    *out = NULL;
    *out_size = 0;

    fp = fopen(path, "rb");
    if(!fp) return 0;
    if(fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if(size <= 0) {
        fclose(fp);
        return 0;
    }
    if(fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    buf = (uint8_t *)malloc((size_t)size);
    if(!buf) {
        fclose(fp);
        return 0;
    }
    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if(got != (size_t)size) {
        free(buf);
        return 0;
    }

    *out = buf;
    *out_size = got;
    return 1;
}

static int parse_base_url(const char *base_url, media_art_url_t *out)
{
    const char *p;
    const char *host_start;
    const char *host_end;
    const char *port_start = NULL;
    size_t len;

    if(!base_url || !out || strncmp(base_url, "http://", 7) != 0) return 0;
    memset(out, 0, sizeof(*out));

    p = base_url + 7;
    host_start = p;
    while(*p && *p != ':' && *p != '/') p++;
    host_end = p;

    if(*p == ':') {
        p++;
        port_start = p;
        while(*p && *p != '/') p++;
    }

    len = (size_t)(host_end - host_start);
    if(len == 0 || len >= sizeof(out->host)) return 0;
    memcpy(out->host, host_start, len);
    out->host[len] = '\0';

    if(port_start) {
        len = (size_t)(p - port_start);
        if(len == 0 || len >= sizeof(out->port)) return 0;
        memcpy(out->port, port_start, len);
        out->port[len] = '\0';
    } else {
        snprintf(out->port, sizeof(out->port), "%s", "8123");
    }
    return 1;
}

static void fallback_pixel(uint16_t *out, int x, int y, uint16_t color)
{
    if(!out || x < 0 || y < 0 || x >= MEDIA_ART_W || y >= MEDIA_ART_H) return;
    out[(size_t)y * MEDIA_ART_W + (size_t)x] = color;
}

static void fallback_disc(uint16_t *out, int cx, int cy, int r, uint16_t color)
{
    int r2 = r * r;
    for(int y = cy - r; y <= cy + r; y++) {
        for(int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if(dx * dx + dy * dy <= r2) fallback_pixel(out, x, y, color);
        }
    }
}

static void fallback_line(uint16_t *out, int x0, int y0, int x1, int y1, int w, uint16_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if(steps <= 0) {
        fallback_disc(out, x0, y0, w / 2, color);
        return;
    }
    for(int i = 0; i <= steps; i++) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;
        fallback_disc(out, x, y, w / 2, color);
    }
}

static void fallback_arc(uint16_t *out, int cx, int cy, int r, int thick, int gap, uint16_t color)
{
    int inner = r - thick;
    int outer = r + thick;
    int inner2 = inner * inner;
    int outer2 = outer * outer;

    for(int y = cy - outer; y <= cy + outer; y++) {
        for(int x = cx - outer; x <= cx + outer; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if(d2 < inner2 || d2 > outer2) continue;
            if(dy > 0 && abs(dx) * 100 < dy * gap) continue;
            fallback_pixel(out, x, y, color);
        }
    }
}

static void render_fallback_art(uint16_t *out)
{
    uint16_t grey = 0x528A;

    if(!out) return;
    memset(out, 0, MEDIA_ART_W * MEDIA_ART_H * sizeof(uint16_t));

    fallback_arc(out, 112, 79, 68, 2, 34, grey);
    fallback_arc(out, 112, 79, 49, 2, 45, grey);
    fallback_arc(out, 112, 79, 30, 2, 60, grey);
    fallback_disc(out, 112, 63, 9, grey);

    fallback_line(out, 112, 88, 79, 184, 10, grey);
    fallback_line(out, 112, 88, 146, 184, 10, grey);
    fallback_line(out, 112, 125, 112, 203, 9, grey);
    fallback_line(out, 97, 143, 128, 154, 8, grey);
    fallback_line(out, 90, 166, 137, 181, 8, grey);
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while(off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if(n <= 0) return 0;
        off += (size_t)n;
    }
    return 1;
}

static int header_value_int(const char *headers, const char *name)
{
    const char *p = headers;
    size_t name_len = strlen(name);

    while(p && *p) {
        const char *line_end = strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if(line_len > name_len && p[name_len] == ':') {
            size_t i;
            int match = 1;
            for(i = 0; i < name_len; i++) {
                if(tolower((unsigned char)p[i]) != tolower((unsigned char)name[i])) {
                    match = 0;
                    break;
                }
            }
            if(match) {
                const char *v = p + name_len + 1;
                while(*v == ' ' || *v == '\t') v++;
                return atoi(v);
            }
        }
        if(!line_end) break;
        p = line_end + 2;
    }
    return -1;
}

static int fetch_to_file(const media_art_url_t *url, const char *token, const char *path)
{
    int fd;
    FILE *fp;
    char req[1024];
    char buf[MEDIA_ART_BUF_SIZE];
    char headers[MEDIA_ART_HEADER_SIZE];
    size_t header_used = 0;
    int have_headers = 0;
    int content_len = -1;
    int written = 0;
    int n;

    if(!url || !path || !*path) return 0;

    fd = ws_tcp_connect(url->host, url->port);
    if(fd < 0) return 0;

    n = snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s:%s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Accept: image/jpeg,image/*\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 path,
                 url->host,
                 url->port,
                 token ? token : "");
    if(n <= 0 || n >= (int)sizeof(req) || !send_all(fd, req, (size_t)n)) {
        close(fd);
        return 0;
    }

    fp = fopen(MEDIA_ART_TMP_PATH, "wb");
    if(!fp) {
        close(fd);
        return 0;
    }

    while(1) {
        int ready = sock_wait(fd, 1, have_headers ? 1000 : 4000);
        ssize_t got;

        if(ready < 0) {
            fclose(fp);
            close(fd);
            unlink(MEDIA_ART_TMP_PATH);
            return 0;
        }
        if(ready == 0) {
            break;
        }

        got = recv(fd, buf, sizeof(buf), 0);
        if(got < 0) {
            fclose(fp);
            close(fd);
            unlink(MEDIA_ART_TMP_PATH);
            return 0;
        }
        if(got == 0) break;

        if(!have_headers) {
            size_t copy = (size_t)got;
            if(header_used + copy >= sizeof(headers)) copy = sizeof(headers) - header_used - 1;
            memcpy(headers + header_used, buf, copy);
            header_used += copy;
            headers[header_used] = '\0';

            char *body = strstr(headers, "\r\n\r\n");
            if(body) {
                size_t header_len = (size_t)(body - headers) + 4;
                size_t body_len = header_used - header_len;

                if(strncmp(headers, "HTTP/1.1 200", 12) != 0 &&
                   strncmp(headers, "HTTP/1.0 200", 12) != 0) {
                    fclose(fp);
                    close(fd);
                    unlink(MEDIA_ART_TMP_PATH);
                    return 0;
                }

                content_len = header_value_int(headers, "content-length");
                if(body_len > 0) {
                    fwrite(headers + header_len, 1, body_len, fp);
                    written += (int)body_len;
                }
                have_headers = 1;
            }
        } else {
            fwrite(buf, 1, (size_t)got, fp);
            written += (int)got;
        }

        if(have_headers && content_len >= 0 && written >= content_len) {
            break;
        }
    }

    fclose(fp);
    close(fd);

    if(!have_headers || written <= 0) {
        unlink(MEDIA_ART_TMP_PATH);
        return 0;
    }

    if(rename(MEDIA_ART_TMP_PATH, MEDIA_ART_PATH) != 0) {
        unlink(MEDIA_ART_TMP_PATH);
        return 0;
    }
    return 1;
}

static size_t jpeg_input(JDEC *jd, uint8_t *buff, size_t ndata)
{
    jpeg_decode_t *ctx = (jpeg_decode_t *)jd->device;
    if(!ctx || !ctx->fp) return 0;
    if(!buff) {
        return fseek(ctx->fp, (long)ndata, SEEK_CUR) == 0 ? ndata : 0;
    }
    return fread(buff, 1, ndata, ctx->fp);
}

static int jpeg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_decode_t *ctx = (jpeg_decode_t *)jd->device;
    uint8_t *src = (uint8_t *)bitmap;
    int row_w;

    if(!ctx || !ctx->rgb || !rect) return 0;
    row_w = rect->right - rect->left + 1;
    for(int y = rect->top; y <= rect->bottom; y++) {
        size_t dst_off = ((size_t)y * (size_t)ctx->width + (size_t)rect->left) * 3U;
        memcpy(ctx->rgb + dst_off, src, (size_t)row_w * 3U);
        src += (size_t)row_w * 3U;
    }
    return 1;
}

static int decode_jpeg_to_rgb565(const char *path, uint16_t *out)
{
    uint8_t *file_data = NULL;
    size_t file_size = 0;
    uint8_t *rgb = NULL;
    int width = 0;
    int height = 0;
    int channels = 0;

    if(!path || !out) return 0;

    if(!read_whole_file(path, &file_data, &file_size)) {
        fprintf(stderr, "[media_art] jpeg read failed\n");
        return 0;
    }

    rgb = stbi_load_from_memory(file_data, (int)file_size, &width, &height, &channels, 3);
    free(file_data);
    if(!rgb) {
        fprintf(stderr, "[media_art] jpeg decode failed\n");
        return 0;
    }

    if(width <= 0 || height <= 0) {
        stbi_image_free(rgb);
        return 0;
    }

    scale_crop_rgb_to_rgb565(rgb, width, height, out);

    fprintf(stderr, "[media_art] decoded album art %dx%d -> %dx%d\n",
            width, height, MEDIA_ART_W, MEDIA_ART_H);
    stbi_image_free(rgb);
    return 1;
}

static int decode_png_to_rgb565(const char *path, uint16_t *out)
{
    uint8_t *file_data = NULL;
    size_t file_size = 0;
    unsigned char *rgba = NULL;
    uint8_t *rgb = NULL;
    unsigned width = 0;
    unsigned height = 0;
    unsigned error;

    if(!path || !out) return 0;

    if(!read_whole_file(path, &file_data, &file_size)) {
        fprintf(stderr, "[media_art] png read failed\n");
        return 0;
    }

    error = lodepng_decode32(&rgba, &width, &height, file_data, file_size);
    free(file_data);
    if(error || !rgba || width == 0 || height == 0) {
        fprintf(stderr, "[media_art] png decode failed rc=%u\n", error);
        free(rgba);
        return 0;
    }

    rgb = (uint8_t *)malloc((size_t)width * (size_t)height * 3U);
    if(!rgb) {
        free(rgba);
        return 0;
    }

    for(unsigned y = 0; y < height; y++) {
        for(unsigned x = 0; x < width; x++) {
            size_t src = ((size_t)y * (size_t)width + (size_t)x) * 4U;
            size_t dst = ((size_t)y * (size_t)width + (size_t)x) * 3U;
            unsigned a = rgba[src + 3];
            rgb[dst + 0] = (uint8_t)((rgba[src + 0] * a) / 255U);
            rgb[dst + 1] = (uint8_t)((rgba[src + 1] * a) / 255U);
            rgb[dst + 2] = (uint8_t)((rgba[src + 2] * a) / 255U);
        }
    }

    scale_crop_rgb_to_rgb565(rgb, (int)width, (int)height, out);
    fprintf(stderr, "[media_art] decoded png album art %ux%u -> %dx%d\n",
            width, height, MEDIA_ART_W, MEDIA_ART_H);

    free(rgb);
    free(rgba);
    return 1;
}

static int decode_image_to_rgb565(const char *path, uint16_t *out)
{
    if(file_is_jpeg(path)) return decode_jpeg_to_rgb565(path, out);
    if(file_is_png(path)) return decode_png_to_rgb565(path, out);

    fprintf(stderr, "[media_art] unsupported album art format\n");
    return 0;
}

static void clear_art_path(void)
{
    pthread_mutex_lock(&g_lock);
    if(g_path[0]) {
        g_path[0] = '\0';
        g_version++;
    }
    pthread_mutex_unlock(&g_lock);
}

static void publish_fallback_art(void)
{
    pthread_mutex_lock(&g_lock);
    if(!g_fallback_ready) {
        render_fallback_art(g_fallback_pixels);
        g_fallback_ready = 1;
    }
    if(strcmp(g_path, MEDIA_ART_FALLBACK_PATH) != 0) {
        memcpy(g_pixels, g_fallback_pixels, sizeof(g_pixels));
        snprintf(g_path, sizeof(g_path), "%s", MEDIA_ART_FALLBACK_PATH);
        g_version++;
    }
    pthread_mutex_unlock(&g_lock);
}

static void publish_art_path(void)
{
    pthread_mutex_lock(&g_lock);
    snprintf(g_path, sizeof(g_path), "%s", MEDIA_ART_DECODED_PATH);
    g_version++;
    pthread_mutex_unlock(&g_lock);
}

static int media_state_loaded(const char *state)
{
    if(!state || !*state) return 0;
    if(strcmp(state, "idle") == 0) return 0;
    if(strcmp(state, "off") == 0) return 0;
    if(strcmp(state, "unavailable") == 0) return 0;
    if(strcmp(state, "unknown") == 0) return 0;
    return 1;
}

static void *worker_main(void *arg)
{
    (void)arg;

    char last_picture[256] = "";

    while(1) {
        media_art_url_t url;
        char base_url[128];
        char token[256];
        char entity_id[64];
        const char *picture;
        const char *title;
        const char *state;
        int running;

        pthread_mutex_lock(&g_lock);
        running = g_running;
        snprintf(base_url, sizeof(base_url), "%s", g_base_url);
        snprintf(token, sizeof(token), "%s", g_token);
        snprintf(entity_id, sizeof(entity_id), "%s", g_entity_id);
        pthread_mutex_unlock(&g_lock);

        if(!running) break;

        title = ha_rest_get_cached_media_title(entity_id);
        state = ha_rest_get_cached_state(entity_id);
        picture = ha_rest_get_cached_media_picture(entity_id);
        if(!media_state_loaded(state) || !title || !*title) {
            last_picture[0] = '\0';
            clear_art_path();
        } else if(!picture || !*picture) {
            last_picture[0] = '\0';
            publish_fallback_art();
        } else if(strcmp(picture, last_picture) != 0 && parse_base_url(base_url, &url)) {
            if(fetch_to_file(&url, token, picture)) {
                uint16_t *decoded = (uint16_t *)malloc(sizeof(g_pixels));
                if(decoded && decode_image_to_rgb565(MEDIA_ART_PATH, decoded)) {
                    pthread_mutex_lock(&g_lock);
                    memcpy(g_pixels, decoded, sizeof(g_pixels));
                    pthread_mutex_unlock(&g_lock);
                    snprintf(last_picture, sizeof(last_picture), "%s", picture);
                    publish_art_path();
                    fprintf(stderr, "[media_art] fetched album art\n");
                } else {
                    fprintf(stderr, "[media_art] album art decode failed\n");
                    snprintf(last_picture, sizeof(last_picture), "%s", picture);
                    publish_fallback_art();
                }
                free(decoded);
            } else {
                fprintf(stderr, "[media_art] album art fetch failed\n");
                publish_fallback_art();
            }
        }

        usleep(MEDIA_ART_SLEEP_US);
    }

    return NULL;
}

int media_art_start(const char *base_url, const char *token, const char *entity_id)
{
    pthread_mutex_lock(&g_lock);
    if(g_started) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    snprintf(g_base_url, sizeof(g_base_url), "%s", base_url ? base_url : "");
    snprintf(g_token, sizeof(g_token), "%s", token ? token : "");
    snprintf(g_entity_id, sizeof(g_entity_id), "%s", entity_id ? entity_id : "");
    g_path[0] = '\0';
    g_version = 0;
    g_running = 1;
    pthread_mutex_unlock(&g_lock);

    if(pthread_create(&g_thread, NULL, worker_main, NULL) != 0) {
        pthread_mutex_lock(&g_lock);
        g_running = 0;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    pthread_mutex_lock(&g_lock);
    g_started = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void media_art_stop(void)
{
    int started;

    pthread_mutex_lock(&g_lock);
    started = g_started;
    g_running = 0;
    pthread_mutex_unlock(&g_lock);

    if(started) pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_lock);
    g_started = 0;
    pthread_mutex_unlock(&g_lock);
}

unsigned long media_art_get_path(char *out, size_t out_size)
{
    unsigned long version;

    pthread_mutex_lock(&g_lock);
    version = g_version;
    if(out && out_size > 0) {
        snprintf(out, out_size, "%s", g_path);
    }
    pthread_mutex_unlock(&g_lock);
    return version;
}

const lv_img_dsc_t *media_art_get_image(unsigned long *version)
{
    const lv_img_dsc_t *image = NULL;

    pthread_mutex_lock(&g_lock);
    if(version) *version = g_version;
    if(g_path[0]) image = &g_image;
    pthread_mutex_unlock(&g_lock);
    return image;
}
