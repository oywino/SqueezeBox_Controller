#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>

#include "ws_io.h"

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, 0);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

int ws_send_text(int fd, const char *s, const uint8_t mask[4])
{
    if (fd < 0 || !s || !mask) return -1;

    size_t len = strlen(s);
    uint8_t hdr[2 + 8 + 4]; /* base + 64-bit ext + mask */
    size_t hlen = 0;

    hdr[0] = 0x81; /* FIN | text */
    if (len <= 125) {
        hdr[1] = 0x80 | (uint8_t)len; /* MASK | len */
        hlen = 2;
    } else if (len <= 0xFFFFu) {
        hdr[1] = 0x80 | 126;
        hdr[2] = (uint8_t)((len >> 8) & 0xFF);
        hdr[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    } else {
        uint64_t L = (uint64_t)len;
        hdr[1] = 0x80 | 127;
        hdr[2] = (uint8_t)((L >> 56) & 0xFF);
        hdr[3] = (uint8_t)((L >> 48) & 0xFF);
        hdr[4] = (uint8_t)((L >> 40) & 0xFF);
        hdr[5] = (uint8_t)((L >> 32) & 0xFF);
        hdr[6] = (uint8_t)((L >> 24) & 0xFF);
        hdr[7] = (uint8_t)((L >> 16) & 0xFF);
        hdr[8] = (uint8_t)((L >>  8) & 0xFF);
        hdr[9] = (uint8_t)( L        & 0xFF);
        hlen = 10;
    }

    /* append mask */
    hdr[hlen + 0] = mask[0];
    hdr[hlen + 1] = mask[1];
    hdr[hlen + 2] = mask[2];
    hdr[hlen + 3] = mask[3];
    hlen += 4;

    if (send_all(fd, hdr, hlen) != 0) return -1;

    /* stream masked payload */
    const uint8_t *in = (const uint8_t *)s;
    uint8_t buf[1024];
    size_t off = 0;
    while (off < len) {
        size_t n = len - off;
        if (n > sizeof(buf)) n = sizeof(buf);
        for (size_t i = 0; i < n; i++) {
            buf[i] = in[off + i] ^ mask[(off + i) & 3];
        }
        if (send_all(fd, buf, n) != 0) return -1;
        off += n;
    }
    return 0;
}

int ws_tcp_connect(const char *host, const char *port)
{
    if(!host || !port) return -1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL, *rp = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if(rc != 0) return -1;

    int fd = -1;
    for(rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(fd < 0) continue;
        if(connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }

    if(res) freeaddrinfo(res);
    return fd;
}

/* === Receive helpers === */
void sb_init(sockbuf_t *sb) {
    sb->len = 0;
    sb->off = 0;
}

size_t sb_avail(const sockbuf_t *sb) {
    return (sb->len > sb->off) ? (sb->len - sb->off) : 0;
}

void sb_set(sockbuf_t *sb, const uint8_t *p, size_t n) {
    if(n > sizeof(sb->buf)) {
        p += (n - sizeof(sb->buf));
        n = sizeof(sb->buf);
    }
    memcpy(sb->buf, p, n);
    sb->len = n;
    sb->off = 0;
}

int sock_wait(int fd, int want_read, int timeout_ms) {
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if(want_read) FD_SET(fd, &rfds);
    else FD_SET(fd, &wfds);

    struct timeval tv;
    struct timeval *ptv = NULL;
    if(timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }
    return select(fd + 1,
                  want_read ? &rfds : NULL,
                  want_read ? NULL : &wfds,
                  NULL,
                  ptv);
}

int recv_all_sb(sockbuf_t *sb, int fd, void *buf, size_t want) {
    uint8_t *out = (uint8_t *)buf;
    size_t got = 0;

    while(got < want) {
        size_t a = sb ? sb_avail(sb) : 0;
        if(a) {
            size_t take = want - got;
            if(take > a) take = a;
            memcpy(out + got, sb->buf + sb->off, take);
            sb->off += take;
            got += take;
            continue;
        }

        if(sb && sb->len && sb->off >= sb->len) {
            sb->len = 0;
            sb->off = 0;
        }

        ssize_t r = recv(fd,
                         sb ? sb->buf : (out + got),
                         sb ? sizeof(sb->buf) : (want - got),
                         0);
        if(r <= 0) return -1;

        if(sb) {
            sb->len = (size_t)r;
            sb->off = 0;
        } else {
            got += (size_t)r;
        }
    }
    return 0;
}

int ws_recv_text_sb(sockbuf_t *sb, int fd, char *out, size_t outlen, int timeout_ms) {
    if(outlen == 0) return -1;
    out[0] = 0;

    int w = sock_wait(fd, 1, timeout_ms);
    if(w <= 0) return -1;

    uint8_t h2[2];
    if(recv_all_sb(sb, fd, h2, 2) != 0) return -1;

    uint8_t op = (h2[0] & 0x0F);
    uint8_t masked = (h2[1] & 0x80);
    uint64_t len = (h2[1] & 0x7F);

    if(len == 126) {
        uint8_t b[2];
        if(recv_all_sb(sb, fd, b, 2) != 0) return -1;
        len = ((uint64_t)b[0] << 8) | b[1];
    } else if(len == 127) {
        uint8_t b[8];
        if(recv_all_sb(sb, fd, b, 8) != 0) return -1;
        len = 0;
        for(int i = 0; i < 8; i++) len = (len << 8) | b[i];
    }

    uint8_t mask[4] = {0,0,0,0};
    if(masked) {
        if(recv_all_sb(sb, fd, mask, 4) != 0) return -1;
    }

    uint8_t *payload = malloc((size_t)len + 1);
    if(!payload) return -1;
    if(len && recv_all_sb(sb, fd, payload, (size_t)len) != 0) {
        free(payload);
        return -1;
    }

    if(masked) {
        for(uint64_t i = 0; i < len; i++) payload[i] ^= mask[i & 3];
    }
    payload[len] = 0;

    if(op == 0x1) {
        size_t cpy = (len >= (outlen - 1)) ? (outlen - 1) : (size_t)len;
        memcpy(out, payload, cpy);
        out[cpy] = 0;
    } else if(op == 0x9) {
        ws_send_pong(fd, payload, (size_t)len);
    }

    free(payload);
    return 0;   /* <-- complete return */
}

int ws_send_pong(int fd, const uint8_t *data, size_t len) {
    uint8_t hdr[16];
    size_t hlen = 0;
    hdr[0] = 0x8A; /* FIN + PONG */

    if(len < 126) {
        hdr[1] = (uint8_t)len;
        hlen = 2;
    } else {
        hdr[1] = 126;
        hdr[2] = (uint8_t)((len >> 8) & 0xFF);
        hdr[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    }

    if(send(fd, hdr, hlen, 0) != (ssize_t)hlen) return -1;
    if(len && send(fd, data, len, 0) != (ssize_t)len) return -1;
    return 0;
}
