#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "ha_config.h"
#include "ha_rest.h"
#include "ws_io.h"

typedef struct {
    char entity_id[64];
    char state[HA_REST_MAX_STATE];
    char media_title[96];
    char media_artist[96];
    char media_album[96];
    char media_picture[256];
    int media_position;
    int media_duration;
    int have_media_position;
    int have_media_duration;
    int position;
    int have_position;
    unsigned long version;
    int valid;
} ha_rest_cached_state_t;

typedef struct {
    char host[96];
    char port[8];
} ha_rest_url_t;

static ha_rest_cached_state_t g_states[HA_CONFIG_MAX_TRACKED_ENTITIES];
static size_t g_state_count = 0;

static int ha_rest_parse_base_url(const char *base_url, ha_rest_url_t *out)
{
    const char *p;
    const char *host_start;
    const char *host_end;
    const char *port_start = NULL;
    size_t host_len;
    size_t port_len;

    if (!base_url || !*base_url || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));

    if (strncmp(base_url, "http://", 7) != 0) {
        fprintf(stderr,
                "[ha_rest] unsupported base_url (only http:// is supported): %s\n",
                base_url);
        return 0;
    }

    p = base_url + 7;
    host_start = p;
    while (*p && *p != ':' && *p != '/') {
        p++;
    }
    host_end = p;

    if (*p == ':') {
        p++;
        port_start = p;
        while (*p && *p != '/') {
            p++;
        }
    }

    host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
        return 0;
    }
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    if (port_start) {
        port_len = (size_t)(p - port_start);
        if (port_len == 0 || port_len >= sizeof(out->port)) {
            return 0;
        }
        memcpy(out->port, port_start, port_len);
        out->port[port_len] = '\0';
    } else {
        snprintf(out->port, sizeof(out->port), "%s", "8123");
    }

    return 1;
}

static int ha_rest_send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n <= 0) {
            return 0;
        }
        off += (size_t)n;
    }
    return 1;
}

static int ha_rest_read_response(int fd, char *out, size_t out_size)
{
    size_t used = 0;

    if (!out || out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    while (used + 1 < out_size) {
        int ready = sock_wait(fd, 1, 2500);
        ssize_t n;

        if (ready < 0) {
            return 0;
        }
        if (ready == 0) {
            break;
        }

        n = recv(fd, out + used, out_size - 1 - used, 0);
        if (n < 0) {
            return 0;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
        out[used] = '\0';
    }

    return used > 0;
}

static int ha_rest_extract_json_string(const char *json,
                                       const char *key,
                                       char *out,
                                       size_t out_size)
{
    char pattern[48];
    const char *p;
    size_t len = 0;

    if (!json || !key || !out || out_size == 0) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return 0;
    }
    p += strlen(pattern);

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':') {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return 0;
    }
    p++;

    while (*p && *p != '"') {
        char ch = *p;

        if (ch == '\\') {
            p++;
            if (!*p) {
                break;
            }
            ch = *p;
        }
        if (len + 1 < out_size) {
            out[len++] = ch;
        }
        p++;
    }

    out[len] = '\0';
    return *p == '"';
}

static int ha_rest_extract_json_int(const char *json, const char *key, int *out)
{
    char pattern[48];
    const char *p;

    if (!json || !key || !out) {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return 0;
    }
    p += strlen(pattern);

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':') {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if ((*p < '0' || *p > '9') && *p != '-') {
        return 0;
    }

    *out = atoi(p);
    return 1;
}

static ha_rest_cached_state_t *ha_rest_cache_slot(const char *entity_id)
{
    size_t i;

    for (i = 0; i < g_state_count; i++) {
        if (strcmp(g_states[i].entity_id, entity_id) == 0) {
            return &g_states[i];
        }
    }

    if (g_state_count >= HA_CONFIG_MAX_TRACKED_ENTITIES) {
        return NULL;
    }

    snprintf(g_states[g_state_count].entity_id,
             sizeof(g_states[g_state_count].entity_id),
             "%s",
             entity_id);
    g_states[g_state_count].state[0] = '\0';
    g_states[g_state_count].media_title[0] = '\0';
    g_states[g_state_count].media_artist[0] = '\0';
    g_states[g_state_count].media_album[0] = '\0';
    g_states[g_state_count].media_picture[0] = '\0';
    g_states[g_state_count].media_position = 0;
    g_states[g_state_count].media_duration = 0;
    g_states[g_state_count].have_media_position = 0;
    g_states[g_state_count].have_media_duration = 0;
    g_states[g_state_count].position = -1;
    g_states[g_state_count].have_position = 0;
    g_states[g_state_count].version = 0;
    g_states[g_state_count].valid = 0;
    g_state_count++;
    return &g_states[g_state_count - 1];
}

