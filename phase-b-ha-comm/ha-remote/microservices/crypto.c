#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "crypto.h"

static inline uint32_t rol32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(sha1_ctx_t *c, const uint8_t blk[64]) {
  uint32_t w[80];
  for(int i = 0; i < 16; i++) {
    w[i] = (uint32_t)blk[i * 4 + 0] << 24 |
           (uint32_t)blk[i * 4 + 1] << 16 |
           (uint32_t)blk[i * 4 + 2] << 8  |
           (uint32_t)blk[i * 4 + 3];
  }
  for(int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  uint32_t a = c->h[0], b = c->h[1], d = c->h[3], e = c->h[4], f, k, t;
  uint32_t cc = c->h[2];

  for(int i = 0; i < 80; i++) {
    if(i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999u; }
    else if(i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1u; }
    else if(i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
    else { f = b ^ cc ^ d; k = 0xCA62C1D6u; }

    t = rol32(a, 5) + f + e + k + w[i];
    e = d;
    d = cc;
    cc = rol32(b, 30);
    b = a;
    a = t;
  }

  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += cc;
  c->h[3] += d;
  c->h[4] += e;
}

void sha1_init(sha1_ctx_t *c) {
  c->h[0] = 0x67452301u;
  c->h[1] = 0xEFCDAB89u;
  c->h[2] = 0x98BADCFEu;
  c->h[3] = 0x10325476u;
  c->h[4] = 0xC3D2E1F0u;
  c->len_bits = 0;
  c->buf_len = 0;
}

void sha1_update(sha1_ctx_t *c, const void *data, size_t n) {
  const uint8_t *p = (const uint8_t *)data;
  c->len_bits += (uint64_t)n * 8ULL;

  while(n) {
    size_t take = 64 - c->buf_len;
    if(take > n) take = n;
    memcpy(c->buf + c->buf_len, p, take);
    c->buf_len += take;
    p += take;
    n -= take;

    if(c->buf_len == 64) {
      sha1_block(c, c->buf);
      c->buf_len = 0;
    }
  }
}

void sha1_final(sha1_ctx_t *c, uint8_t out20[20]) {
  c->buf[c->buf_len++] = 0x80;

  if(c->buf_len > 56) {
    while(c->buf_len < 64) c->buf[c->buf_len++] = 0;
    sha1_block(c, c->buf);
    c->buf_len = 0;
  }

  while(c->buf_len < 56) c->buf[c->buf_len++] = 0;

  uint64_t L = c->len_bits;
  for(int i = 0; i < 8; i++) c->buf[56 + i] = (uint8_t)(L >> (56 - 8 * i));
  sha1_block(c, c->buf);

  for(int i = 0; i < 5; i++) {
    out20[i * 4 + 0] = (uint8_t)(c->h[i] >> 24);
    out20[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
    out20[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
    out20[i * 4 + 3] = (uint8_t)(c->h[i]);
  }
}

int b64enc(const uint8_t *in, size_t inlen, char *out, size_t outlen) {
  const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;

  for(size_t i = 0; i < inlen; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if(i + 1 < inlen) v |= (uint32_t)in[i + 1] << 8;
    if(i + 2 < inlen) v |= (uint32_t)in[i + 2];

    char c0 = tbl[(v >> 18) & 63];
    char c1 = tbl[(v >> 12) & 63];
    char c2 = (i + 1 < inlen) ? tbl[(v >> 6) & 63] : '=';
    char c3 = (i + 2 < inlen) ? tbl[v & 63] : '=';

    if(o + 4 >= outlen) return -1;
    out[o++] = c0; out[o++] = c1; out[o++] = c2; out[o++] = c3;
  }
  if(o >= outlen) return -1;
  out[o] = 0;
  return (int)o;
}
