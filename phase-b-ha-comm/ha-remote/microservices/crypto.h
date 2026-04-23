#ifndef HA_REMOTE_CRYPTO_H
#define HA_REMOTE_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint32_t h[5];
  uint64_t len_bits;
  uint8_t buf[64];
  size_t buf_len;
} sha1_ctx_t;

void sha1_init(sha1_ctx_t *c);
void sha1_update(sha1_ctx_t *c, const void *data, size_t n);
void sha1_final(sha1_ctx_t *c, uint8_t out20[20]);

int b64enc(const uint8_t *in, size_t inlen, char *out, size_t outlen);

#endif /* HA_REMOTE_CRYPTO_H */
