#include "ap2_psa.h"

int ap2_aes(const uint8_t key[16], const uint8_t iv[16], bool ctr,
            const uint8_t *input, size_t size, uint8_t *output) {
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) return -1;
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t id = 0;
    psa_algorithm_t alg = ctr ? PSA_ALG_CTR : PSA_ALG_CBC_NO_PADDING;
    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_algorithm(&attrs, alg);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DECRYPT);
    status = psa_import_key(&attrs, key, 16, &id);
    psa_reset_key_attributes(&attrs);
    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    size_t written = 0, final = 0;
    if (status == PSA_SUCCESS) status = psa_cipher_decrypt_setup(&op, id, alg);
    if (status == PSA_SUCCESS) status = psa_cipher_set_iv(&op, iv, 16);
    if (status == PSA_SUCCESS) status = psa_cipher_update(&op, input, size, output, size, &written);
    if (status == PSA_SUCCESS) status = psa_cipher_finish(&op, output + written, size - written, &final);
    psa_cipher_abort(&op);
    psa_destroy_key(id);
    return status == PSA_SUCCESS && written + final == size ? 0 : -1;
}
