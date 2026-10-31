#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include <psa/crypto.h>
#include <stdbool.h>
#include <stddef.h>
char *raop_rsa_s31(const char *pem, const unsigned char *input, size_t len, int *outlen, bool auth);
psa_status_t raop_aes_s31_import(const unsigned char *key, mbedtls_svc_key_id_t *id);
psa_status_t raop_aes_s31_decrypt(mbedtls_svc_key_id_t id, const unsigned char iv[16],
    const unsigned char *input, unsigned char *output, size_t len);
#endif
