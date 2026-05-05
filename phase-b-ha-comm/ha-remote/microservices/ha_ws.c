#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "ha_ws.h"
#include "crypto.h"
#include "ha_config.h"
#include "ha_rest.h"
#include "ui.h"
#include "ws_io.h"

#define HA_WS_UPDATE_QUEUE_LEN 16
#define HA_WS_DRAIN_MAX 8

/* HA/WebSocket state (encapsulated here) */
static char g_ha_status[128] = "";

static uint64_t ms_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

typedef struct {
  int fd;
  sockbuf_t sb;
  uint32_t next_id;
  uint32_t id_get_states;
  uint32_t id_subscribe;
  int have_states;
  int subscribed;
} ha_session_t;

typedef struct {
  char entity_id[64];
  char state[HA_REST_MAX_STATE];
  char media_title[96];
  char media_artist[96];
  char media_album[96];
  char media_picture[256];
  int is_media;
  int have_media_title;
  int have_media_artist;
  int have_media_album;
  int have_media_picture;
  int media_position;
  int have_media_position;
  int media_duration;
  int have_media_duration;
  int position;
  int have_position;
} ha_state_update_t;

static ha_session_t g_ha = { .fd = -1 };
static pthread_t g_rx_thread;
static pthread_mutex_t g_rx_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_rx_running = 0;
static int g_rx_started = 0;
static pthread_mutex_t g_update_lock = PTHREAD_MUTEX_INITIALIZER;
static ha_state_update_t g_update_queue[HA_WS_UPDATE_QUEUE_LEN];
static unsigned int g_update_head = 0;
static unsigned int g_update_tail = 0;

void ha_session_note_activity(void)
{
  /* Snapshot-only: UI activity is not tracked yet. */
}

/* Unified status bridge */
static void ha_refresh_ui(void) {
  ui_status_set(g_ha_status, (g_ha.fd >= 0), g_ha.have_states);
}

/* xorshift32, OK for WS masking */
static uint32_t rng_state = 0;

void ha_ws_seed(uint32_t seed)
{
  rng_state = seed ? seed : 0xA5A5A5A5u;
}

