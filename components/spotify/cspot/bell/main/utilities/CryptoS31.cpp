// ESP-IDF 6 / TF-PSA-Crypto adapter. Spotify's legacy 768-bit DH group
// needs MPI arithmetic; all symmetric cryptography uses public PSA APIs.
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "Crypto.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include <mbedtls/base64.h>
#include <mbedtls/private/bignum.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
void check(int status, const char *operation) {
  if (status != 0) throw std::runtime_error(std::string(operation) + ": " + std::to_string(status));
}
struct Key {
  mbedtls_svc_key_id_t id = 0;
  Key(const std::vector<uint8_t>& bytes, psa_key_type_t type,
      psa_algorithm_t alg, psa_key_usage_t usage) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    const auto status = psa_import_key(&attributes, bytes.data(), bytes.size(), &id);
    psa_reset_key_attributes(&attributes);
    check(status, "PSA key import");
  }
  ~Key() { psa_destroy_key(id); }
  Key(const Key&) = delete;
  Key& operator=(const Key&) = delete;
};
struct Cipher {
  psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
  ~Cipher() { psa_cipher_abort(&op); }
};
struct Derivation {
  psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
  ~Derivation() { psa_key_derivation_abort(&op); }
};
struct Mpi {
  mbedtls_mpi value;
  Mpi() { mbedtls_mpi_init(&value); }
  ~Mpi() { mbedtls_mpi_free(&value); }
  Mpi(const Mpi&) = delete;
  Mpi& operator=(const Mpi&) = delete;
};

std::vector<uint8_t> dhPower(const std::vector<uint8_t>& base,
                             const std::vector<uint8_t>& exponent) {
  if (base.empty() || base.size() > DH_KEY_SIZE || exponent.size() != DH_KEY_SIZE)
    throw std::runtime_error("Invalid Spotify DH key length");
  Mpi p, b, x, result, upper;
  check(mbedtls_mpi_read_binary(&p.value, DHPrime, sizeof(DHPrime)), "DH prime");
  check(mbedtls_mpi_read_binary(&b.value, base.data(), base.size()), "DH public key");
  check(mbedtls_mpi_sub_int(&upper.value, &p.value, 1), "DH range");
  if (mbedtls_mpi_cmp_int(&b.value, 1) <= 0 || mbedtls_mpi_cmp_mpi(&b.value, &upper.value) >= 0)
    throw std::runtime_error("Spotify DH public key outside valid range");
  check(mbedtls_mpi_read_binary(&x.value, exponent.data(), exponent.size()), "DH private key");
  check(mbedtls_mpi_exp_mod(&result.value, &b.value, &x.value, &p.value, nullptr), "DH exponentiation");
  std::vector<uint8_t> output(DH_KEY_SIZE);
  check(mbedtls_mpi_write_binary(&result.value, output.data(), output.size()), "DH output");
  return output;
}
}

CryptoMbedTLS::CryptoMbedTLS() {
  check(psa_crypto_init(), "PSA init");
  mbedtls_md_init(&sha1Context);
}
CryptoMbedTLS::~CryptoMbedTLS() { mbedtls_md_free(&sha1Context); }

std::vector<uint8_t> CryptoMbedTLS::base64Decode(const std::string& data) {
  size_t size = 0;
  auto status = mbedtls_base64_decode(nullptr, 0, &size,
      reinterpret_cast<const unsigned char *>(data.data()), data.size());
  if (status != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) check(status, "Base64 decode size");
  std::vector<uint8_t> output(size);
  check(mbedtls_base64_decode(output.data(), output.size(), &size,
      reinterpret_cast<const unsigned char *>(data.data()), data.size()), "Base64 decode");
  output.resize(size);
  return output;
}
std::string CryptoMbedTLS::base64Encode(const std::vector<uint8_t>& data) {
  size_t size = 0;
  auto status = mbedtls_base64_encode(nullptr, 0, &size, data.data(), data.size());
  if (status != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) check(status, "Base64 encode size");
  std::vector<uint8_t> output(size);
  check(mbedtls_base64_encode(output.data(), output.size(), &size, data.data(), data.size()), "Base64 encode");
  return std::string(output.begin(), output.begin() + size);
}

void CryptoMbedTLS::sha1Init() {
  mbedtls_md_free(&sha1Context);
  mbedtls_md_init(&sha1Context);
  check(mbedtls_md_setup(&sha1Context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 0), "SHA1 setup");
  check(mbedtls_md_starts(&sha1Context), "SHA1 start");
}
void CryptoMbedTLS::sha1Update(const std::string& data) {
  check(mbedtls_md_update(&sha1Context, reinterpret_cast<const unsigned char *>(data.data()), data.size()), "SHA1 update");
}
void CryptoMbedTLS::sha1Update(const std::vector<uint8_t>& data) {
  check(mbedtls_md_update(&sha1Context, data.data(), data.size()), "SHA1 update");
}
std::vector<uint8_t> CryptoMbedTLS::sha1FinalBytes() {
  std::vector<uint8_t> digest(20);
  check(mbedtls_md_finish(&sha1Context, digest.data()), "SHA1 finish");
  mbedtls_md_free(&sha1Context);
  mbedtls_md_init(&sha1Context);
  return digest;
}
std::string CryptoMbedTLS::sha1Final() {
  const auto bytes = sha1FinalBytes();
  return std::string(bytes.begin(), bytes.end());
}
std::vector<uint8_t> CryptoMbedTLS::sha1HMAC(const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& message) {
  std::vector<uint8_t> output(20);
  check(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key.data(), key.size(),
      message.data(), message.size(), output.data()), "HMAC SHA1");
  return output;
}

