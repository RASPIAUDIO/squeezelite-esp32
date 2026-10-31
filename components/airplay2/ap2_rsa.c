/* SPDX-License-Identifier: MIT
 * RAOP v1 uses a raw PKCS#1 v1.5 private signature for Apple-Challenge,
 * RSA-OAEP/SHA1 for the session key, and AES-128-CBC without padding.
 */
#include "ap2_psa.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include <stdlib.h>
#include <string.h>

static void crypto_alloc_failed(size_t size, uint32_t caps, const char *function) {
    // Allocation-failure diagnostics must not allocate memory themselves.
    esp_rom_printf("RAOP allocation failed: bytes=%u caps=0x%x function=%s\n",
        (unsigned)size, (unsigned)caps, function);
}

char *ap2_rsa(const char *pem, const unsigned char *input, size_t len, int *outlen, bool auth) {
    *outlen = 0;
    heap_caps_register_failed_alloc_callback(crypto_alloc_failed);
    // Reject failed Base64 lengths before PSA can interpret a negative int as
    // a huge size_t allocation. The upstream RAOP RSA key is 2048 bits.
    if (!input || (auth ? len != 32 : len != 256)) {
        ESP_LOGE("raop_crypto", "Invalid RSA %s input length: %u", auth ? "challenge" : "session", (unsigned)len);
        return NULL;
    }
    const char *start = strchr(pem, '\n');
    const char *end = strstr(pem, "-----END RSA PRIVATE KEY-----");
    if (!start || !end || ++start >= end || psa_crypto_init() != PSA_SUCCESS) return NULL;
    size_t der_len = 0, written = 0;
    int ret = mbedtls_base64_decode(NULL, 0, &der_len, (const unsigned char *)start, end - start);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return NULL;
    unsigned char *der = malloc(der_len), *output = NULL;
    if (!der) return NULL;
    ret = mbedtls_base64_decode(der, der_len, &written, (const unsigned char *)start, end - start);
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = 0;
    psa_algorithm_t alg = auth ? PSA_ALG_RSA_PKCS1V15_SIGN_RAW : PSA_ALG_RSA_OAEP(PSA_ALG_SHA_1);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_usage_flags(&attrs, auth ? PSA_KEY_USAGE_SIGN_HASH : PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attrs, alg);
    psa_status_t status = ret ? PSA_ERROR_INVALID_ARGUMENT : psa_import_key(&attrs, der, written, &key);
    const char *stage = "import";
    free(der); // This is the unchanged public RAOP compatibility key from upstream.
    psa_reset_key_attributes(&attrs);
    if (status == PSA_SUCCESS) status = psa_get_key_attributes(key, &attrs);
    size_t rsa_len = (psa_get_key_bits(&attrs) + 7) / 8;
    if (status == PSA_SUCCESS && !(output = malloc(rsa_len))) status = PSA_ERROR_INSUFFICIENT_MEMORY;
    if (status == PSA_SUCCESS) {
        stage = "operation";
        status = auth ? psa_sign_hash(key, alg, input, len, output, rsa_len, &written)
                      : psa_asymmetric_decrypt(key, alg, input, len, NULL, 0, output, rsa_len, &written);
    }
    psa_destroy_key(key);
    psa_reset_key_attributes(&attrs);
    if (status != PSA_SUCCESS) {
        ESP_LOGE("raop_crypto", "RSA %s %s failed: %ld (input=%u, RSA=%u, internal=%u, PSRAM=%u)",
            auth ? "challenge" : "session key", stage, (long)status, (unsigned)len, (unsigned)rsa_len,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        free(output);
        return NULL;
    }
    *outlen = (int)written;
    return (char *)output;
}


#endif
