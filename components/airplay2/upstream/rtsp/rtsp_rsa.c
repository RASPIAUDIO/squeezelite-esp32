#include "rtsp_rsa.h"
#include "ap2_psa.h"
#include <stdlib.h>

#include <string.h>

#include "esp_log.h"
#include "sodium/utils.h" // sodium_base642bin, sodium_bin2base64

static const char *TAG = "rtsp_rsa";

// Well-known AirPlay RSA private key (public knowledge, used by all receivers)
static const char airplay_rsa_private_key[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Q\n"
    "g90u3sG/1CUtwC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6\n"
    "DZHJ2YCCLlDRKSKv6kDqnw4UwPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLv\n"
    "qr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf/+sG+NCK3eQJVxqcaJ/vEHKIVd2M\n"
    "+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/UAaHqn9JdsBWLUEp\n"
    "VviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfWBLmk\n"
    "zkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14n\n"
    "DY4TFQAaLlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPsc\n"
    "EsA5ltpxOgUGCY7b7ez5NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZu\n"
    "NGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jmlpPHr0O/KnPQtzI3eguhe0TwUem/eYSd\n"
    "yzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurcizaaA/L0HIgAmOit1GJA2s\n"
    "aMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFua39GLS99\n"
    "ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrn\n"
    "jndMoPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v\n"
    "//mU8eVkQaoANf0ZoMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtk\n"
    "rfa7ef+AUb69DNggq4mHQAYBp7L+k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQ\n"
    "NepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hLAoGBANDrr7xAJbqBjHVwIzQ4\n"
    "To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvAcJyRM9SJ7OKl\n"
    "Gt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
    "54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TF\n"
    "BVmD7fV0Zhov17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLa\n"
    "lpGSwomSNYJcB9HNMlmhkGzc1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9\n"
    "+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gILAuE4Pu13aKiJnfft7hIjbK+5kyb\n"
    "3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ2gG0N5hvJpzwwhbh\n"
    "XqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
    "-----END RSA PRIVATE KEY-----";

// Simple base64 decode using libsodium
static int b64_decode(const char *b64, uint8_t *out, size_t out_size,
                      size_t *out_len) {
  // Apple base64 may lack padding — libsodium handles that with _IGNORE variant
  if (sodium_base642bin(out, out_size, b64, strlen(b64), "\r\n \t", out_len,
                        NULL, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) != 0) {
    // Try with padding variant
    if (sodium_base642bin(out, out_size, b64, strlen(b64), "\r\n \t", out_len,
                          NULL, sodium_base64_VARIANT_ORIGINAL) != 0) {
      return -1;
    }
  }
  return 0;
}

// Simple base64 encode using libsodium, strip trailing '='
static int b64_encode(const uint8_t *data, size_t data_len, char *out,
                      size_t out_size) {
  char *result = sodium_bin2base64(out, out_size, data, data_len,
                                   sodium_base64_VARIANT_ORIGINAL_NO_PADDING);
  return result ? 0 : -1;
}

int rsa_apple_challenge_response(const char *challenge_b64, uint32_t ip_addr,
                                 const uint8_t mac[6], char *out_b64,
                                 size_t out_b64_size) {

  // Decode challenge
  uint8_t challenge[32];
  size_t challenge_len = 0;
  if (b64_decode(challenge_b64, challenge, sizeof(challenge), &challenge_len) !=
      0) {
    ESP_LOGE(TAG, "Failed to decode Apple-Challenge");
    return -1;
  }

  // Build response data: challenge + IP(4) + MAC(6), padded to 32 bytes
  uint8_t data[32];
  memset(data, 0, sizeof(data));
  size_t pos = 0;

  if (challenge_len > 22) {
    challenge_len = 22; // Max room for challenge if IP + MAC must fit
  }
  memcpy(data + pos, challenge, challenge_len);
  pos += challenge_len;

  memcpy(data + pos, &ip_addr, 4);
  pos += 4;

  memcpy(data + pos, mac, 6);
  // pos is now at most 32, rest is zero-padded

  // RSA PKCS1 v1.5 "private encrypt" — equivalent to OpenSSL's
  // RSA_private_encrypt(..., RSA_PKCS1_PADDING).
  // mbedtls_rsa_pkcs1_sign with MBEDTLS_MD_NONE applies type-1 padding
  // (0x00 0x01 0xFF..0xFF 0x00 <data>) then the private key operation.
  int rsa_len = 0;
  uint8_t *rsa_out = (uint8_t *)ap2_rsa(airplay_rsa_private_key, data, 32, &rsa_len, true);
  if (!rsa_out) return -1;
  int ret;

  // Base64 encode result without padding
  ret = b64_encode(rsa_out, rsa_len, out_b64, out_b64_size);
  free(rsa_out);

  if (ret != 0) {
    ESP_LOGE(TAG, "Failed to base64-encode RSA response");
    return -1;
  }

  return 0;
}

int rsa_decrypt_aes_key(const char *encrypted_b64, uint8_t *out_key,
                        size_t out_key_size, size_t *out_key_len) {

  // Decode the base64 RSA-encrypted key
  uint8_t encrypted[512];
  size_t encrypted_len = 0;
  if (b64_decode(encrypted_b64, encrypted, sizeof(encrypted), &encrypted_len) !=
      0) {
    ESP_LOGE(TAG, "Failed to decode RSA-encrypted AES key");
    return -1;
  }

  ESP_LOGI(TAG, "RSA-encrypted AES key: %zu bytes (expected 256)",
           encrypted_len);

  // RSA OAEP-SHA1 decrypt (RAOP uses OAEP padding for the AES key)
  int olen = 0;
  char *decoded = ap2_rsa(airplay_rsa_private_key, encrypted, encrypted_len, &olen, false);
  if (!decoded) return -1;
  if ((size_t)olen > out_key_size) { free(decoded); return -1; }
  memcpy(out_key, decoded, olen);
  free(decoded);
  *out_key_len = olen;
  ESP_LOGI(TAG, "Decrypted AES key: %zu bytes", olen);
  return 0;
}
