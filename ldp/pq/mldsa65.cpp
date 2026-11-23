#include "mldsa65.h"

#ifdef LDP_HAVE_MLDSA
extern "C" {
int PQCP_MLDSA_NATIVE_MLDSA65_keypair(uint8_t* pk, uint8_t* sk);
int PQCP_MLDSA_NATIVE_MLDSA65_signature(uint8_t* sig, const uint8_t* message, size_t message_len,
                                        const uint8_t* context, size_t context_len, const uint8_t* sk);
int PQCP_MLDSA_NATIVE_MLDSA65_verify(const uint8_t* sig, const uint8_t* message, size_t message_len,
                                     const uint8_t* context, size_t context_len, const uint8_t* pk);
}
#endif

extern "C" int ldp_mldsa65_available(void) {
#ifdef LDP_HAVE_MLDSA
    return 1;
#else
    return 0;
#endif
}

extern "C" int ldp_mldsa65_keypair(uint8_t pk[LDP_MLDSA65_PUBLICKEY], uint8_t sk[LDP_MLDSA65_SECRETKEY]) {
#ifdef LDP_HAVE_MLDSA
    return PQCP_MLDSA_NATIVE_MLDSA65_keypair(pk, sk);
#else
    (void)pk;
    (void)sk;
    return -1;
#endif
}

extern "C" int ldp_mldsa65_sign(uint8_t sig[LDP_MLDSA65_SIGNATURE], const uint8_t* message, size_t message_len,
                               const uint8_t* context, size_t context_len, const uint8_t sk[LDP_MLDSA65_SECRETKEY]) {
#ifdef LDP_HAVE_MLDSA
    return PQCP_MLDSA_NATIVE_MLDSA65_signature(sig, message, message_len, context, context_len, sk);
#else
    (void)sig;
    (void)message;
    (void)message_len;
    (void)context;
    (void)context_len;
    (void)sk;
    return -1;
#endif
}

extern "C" int ldp_mldsa65_verify(const uint8_t sig[LDP_MLDSA65_SIGNATURE], const uint8_t* message, size_t message_len,
                                 const uint8_t* context, size_t context_len, const uint8_t pk[LDP_MLDSA65_PUBLICKEY]) {
#ifdef LDP_HAVE_MLDSA
    return PQCP_MLDSA_NATIVE_MLDSA65_verify(sig, message, message_len, context, context_len, pk);
#else
    (void)sig;
    (void)message;
    (void)message_len;
    (void)context;
    (void)context_len;
    (void)pk;
    return -1;
#endif
}