static uint32_t rng_u32(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

/* Forward declaration */
static void set_status(const char *s);
static void *ha_receiver_thread_main(void *arg);

static int update_queue_empty(void)
{
  return g_update_head == g_update_tail;
}

static int update_queue_full(void)
{
  return ((g_update_tail + 1U) % HA_WS_UPDATE_QUEUE_LEN) == g_update_head;
}

static void update_queue_push(const ha_state_update_t *update)
{
  if(!update) return;

  pthread_mutex_lock(&g_update_lock);
  if(update_queue_full()) {
    g_update_head = (g_update_head + 1U) % HA_WS_UPDATE_QUEUE_LEN;
  }
  g_update_queue[g_update_tail] = *update;
  g_update_tail = (g_update_tail + 1U) % HA_WS_UPDATE_QUEUE_LEN;
  pthread_mutex_unlock(&g_update_lock);
}

static int update_queue_pop(ha_state_update_t *update)
{
  int have = 0;

  if(!update) return 0;

  pthread_mutex_lock(&g_update_lock);
  if(!update_queue_empty()) {
    *update = g_update_queue[g_update_head];
    g_update_head = (g_update_head + 1U) % HA_WS_UPDATE_QUEUE_LEN;
    have = 1;
  }
  pthread_mutex_unlock(&g_update_lock);
  return have;
}

static void update_queue_clear(void)
{
  pthread_mutex_lock(&g_update_lock);
  g_update_head = 0;
  g_update_tail = 0;
  pthread_mutex_unlock(&g_update_lock);
}

static void ha_receiver_stop(void)
{
  int started;

  pthread_mutex_lock(&g_rx_lock);
  started = g_rx_started;
  g_rx_running = 0;
  if(started && g_ha.fd >= 0) {
    shutdown(g_ha.fd, SHUT_RDWR);
  }
  pthread_mutex_unlock(&g_rx_lock);

  if(started && !pthread_equal(pthread_self(), g_rx_thread)) {
    pthread_join(g_rx_thread, NULL);
  }

  pthread_mutex_lock(&g_rx_lock);
  g_rx_started = 0;
  pthread_mutex_unlock(&g_rx_lock);
}

void ha_session_close(void) {
  ha_receiver_stop();

  if(g_ha.fd >= 0) {
    close(g_ha.fd);
    set_status("HA: disconnected");
  }
  g_ha.fd = -1;
  sb_init(&g_ha.sb);
  g_ha.next_id = 1;
  g_ha.id_get_states = 0;
  g_ha.id_subscribe = 0;
  g_ha.have_states = 0;
  g_ha.subscribed = 0;
  update_queue_clear();
  ha_refresh_ui();
}

static void set_status(const char *s) {
  if(!s) {
    snprintf(g_ha_status, sizeof(g_ha_status), "%s", "HA: status missing");
    ha_refresh_ui();
    return;
  }

  snprintf(g_ha_status, sizeof(g_ha_status), "%s", s);
  printf("%s\n", s);
  ha_refresh_ui();
}

static int ha_session_connect_and_auth(const char *host, const char *token) {
  const char *port = "8123";
  int fd = -1;
  uint64_t start;

  set_status("HA: connecting...");
  fd = ws_tcp_connect(host, port);
  if(fd < 0) { set_status("HA: connect failed"); return -1; }

  uint8_t keybin[16];
  for(int i = 0; i < 16; i++) keybin[i] = (uint8_t)(rng_u32() & 0xFF);
  char keyb64[64];
  if(b64enc(keybin, sizeof(keybin), keyb64, sizeof(keyb64)) < 0) { close(fd); return -1; }

  static const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  char concat[128];
  snprintf(concat, sizeof(concat), "%s%s", keyb64, GUID);

  sha1_ctx_t sc;
  sha1_init(&sc);
  sha1_update(&sc, concat, strlen(concat));
  uint8_t sha_out[20];
  sha1_final(&sc, sha_out);

  char accept_expected[64];
  if(b64enc(sha_out, sizeof(sha_out), accept_expected, sizeof(accept_expected)) < 0) {
    close(fd); return -1;
  }

  char req[512];
  int n = snprintf(req, sizeof(req),
    "GET /api/websocket HTTP/1.1\r\n"
    "Host: %s:%s\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: %s\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n",
    host, port, keyb64);

  if(n <= 0 || n >= (int)sizeof(req)) { close(fd); return -1; }
  if(send(fd, req, (size_t)n, 0) != (ssize_t)n) { close(fd); return -1; }

  sb_init(&g_ha.sb);

  char hdr[2048];
  size_t used = 0;
  int ok = 0;

  set_status("HA: ws handshake...");
  while(used + 1 < sizeof(hdr)) {
    int w = sock_wait(fd, 1, 2000);
    if(w <= 0) break;
    ssize_t r = recv(fd, hdr + used, sizeof(hdr) - 1 - used, 0);
    if(r <= 0) break;
    used += (size_t)r;
    hdr[used] = 0;

    char *p = strstr(hdr, "\r\n\r\n");
    if(p) {
      size_t header_len = (size_t)(p - hdr) + 4;
      size_t extra = used - header_len;
      if(extra) {
        sb_set(&g_ha.sb, (const uint8_t *)(hdr + header_len), extra);
      }
      hdr[header_len] = 0;
      ok = 1;
      goto handshake_done;
    }
  }

handshake_done:
  if(!ok) {
    set_status("HA: handshake failed");
    close(fd);
    return -1;
  }

  if(strstr(hdr, " 101 ") == NULL || strstr(hdr, accept_expected) == NULL) {
    set_status("HA: handshake failed");
    close(fd);
    return -1;
  }

  char msg[2048];

  set_status("HA: sending auth...");
  char auth[1024];
  n = snprintf(auth, sizeof(auth), "{\"type\":\"auth\",\"access_token\":\"%s\"}", token ? token : "");
  if(n <= 0 || n >= (int)sizeof(auth)) { close(fd); return -1; }

  uint32_t r = rng_u32();
  uint8_t mask[4] = {
    (uint8_t)(r & 0xFF),
    (uint8_t)((r >> 8) & 0xFF),
    (uint8_t)((r >> 16) & 0xFF),
    (uint8_t)((r >> 24) & 0xFF)
  };
  if(ws_send_text(fd, auth, mask) != 0) { close(fd); return -1; }

  start = ms_now();
  while(ms_now() - start < 5000) {
    int rr = ws_recv_text_sb(&g_ha.sb, fd, msg, sizeof(msg), 2000);
    if(rr != 0) continue;
    if(msg[0] == 0) continue;
    if(strstr(msg, "auth_ok")) {
      set_status("HA: auth_ok");
      return fd;
    }
    if(strstr(msg, "auth_invalid")) {
      set_status("HA: auth_invalid");
      close(fd);
      return -1;
    }
  }

  set_status("HA: auth timeout");
  close(fd);
  return -1;
}

static int msg_has_id(const char *msg, uint32_t id) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"id\":%u", (unsigned)id);
  return strstr(msg, pat) != NULL;
}