static int ha_rest_fetch_entity(const ha_rest_url_t *url,
                                const char *token,
                                const char *entity_id)
{
    int fd;
    char req[1024];
    char response[8192];
    char state[HA_REST_MAX_STATE];
    char media_title[96];
    char media_artist[96];
    char media_album[96];
    char media_picture[256];
    int position;
    int media_position;
    int media_duration;
    ha_rest_cached_state_t *slot;
    int n;

    if (!url || !token || !*token || !entity_id || !*entity_id) {
        return 0;
    }

    fd = ws_tcp_connect(url->host, url->port);
    if (fd < 0) {
        fprintf(stderr, "[ha_rest] %s fetch failed: connect\n", entity_id);
        return 0;
    }

    n = snprintf(req, sizeof(req),
                 "GET /api/states/%s HTTP/1.1\r\n"
                 "Host: %s:%s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 entity_id,
                 url->host,
                 url->port,
                 token);
    if (n <= 0 || n >= (int)sizeof(req) ||
        !ha_rest_send_all(fd, req, (size_t)n)) {
        fprintf(stderr, "[ha_rest] %s fetch failed: send\n", entity_id);
        close(fd);
        return 0;
    }

    if (!ha_rest_read_response(fd, response, sizeof(response))) {
        fprintf(stderr, "[ha_rest] %s fetch failed: read\n", entity_id);
        close(fd);
        return 0;
    }
    close(fd);

    if (strncmp(response, "HTTP/1.1 200", 12) != 0 &&
        strncmp(response, "HTTP/1.0 200", 12) != 0) {
        fprintf(stderr, "[ha_rest] %s fetch failed: HTTP status\n", entity_id);
        return 0;
    }

    if (!ha_rest_extract_json_string(response, "state", state, sizeof(state))) {
        fprintf(stderr, "[ha_rest] %s fetch failed: state missing\n", entity_id);
        return 0;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (slot) {
        snprintf(slot->state, sizeof(slot->state), "%s", state);
        if (ha_rest_extract_json_int(response, "current_position", &position)) {
            slot->position = position;
            slot->have_position = 1;
        }
        if (ha_rest_extract_json_string(response, "media_title", media_title, sizeof(media_title))) {
            snprintf(slot->media_title, sizeof(slot->media_title), "%s", media_title);
        } else {
            slot->media_title[0] = '\0';
        }
        if (ha_rest_extract_json_string(response, "media_artist", media_artist, sizeof(media_artist))) {
            snprintf(slot->media_artist, sizeof(slot->media_artist), "%s", media_artist);
        } else {
            slot->media_artist[0] = '\0';
        }
        if (ha_rest_extract_json_string(response, "media_album_name", media_album, sizeof(media_album))) {
            snprintf(slot->media_album, sizeof(slot->media_album), "%s", media_album);
        } else {
            slot->media_album[0] = '\0';
        }
        if (ha_rest_extract_json_string(response, "entity_picture", media_picture, sizeof(media_picture))) {
            snprintf(slot->media_picture, sizeof(slot->media_picture), "%s", media_picture);
        } else {
            slot->media_picture[0] = '\0';
        }
        if (ha_rest_extract_json_int(response, "media_position", &media_position)) {
            slot->media_position = media_position;
            slot->have_media_position = 1;
        }
        if (ha_rest_extract_json_int(response, "media_duration", &media_duration)) {
            slot->media_duration = media_duration;
            slot->have_media_duration = 1;
        }
        slot->version++;
        slot->valid = 1;
    }

    if (slot && slot->have_position) {
        fprintf(stderr, "[ha_rest] %s state=%s position=%d\n", entity_id, state, slot->position);
    } else {
        fprintf(stderr, "[ha_rest] %s state=%s\n", entity_id, state);
    }
    return 1;
}

int ha_rest_fetch_configured_states(const char *base_url, const char *token)
{
    ha_rest_url_t url;
    size_t count;
    size_t i;
    int ok_count = 0;

    if (!ha_rest_parse_base_url(base_url, &url)) {
        fprintf(stderr, "[ha_rest] configured state fetch skipped: bad base_url\n");
        return 0;
    }

    if (!token || !*token) {
        fprintf(stderr, "[ha_rest] configured state fetch skipped: missing token\n");
        return 0;
    }

    count = ha_config_get_tracked_entity_count();
    fprintf(stderr,
            "[ha_rest] fetching %lu configured state(s) from %s:%s\n",
            (unsigned long)count,
            url.host,
            url.port);

    for (i = 0; i < count; i++) {
        const char *entity_id = ha_config_get_tracked_entity(i);
        if (entity_id && ha_rest_fetch_entity(&url, token, entity_id)) {
            ok_count++;
        }
    }

    fprintf(stderr,
            "[ha_rest] configured state fetch complete: %d/%lu ok\n",
            ok_count,
            (unsigned long)count);
    return ok_count == (int)count;
}

int ha_rest_fetch_state(const char *base_url, const char *token, const char *entity_id)
{
    ha_rest_url_t url;

    if (!ha_rest_parse_base_url(base_url, &url)) {
        fprintf(stderr, "[ha_rest] state fetch skipped: bad base_url\n");
        return 0;
    }

    if (!token || !*token || !entity_id || !*entity_id) {
        fprintf(stderr, "[ha_rest] state fetch skipped: missing input\n");
        return 0;
    }

    return ha_rest_fetch_entity(&url, token, entity_id);
}

int ha_rest_call_service(const char *base_url,
                         const char *token,
                         const char *service,
                         const char *entity_id)
{
    ha_rest_url_t url;
    char domain[32];
    char service_name[32];
    const char *dot;
    size_t domain_len;
    int fd;
    int n;
    char body[160];
    char req[1024];
    char response[8192];

    if (!ha_rest_parse_base_url(base_url, &url)) {
        fprintf(stderr, "[ha_rest] service call skipped: bad base_url\n");
        return 0;
    }
    if (!token || !*token || !service || !entity_id || !*entity_id) {
        fprintf(stderr, "[ha_rest] service call skipped: missing input\n");
        return 0;
    }

    dot = strchr(service, '.');
    if (!dot || dot == service || !dot[1]) {
        fprintf(stderr, "[ha_rest] service call skipped: bad service=%s\n", service);
        return 0;
    }

    domain_len = (size_t)(dot - service);
    if (domain_len >= sizeof(domain) || strlen(dot + 1) >= sizeof(service_name)) {
        fprintf(stderr, "[ha_rest] service call skipped: service too long\n");
        return 0;
    }
    memcpy(domain, service, domain_len);
    domain[domain_len] = '\0';
    snprintf(service_name, sizeof(service_name), "%s", dot + 1);

    n = snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);
    if (n <= 0 || n >= (int)sizeof(body)) {
        fprintf(stderr, "[ha_rest] service call skipped: body too long\n");
        return 0;
    }

    fd = ws_tcp_connect(url.host, url.port);
    if (fd < 0) {
        fprintf(stderr, "[ha_rest] %s service call failed: connect\n", service);
        return 0;
    }

    n = snprintf(req, sizeof(req),
                 "POST /api/services/%s/%s HTTP/1.1\r\n"
                 "Host: %s:%s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Accept: application/json\r\n"
                 "Content-Length: %lu\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 domain,
                 service_name,
                 url.host,
                 url.port,
                 token,
                 (unsigned long)strlen(body),
                 body);
    if (n <= 0 || n >= (int)sizeof(req) ||
        !ha_rest_send_all(fd, req, (size_t)n)) {
        fprintf(stderr, "[ha_rest] %s service call failed: send\n", service);
        close(fd);
        return 0;
    }

    if (!ha_rest_read_response(fd, response, sizeof(response))) {
        fprintf(stderr, "[ha_rest] %s service call failed: read\n", service);
        close(fd);
        return 0;
    }
    close(fd);

    if (strncmp(response, "HTTP/1.1 200", 12) != 0 &&
        strncmp(response, "HTTP/1.0 200", 12) != 0 &&
        strncmp(response, "HTTP/1.1 201", 12) != 0 &&
        strncmp(response, "HTTP/1.0 201", 12) != 0) {
        fprintf(stderr, "[ha_rest] %s service call failed: HTTP status\n", service);
        return 0;
    }

    fprintf(stderr,
            "[ha_rest] service call ok: %s entity_id=%s\n",
            service,
            entity_id);
    if (strncmp(service, "cover.", 6) != 0) {
        (void)ha_rest_fetch_entity(&url, token, entity_id);
    }
    return 1;
}

