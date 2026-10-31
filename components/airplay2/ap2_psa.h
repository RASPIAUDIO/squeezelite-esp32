#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "psa/crypto.h"

/* Complete, no-padding operation. CTR is symmetric; CBC is decrypt-only. */
int ap2_aes(const uint8_t key[16], const uint8_t iv[16], bool ctr,
            const uint8_t *input, size_t size, uint8_t *output);
char *ap2_rsa(const char *pem, const unsigned char *input, size_t len,
              int *outlen, bool auth);
