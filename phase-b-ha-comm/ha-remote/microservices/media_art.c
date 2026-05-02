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
#include "src/extra/libs/sjpg/tjpgd.h"

#define MEDIA_ART_TMP_PATH "/tmp/ha_album_art.tmp"
#define MEDIA_ART_PATH "/tmp/ha_album_art.jpg"
#define MEDIA_ART_LVGL_PATH "S:/tmp/ha_album_art.jpg"
#define MEDIA_ART_W 240
#define MEDIA_ART_H 204
#define MEDIA_ART_SLEEP_US 500000
#define MEDIA_ART_BUF_SIZE 2048
#define MEDIA_ART_HEADER_SIZE 4096
#define MEDIA_ART_JPEG_WORKBUF_SIZE 4096

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

typedef struct {
    FILE *fp;
    uint8_t *rgb;
    int width;
    int height;
} jpeg_decode_t;

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
    FILE *fp;
    uint8_t *work;
    uint8_t *rgb = NULL;
    jpeg_decode_t ctx;
    JDEC jd;
    JRESULT rc;

    if(!path || !out) return 0;
    fp = fopen(path, "rb");
    if(!fp) return 0;

    work = (uint8_t *)malloc(MEDIA_ART_JPEG_WORKBUF_SIZE);
    if(!work) {
        fclose(fp);
        return 0;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.fp = fp;
    rc = jd_prepare(&jd, jpeg_input, work, MEDIA_ART_JPEG_WORKBUF_SIZE, &ctx);
    if(rc != JDR_OK || jd.width <= 0 || jd.height <= 0) {
        fprintf(stderr, "[media_art] jpeg prepare failed rc=%d\n", (int)rc);
        free(work);
        fclose(fp);
        return 0;
    }

    ctx.width = jd.width;
    ctx.height = jd.height;
    rgb = (uint8_t *)malloc((size_t)ctx.width * (size_t)ctx.height * 3U);
    if(!rgb) {
        free(work);
        fclose(fp);
        return 0;
    }
    memset(rgb, 0, (size_t)ctx.width * (size_t)ctx.height * 3U);
    ctx.rgb = rgb;

    fseek(fp, 0, SEEK_SET);
    rc = jd_prepare(&jd, jpeg_input, work, MEDIA_ART_JPEG_WORKBUF_SIZE, &ctx);
    if(rc == JDR_OK) rc = jd_decomp(&jd, jpeg_output, 0);
    if(rc != JDR_OK) {
        fprintf(stderr, "[media_art] jpeg decode failed rc=%d\n", (int)rc);
        free(rgb);
        free(work);
        fclose(fp);
        return 0;
    }

    for(int y = 0; y < MEDIA_ART_H; y++) {
        int sy = (y * ctx.width) / MEDIA_ART_W;
        if(sy < 0) sy = 0;
        if(sy >= ctx.height) sy = ctx.height - 1;
        for(int x = 0; x < MEDIA_ART_W; x++) {
            int sx = (x * ctx.width) / MEDIA_ART_W;
            uint8_t *p;
            uint16_t c;
            if(sx < 0) sx = 0;
            if(sx >= ctx.width) sx = ctx.width - 1;
            p = rgb + ((size_t)sy * (size_t)ctx.width + (size_t)sx) * 3U;
            c = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
            out[(size_t)y * MEDIA_ART_W + (size_t)x] = c;
        }
    }

    fprintf(stderr, "[media_art] decoded album art %dx%d -> %dx%d\n",
            ctx.width, ctx.height, MEDIA_ART_W, MEDIA_ART_H);
    free(rgb);
    free(work);
    fclose(fp);
    return 1;
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

static void publish_art_path(void)
{
    pthread_mutex_lock(&g_lock);
    snprintf(g_path, sizeof(g_path), "%s", "decoded");
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
        if(!media_state_loaded(state) || !title || !*title || !picture || !*picture) {
            last_picture[0] = '\0';
            clear_art_path();
        } else if(strcmp(picture, last_picture) != 0 && parse_base_url(base_url, &url)) {
            if(fetch_to_file(&url, token, picture)) {
                uint16_t *decoded = (uint16_t *)malloc(sizeof(g_pixels));
                if(decoded && decode_jpeg_to_rgb565(MEDIA_ART_PATH, decoded)) {
                    pthread_mutex_lock(&g_lock);
                    memcpy(g_pixels, decoded, sizeof(g_pixels));
                    pthread_mutex_unlock(&g_lock);
                    snprintf(last_picture, sizeof(last_picture), "%s", picture);
                    publish_art_path();
                    fprintf(stderr, "[media_art] fetched album art\n");
                } else {
                    fprintf(stderr, "[media_art] album art decode failed\n");
                }
                free(decoded);
            } else {
                fprintf(stderr, "[media_art] album art fetch failed\n");
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