const char *ha_rest_get_cached_state(const char *entity_id)
{
    size_t i;

    if (!entity_id) {
        return NULL;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0) {
            return g_states[i].state;
        }
    }
    return NULL;
}

const char *ha_rest_get_cached_media_title(const char *entity_id)
{
    size_t i;

    if (!entity_id) {
        return NULL;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0) {
            return g_states[i].media_title[0] ? g_states[i].media_title : NULL;
        }
    }
    return NULL;
}

const char *ha_rest_get_cached_media_artist(const char *entity_id)
{
    size_t i;

    if (!entity_id) {
        return NULL;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0) {
            return g_states[i].media_artist[0] ? g_states[i].media_artist : NULL;
        }
    }
    return NULL;
}

const char *ha_rest_get_cached_media_album(const char *entity_id)
{
    size_t i;

    if (!entity_id) {
        return NULL;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0) {
            return g_states[i].media_album[0] ? g_states[i].media_album : NULL;
        }
    }
    return NULL;
}

int ha_rest_get_cached_media_position(const char *entity_id, int *position)
{
    size_t i;

    if (!entity_id || !position) {
        return 0;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0 &&
            g_states[i].have_media_position) {
            *position = g_states[i].media_position;
            return 1;
        }
    }
    return 0;
}

int ha_rest_get_cached_media_duration(const char *entity_id, int *duration)
{
    size_t i;

    if (!entity_id || !duration) {
        return 0;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0 &&
            g_states[i].have_media_duration) {
            *duration = g_states[i].media_duration;
            return 1;
        }
    }
    return 0;
}

