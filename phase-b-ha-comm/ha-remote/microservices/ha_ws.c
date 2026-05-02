#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static ha_session_t g_ha = { .fd = -1 };

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

void ha_session_close(void) {
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

static void handle_state_changed(const char *msg)
{
  char entity_id[64];
  char state[HA_REST_MAX_STATE];
  const char *new_state;
  int position;

  if(!msg || !strstr(msg, "\"event_type\":\"state_changed\"")) return;
  if(!json_extract_string_after(msg, "entity_id", entity_id, sizeof(entity_id))) return;
  if(!is_configured_entity(entity_id)) return;

  new_state = strstr(msg, "\"new_state\"");
  if(!new_state) {
    if(strcmp(entity_id, "switch.ikea_power_plug") == 0) {
      fprintf(stderr, "[ha_ws] switch event ignored: new_state missing\n");
    }
    return;
  }
  if(!json_extract_string_after(new_state, "state", state, sizeof(state))) {
    if(strcmp(entity_id, "switch.ikea_power_plug") == 0) {
      fprintf(stderr, "[ha_ws] switch event ignored: state parse failed\n");
    }
    return;
  }

  ha_rest_set_cached_state(entity_id, state);
  if(json_extract_int_after(new_state, "current_position", &position)) {
    ha_rest_set_cached_position(entity_id, position);
    fprintf(stderr, "[ha_ws] state_changed %s state=%s position=%d\n",
            entity_id,
            state,
            position);
  } else {
    fprintf(stderr, "[ha_ws] state_changed %s state=%s\n", entity_id, state);
  }
  ui_refresh_cards();
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
    handle_state_changed(msg);
  }
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