static int parse_base_url(const char *base_url, char *host, size_t host_size)
{
  const char *p;
  const char *start;
  size_t len;

  if(!base_url || !host || host_size == 0) return 0;
  if(strncmp(base_url, "http://", 7) != 0) return 0;
  start = base_url + 7;
  p = start;
  while(*p && *p != ':' && *p != '/') p++;
  len = (size_t)(p - start);
  if(len == 0 || len >= host_size) return 0;
  memcpy(host, start, len);
  host[len] = '\0';
  return 1;
}

static int json_extract_string_after(const char *start,
                                     const char *key,
                                     char *out,
                                     size_t out_size)
{
  char pattern[48];
  const char *p;
  size_t len = 0;

  if(!start || !key || !out || out_size == 0) return 0;
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(start, pattern);
  if(!p) return 0;
  p += strlen(pattern);
  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if(*p != ':') return 0;
  p++;
  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if(*p != '"') return 0;
  p++;
  while(*p && *p != '"') {
    char ch = *p;
    if(ch == '\\') {
      p++;
      if(!*p) break;
      ch = *p;
    }
    if(len + 1 < out_size) out[len++] = ch;
    p++;
  }
  out[len] = '\0';
  return *p == '"';
}

static int json_extract_int_after(const char *start, const char *key, int *out)
{
  char pattern[48];
  const char *p;

  if(!start || !key || !out) return 0;
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(start, pattern);
  if(!p) return 0;
  p += strlen(pattern);
  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if(*p != ':') return 0;
  p++;
  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if((*p < '0' || *p > '9') && *p != '-') return 0;
  *out = atoi(p);
  return 1;
}

static int is_configured_entity(const char *entity_id)
{
  size_t count = ha_config_get_tracked_entity_count();
  size_t i;

  if(!entity_id || !*entity_id) return 0;
  for(i = 0; i < count; i++) {
    const char *tracked = ha_config_get_tracked_entity(i);
    if(tracked && strcmp(tracked, entity_id) == 0) return 1;
  }
  return 0;
}

static int parse_state_changed(const char *msg, ha_state_update_t *update)
{
  char entity_id[64];
  char state[HA_REST_MAX_STATE];
  char media_title[96];
  char media_artist[96];
  char media_album[96];
  char media_picture[256];
  const char *new_state;
  int position;
  int media_position;
  int media_duration;

  if(!msg || !update || !strstr(msg, "\"event_type\":\"state_changed\"")) return 0;
  if(!json_extract_string_after(msg, "entity_id", entity_id, sizeof(entity_id))) return 0;
  if(!is_configured_entity(entity_id)) return 0;

  new_state = strstr(msg, "\"new_state\"");
  if(!new_state) {
    if(strcmp(entity_id, "switch.ikea_power_plug") == 0) {
      fprintf(stderr, "[ha_ws] switch event ignored: new_state missing\n");
    }
    return 0;
  }
  if(!json_extract_string_after(new_state, "state", state, sizeof(state))) {
    if(strcmp(entity_id, "switch.ikea_power_plug") == 0) {
      fprintf(stderr, "[ha_ws] switch event ignored: state parse failed\n");
    }
    return 0;
  }

  memset(update, 0, sizeof(*update));
  snprintf(update->entity_id, sizeof(update->entity_id), "%s", entity_id);
  snprintf(update->state, sizeof(update->state), "%s", state);
  update->is_media = strncmp(entity_id, "media_player.", 13) == 0;

  if(json_extract_string_after(new_state, "media_title", media_title, sizeof(media_title))) {
    snprintf(update->media_title, sizeof(update->media_title), "%s", media_title);
    update->have_media_title = 1;
  }
  if(json_extract_string_after(new_state, "media_artist", media_artist, sizeof(media_artist))) {
    snprintf(update->media_artist, sizeof(update->media_artist), "%s", media_artist);
    update->have_media_artist = 1;
  }
  if(json_extract_string_after(new_state, "media_album_name", media_album, sizeof(media_album))) {
    snprintf(update->media_album, sizeof(update->media_album), "%s", media_album);
    update->have_media_album = 1;
  }
  if(json_extract_string_after(new_state, "entity_picture", media_picture, sizeof(media_picture))) {
    snprintf(update->media_picture, sizeof(update->media_picture), "%s", media_picture);
    update->have_media_picture = 1;
  }
  if(json_extract_int_after(new_state, "media_position", &media_position)) {
    update->media_position = media_position;
    update->have_media_position = 1;
  }
  if(json_extract_int_after(new_state, "media_duration", &media_duration)) {
    update->media_duration = media_duration;
    update->have_media_duration = 1;
  }
  if(json_extract_int_after(new_state, "current_position", &position)) {
    update->position = position;
    update->have_position = 1;
    fprintf(stderr, "[ha_ws] state_changed %s state=%s position=%d\n",
            entity_id,
            state,
            position);
  } else {
    fprintf(stderr, "[ha_ws] state_changed %s state=%s\n", entity_id, state);
  }
  return 1;
}

