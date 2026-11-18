/* Minimal mbedTLS config used by build — duplicate of include/mbedtls/config_mini.h */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Basic crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_MD_C

/* Bignum and ECC */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECDH_C

/* Randomness */
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C

/* SSL/TLS core (client only) */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_TLS_CRYPT

/* Small utilities */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

/* No filesystem, no self-test, no time/date */
#define MBEDTLS_FS_IO 0
#define MBEDTLS_SELF_TEST 0
#define MBEDTLS_HAVE_TIME_DATE 0

/* Use platform helpers if present; we'll provide simple stubs */
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS 1

#endif /* MBEDTLS_CONFIG_H */