void CryptoMbedTLS::aesCTRXcrypt(const std::vector<uint8_t>& key, std::vector<uint8_t>& iv,
                                uint8_t *buffer, size_t nbytes) {
  if (iv.size() != 16) throw std::runtime_error("AES CTR requires a 16-byte IV");
  if (!nbytes) return;
  Key imported(key, PSA_KEY_TYPE_AES, PSA_ALG_CTR, PSA_KEY_USAGE_ENCRYPT);
  Cipher cipher;
  check(psa_cipher_encrypt_setup(&cipher.op, imported.id, PSA_ALG_CTR), "AES CTR setup");
  check(psa_cipher_set_iv(&cipher.op, iv.data(), iv.size()), "AES CTR IV");
  // A distinct output buffer also supports implementations without in-place update.
  std::vector<uint8_t> output(nbytes + 16);
  size_t written = 0, final = 0;
  check(psa_cipher_update(&cipher.op, buffer, nbytes, output.data(), output.size(), &written), "AES CTR update");
  check(psa_cipher_finish(&cipher.op, output.data() + written, output.size() - written, &final), "AES CTR finish");
  if (written + final != nbytes) throw std::runtime_error("AES CTR output length");
  std::memcpy(buffer, output.data(), nbytes);
  // Preserve the old mbedtls_aes_crypt_ctr contract: advance the big-endian
  // counter for every consumed block, including a partial final block.
  size_t carry = nbytes / 16 + (nbytes % 16 != 0);
  for (int i = 15; i >= 0 && carry; --i) {
    carry += iv[i];
    iv[i] = static_cast<uint8_t>(carry);
    carry >>= 8;
  }
}
void CryptoMbedTLS::aesECBdecrypt(const std::vector<uint8_t>& key, std::vector<uint8_t>& data) {
  if (data.size() % 16) throw std::runtime_error("AES ECB requires complete blocks");
  if (data.empty()) return;
  Key imported(key, PSA_KEY_TYPE_AES, PSA_ALG_ECB_NO_PADDING, PSA_KEY_USAGE_DECRYPT);
  std::vector<uint8_t> output(data.size());
  size_t written = 0;
  check(psa_cipher_decrypt(imported.id, PSA_ALG_ECB_NO_PADDING, data.data(), data.size(),
      output.data(), output.size(), &written), "AES ECB decrypt");
  if (written != data.size()) throw std::runtime_error("AES ECB output length");
  data.swap(output);
}
std::vector<uint8_t> CryptoMbedTLS::pbkdf2HmacSha1(const std::vector<uint8_t>& password,
    const std::vector<uint8_t>& salt, int iterations, int digestSize) {
  if (iterations <= 0 || digestSize <= 0) throw std::runtime_error("Invalid PBKDF2 parameters");
  Derivation derivation;
  check(psa_key_derivation_setup(&derivation.op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_1)), "PBKDF2 setup");
  check(psa_key_derivation_input_integer(&derivation.op, PSA_KEY_DERIVATION_INPUT_COST, iterations), "PBKDF2 cost");
  check(psa_key_derivation_input_bytes(&derivation.op, PSA_KEY_DERIVATION_INPUT_SALT, salt.data(), salt.size()), "PBKDF2 salt");
  check(psa_key_derivation_input_bytes(&derivation.op, PSA_KEY_DERIVATION_INPUT_PASSWORD, password.data(), password.size()), "PBKDF2 password");
  std::vector<uint8_t> output(digestSize);
  check(psa_key_derivation_output_bytes(&derivation.op, output.data(), output.size()), "PBKDF2 output");
  return output;
}
void CryptoMbedTLS::dhInit() {
  privateKey = generateVectorWithRandomData(DH_KEY_SIZE);
  publicKey = dhPower({2}, privateKey);
}
std::vector<uint8_t> CryptoMbedTLS::dhCalculateShared(const std::vector<uint8_t>& remoteKey) {
  return dhPower(remoteKey, privateKey);
}
std::vector<uint8_t> CryptoMbedTLS::generateVectorWithRandomData(size_t length) {
  std::vector<uint8_t> output(length);
  check(psa_generate_random(output.data(), output.size()), "PSA random");
  return output;
}
#endif