int ha_session_start(const char *host, const char *token) {
  ha_session_close();

  int fd = ha_session_connect_and_auth(host, token);
  if(fd < 0) return -1;

  g_ha.fd = fd;
  g_ha.next_id = 1;

  g_ha.id_get_states = g_ha.next_id++;
  set_status("HA: sending get_states...");
  char cmd1[96];
  snprintf(cmd1, sizeof(cmd1), "{\"id\":%u,\"type\":\"get_states\"}", (unsigned)g_ha.id_get_states);

  uint32_t r2 = rng_u32();
  uint8_t mask2[4] = {
    (uint8_t)(r2 & 0xFF),
    (uint8_t)((r2 >> 8) & 0xFF),
    (uint8_t)((r2 >> 16) & 0xFF),
    (uint8_t)((r2 >> 24) & 0xFF)
  };
  if(ws_send_text(g_ha.fd, cmd1, mask2) != 0) {
    set_status("HA: get_states send failed");
    ha_session_close();
    return -1;
  }

  set_status("HA: waiting states result...");
  uint64_t start = ms_now();
  char msg[8192];
  int got_states = 0;
  while(ms_now() - start < 15000) {
    int rcv = ws_recv_text_sb(&g_ha.sb, g_ha.fd, msg, sizeof(msg), 2000);
    if(rcv != 0) continue;
    if(msg[0] == 0) continue;

    if(strstr(msg, "\"type\":\"result\"") && msg_has_id(msg, g_ha.id_get_states)) {
      if(strstr(msg, "\"success\":true")) {
        got_states = 1;
        break;
      } else {
        set_status("HA: get_states failed");
        ha_session_close();
        return -1;
      }
    }
  }
  if(!got_states) {
    set_status("HA: get_states timeout");
    ha_session_close();
    return -1;
  }
  g_ha.have_states = 1;

  set_status("HA: snapshot ok");
  ha_session_close();
  return 0;
}

void ha_poll_timer(void) {
  char msg[8192];

  if(g_ha.fd < 0 || !g_ha.subscribed) return;

  for(;;) {
    int rcv = ws_recv_text_sb(&g_ha.sb, g_ha.fd, msg, sizeof(msg), 0);
    if(rcv != 0 || msg[0] == 0) break;
    {
      ha_state_update_t update;
      if(parse_state_changed(msg, &update)) {
        update_queue_push(&update);
      }
    }
  }
}

static void apply_state_update(const ha_state_update_t *update)
{
  if(!update || !update->entity_id[0]) return;

  ha_rest_set_cached_state(update->entity_id, update->state);
  if(update->is_media) {
    ha_rest_set_cached_media_title(update->entity_id,
                                   update->have_media_title ? update->media_title : NULL);
    ha_rest_set_cached_media_artist(update->entity_id,
                                    update->have_media_artist ? update->media_artist : NULL);
    ha_rest_set_cached_media_album(update->entity_id,
                                   update->have_media_album ? update->media_album : NULL);
    ha_rest_set_cached_media_picture(update->entity_id,
                                     update->have_media_picture ? update->media_picture : NULL);
  }
  if(update->have_media_position) {
    ha_rest_set_cached_media_position(update->entity_id, update->media_position);
  }
  if(update->have_media_duration) {
    ha_rest_set_cached_media_duration(update->entity_id, update->media_duration);
  }
  if(update->have_position) {
    ha_rest_set_cached_position(update->entity_id, update->position);
  }
}