const char *ha_rest_get_cached_media_picture(const char *entity_id)
{
    size_t i;

    if (!entity_id) {
        return NULL;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0) {
            return g_states[i].media_picture[0] ? g_states[i].media_picture : NULL;
        }
    }
    return NULL;
}

unsigned long ha_rest_get_cached_version(const char *entity_id)
{
    size_t i;

    if (!entity_id) {
        return 0;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0) {
            return g_states[i].version;
        }
    }
    return 0;
}

int ha_rest_get_cached_position(const char *entity_id, int *position)
{
    size_t i;

    if (!entity_id || !position) {
        return 0;
    }

    for (i = 0; i < g_state_count; i++) {
        if (g_states[i].valid && strcmp(g_states[i].entity_id, entity_id) == 0 &&
            g_states[i].have_position) {
            *position = g_states[i].position;
            return 1;
        }
    }
    return 0;
}

void ha_rest_set_cached_state(const char *entity_id, const char *state)
{
    ha_rest_cached_state_t *slot;

    if (!entity_id || !*entity_id || !state) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    snprintf(slot->state, sizeof(slot->state), "%s", state);
    slot->version++;
    slot->valid = 1;
}

void ha_rest_set_cached_media_title(const char *entity_id, const char *title)
{
    ha_rest_cached_state_t *slot;

    if (!entity_id || !*entity_id) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    if (title && *title) {
        snprintf(slot->media_title, sizeof(slot->media_title), "%s", title);
    } else {
        slot->media_title[0] = '\0';
    }
    slot->version++;
    slot->valid = 1;
}

static void set_cached_string_field(const char *entity_id, const char *value, int field)
{
    ha_rest_cached_state_t *slot;
    char *dest;
    size_t dest_size;

    if (!entity_id || !*entity_id) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    if (field == 0) {
        dest = slot->media_artist;
        dest_size = sizeof(slot->media_artist);
    } else if (field == 1) {
        dest = slot->media_album;
        dest_size = sizeof(slot->media_album);
    } else {
        dest = slot->media_picture;
        dest_size = sizeof(slot->media_picture);
    }

    if (value && *value) {
        snprintf(dest, dest_size, "%s", value);
    } else {
        dest[0] = '\0';
    }
    slot->version++;
    slot->valid = 1;
}

void ha_rest_set_cached_media_artist(const char *entity_id, const char *artist)
{
    set_cached_string_field(entity_id, artist, 0);
}

void ha_rest_set_cached_media_album(const char *entity_id, const char *album)
{
    set_cached_string_field(entity_id, album, 1);
}

void ha_rest_set_cached_media_picture(const char *entity_id, const char *picture)
{
    set_cached_string_field(entity_id, picture, 2);
}

void ha_rest_set_cached_media_position(const char *entity_id, int position)
{
    ha_rest_cached_state_t *slot;

    if (!entity_id || !*entity_id) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    slot->media_position = position;
    slot->have_media_position = 1;
    slot->version++;
    slot->valid = 1;
}

void ha_rest_set_cached_media_duration(const char *entity_id, int duration)
{
    ha_rest_cached_state_t *slot;

    if (!entity_id || !*entity_id) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    slot->media_duration = duration;
    slot->have_media_duration = 1;
    slot->version++;
    slot->valid = 1;
}

void ha_rest_set_cached_position(const char *entity_id, int position)
{
    ha_rest_cached_state_t *slot;

    if (!entity_id || !*entity_id) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    slot->position = position;
    slot->have_position = 1;
    slot->version++;
    slot->valid = 1;
}

void ha_rest_clear_cached_position(const char *entity_id)
{
    ha_rest_cached_state_t *slot;

    if (!entity_id || !*entity_id) {
        return;
    }

    slot = ha_rest_cache_slot(entity_id);
    if (!slot) {
        return;
    }

    slot->position = -1;
    slot->have_position = 0;
}

size_t ha_rest_get_cached_count(void)
{
    return g_state_count;
}
