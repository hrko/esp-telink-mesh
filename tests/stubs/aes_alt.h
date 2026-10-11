#pragma once
#include <cstring>
constexpr int ESP_AES_ENCRYPT = 1;
struct mbedtls_aes_context {};
inline void mbedtls_aes_init(mbedtls_aes_context *) {}
inline void mbedtls_aes_free(mbedtls_aes_context *) {}
inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *, const unsigned char *, unsigned) { return 0; }
inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context *, int, const unsigned char *in, unsigned char *out) {
  std::memcpy(out, in, 16);
  return 0;
}
