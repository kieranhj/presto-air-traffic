// mbedTLS 3.6 (the Pico SDK's) for one thing: a TLS 1.2 client to the Fight or Flight Worker on
// Cloudflare, checking its certificate against src/roots.c. Elliptic-curve and RSA certificates,
// ECDHE key exchange, AES-GCM and ChaCha20-Poly1305. No TLS 1.3 (it needs PSA crypto) and no
// clock: certificates' dates aren't checked, only their signatures and names.
#pragma once

// platform: memory through lwIP's heap (in PSRAM: lwipopts.h), entropy from the RP2350's
// random number generator (pico_mbedtls.c)
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

// hashes and ciphers
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C

// public keys
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

// certificates
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

// TLS 1.2, client only
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384   // a server may send full-size records
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048    // our requests are a few hundred bytes

#define MBEDTLS_ERROR_C

// The SDK's lwIP glue (altcp_tls_mbedtls.c) reads fields mbedTLS 3 marks private, and a session's
// start time, which needs HAVE_TIME (time since boot is enough: without HAVE_TIME_DATE no
// certificate's dates are checked)
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_MS_TIME_ALT    // mbedtls_ms_time() is in net.c
