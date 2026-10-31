# Extend the existing Sendspin/Noise library, not a second libsodium ABI.
# sign.c supplies the same _crypto_sign_ed25519_ref10_hinit used by open.c.
idf_component_get_property(ap2_sodium esphome__libsodium COMPONENT_LIB)
idf_component_get_property(ap2_sodium_dir esphome__libsodium COMPONENT_DIR)
get_target_property(ap2_sodium_sources ${ap2_sodium} SOURCES)
list(FILTER ap2_sodium_sources EXCLUDE REGEX "(^|/)ed25519_ref10_hinit.c$")
set_property(TARGET ${ap2_sodium} PROPERTY SOURCES "${ap2_sodium_sources}")
target_sources(${ap2_sodium} PRIVATE
    "${ap2_sodium_dir}/libsodium/src/libsodium/crypto_sign/ed25519/ref10/sign.c"
    "${ap2_sodium_dir}/libsodium/src/libsodium/crypto_sign/ed25519/ref10/keypair.c"
    "${ap2_sodium_dir}/libsodium/src/libsodium/crypto_auth/hmacsha512/auth_hmacsha512.c")
