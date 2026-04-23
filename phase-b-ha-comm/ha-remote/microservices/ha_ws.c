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
  int have_states;
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
  g_ha.have_states = 0;
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
    /* snapshot-only: no persistent WS session yet */
}