void ha_ws_drain_state_updates(void)
{
  int refreshed = 0;

  for(int i = 0; i < HA_WS_DRAIN_MAX; i++) {
    ha_state_update_t update;
    if(!update_queue_pop(&update)) break;
    apply_state_update(&update);
    refreshed = 1;
  }

  if(refreshed) {
    ui_refresh_cards();
  }
}

static void *ha_receiver_thread_main(void *arg)
{
  (void)arg;

  for(;;) {
    char msg[8192];
    ha_state_update_t update;
    int running;
    int rcv;

    pthread_mutex_lock(&g_rx_lock);
    running = g_rx_running;
    pthread_mutex_unlock(&g_rx_lock);
    if(!running) break;

    rcv = ws_recv_text_sb(&g_ha.sb, g_ha.fd, msg, sizeof(msg), -1);
    if(rcv != 0) break;
    if(msg[0] == 0) continue;

    if(parse_state_changed(msg, &update)) {
      update_queue_push(&update);
    }
  }

  pthread_mutex_lock(&g_rx_lock);
  g_rx_running = 0;
  pthread_mutex_unlock(&g_rx_lock);
  return NULL;
}

int ha_session_subscribe_state_changes(const char *base_url, const char *token)
{
  char host[96];
  char cmd[128];
  int n;
  uint32_t r;
  uint8_t mask[4];
  uint64_t start;
  char msg[2048];

  ha_session_close();

  if(!parse_base_url(base_url, host, sizeof(host))) {
    set_status("HA: bad ws base_url");
    return -1;
  }

  int fd = ha_session_connect_and_auth(host, token);
  if(fd < 0) return -1;

  g_ha.fd = fd;
  g_ha.next_id = 1;
  g_ha.id_subscribe = g_ha.next_id++;

  n = snprintf(cmd, sizeof(cmd),
               "{\"id\":%u,\"type\":\"subscribe_events\",\"event_type\":\"state_changed\"}",
               (unsigned)g_ha.id_subscribe);
  if(n <= 0 || n >= (int)sizeof(cmd)) {
    ha_session_close();
    return -1;
  }

  r = rng_u32();
  mask[0] = (uint8_t)(r & 0xFF);
  mask[1] = (uint8_t)((r >> 8) & 0xFF);
  mask[2] = (uint8_t)((r >> 16) & 0xFF);
  mask[3] = (uint8_t)((r >> 24) & 0xFF);

  set_status("HA: subscribing events...");
  if(ws_send_text(g_ha.fd, cmd, mask) != 0) {
    set_status("HA: subscribe send failed");
    ha_session_close();
    return -1;
  }

  start = ms_now();
  while(ms_now() - start < 5000) {
    int rcv = ws_recv_text_sb(&g_ha.sb, g_ha.fd, msg, sizeof(msg), 1000);
    if(rcv != 0 || msg[0] == 0) continue;
    if(strstr(msg, "\"type\":\"result\"") && msg_has_id(msg, g_ha.id_subscribe)) {
      if(strstr(msg, "\"success\":true")) {
        g_ha.subscribed = 1;
        set_status("HA: state subscription ok");
        pthread_mutex_lock(&g_rx_lock);
        g_rx_running = 1;
        pthread_mutex_unlock(&g_rx_lock);
        if(pthread_create(&g_rx_thread, NULL, ha_receiver_thread_main, NULL) != 0) {
          pthread_mutex_lock(&g_rx_lock);
          g_rx_running = 0;
          pthread_mutex_unlock(&g_rx_lock);
          set_status("HA: receiver start failed");
          ha_session_close();
          return -1;
        }
        pthread_mutex_lock(&g_rx_lock);
        g_rx_started = 1;
        pthread_mutex_unlock(&g_rx_lock);
        return 0;
      }
      set_status("HA: subscribe failed");
      ha_session_close();
      return -1;
    }
  }

  set_status("HA: subscribe timeout");
  ha_session_close();
  return -1;
}
