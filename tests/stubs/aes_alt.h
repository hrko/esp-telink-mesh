#pragma once
#include <openssl/evp.h>
#include <cstring>
constexpr int ESP_AES_ENCRYPT = 1;
inline bool aes_fail = false;
struct mbedtls_aes_context { unsigned char key[16]; };
inline void mbedtls_aes_init(mbedtls_aes_context *) {}
inline void mbedtls_aes_free(mbedtls_aes_context *) {}
inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key, unsigned bits) {
  if (aes_fail || bits != 128) return -1;
  std::memcpy(ctx->key, key, 16);
  return 0;
}
inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx, int, const unsigned char *in, unsigned char *out) {
  if (aes_fail) return -1;
  auto *cipher = EVP_CIPHER_CTX_new();
  if (cipher == nullptr) return -1;
  int length = 0;
  int final_length = 0;
  const bool ok = EVP_EncryptInit_ex(cipher, EVP_aes_128_ecb(), nullptr, ctx->key, nullptr) == 1 &&
                  EVP_CIPHER_CTX_set_padding(cipher, 0) == 1 &&
                  EVP_EncryptUpdate(cipher, out, &length, in, 16) == 1 &&
                  EVP_EncryptFinal_ex(cipher, out + length, &final_length) == 1 &&
                  length + final_length == 16;
  EVP_CIPHER_CTX_free(cipher);
  return ok ? 0 : -1;
}
