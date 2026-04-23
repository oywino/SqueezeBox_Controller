#ifndef WS_IO_H
#define WS_IO_H

#include <stdint.h>
#include <stddef.h>

/* Send a FIN text frame with client masking; supports 7/16/64-bit lengths. */
int ws_send_text(int fd, const char *s, const uint8_t mask[4]);

/* TCP connect helper (getaddrinfo + connect loop). Returns fd>=0 or -1 on error. */
int ws_tcp_connect(const char *host, const char *port);

/* Socket read buffer */
typedef struct {
    uint8_t buf[4096];
    size_t len;
    size_t off;
} sockbuf_t;

/* Receive helpers */
void sb_init(sockbuf_t *sb);
size_t sb_avail(const sockbuf_t *sb);
void sb_set(sockbuf_t *sb, const uint8_t *p, size_t n);
int sock_wait(int fd, int want_read, int timeout_ms);
int recv_all_sb(sockbuf_t *sb, int fd, void *buf, size_t want);
int ws_recv_text_sb(sockbuf_t *sb, int fd, char *out, size_t outlen, int timeout_ms);
int ws_send_pong(int fd, const uint8_t *data, size_t len);

#endif /* WS_IO_H */
