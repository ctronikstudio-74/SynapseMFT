#include "mlkem768.h"

#ifdef LDP_HAVE_MLKEM
extern "C" {
int PQCP_MLKEM_NATIVE_MLKEM768_keypair(uint8_t* pk, uint8_t* sk);
int PQCP_MLKEM_NATIVE_MLKEM768_enc(uint8_t* ct, uint8_t* ss, const uint8_t* pk);
int PQCP_MLKEM_NATIVE_MLKEM768_dec(uint8_t* ss, const uint8_t* ct, const uint8_t* sk);
}
#endif

extern "C" int ldp_mlkem768_available(void) {
#ifdef LDP_HAVE_MLKEM
    return 1;
#else
    return 0;
#endif
}

extern "C" int ldp_mlkem768_keypair(uint8_t pk[LDP_MLKEM768_PUBLICKEY], uint8_t sk[LDP_MLKEM768_SECRETKEY]) {
#ifdef LDP_HAVE_MLKEM
    return PQCP_MLKEM_NATIVE_MLKEM768_keypair(pk, sk);
#else
    (void)pk;
    (void)sk;
    return -1;
#endif
}

extern "C" int ldp_mlkem768_enc(uint8_t ct[LDP_MLKEM768_CIPHERTEXT], uint8_t ss[LDP_MLKEM768_SHARED],
                               const uint8_t pk[LDP_MLKEM768_PUBLICKEY]) {
#ifdef LDP_HAVE_MLKEM
    return PQCP_MLKEM_NATIVE_MLKEM768_enc(ct, ss, pk);
#else
    (void)ct;
    (void)ss;
    (void)pk;
    return -1;
#endif
}

extern "C" int ldp_mlkem768_dec(uint8_t ss[LDP_MLKEM768_SHARED], const uint8_t ct[LDP_MLKEM768_CIPHERTEXT],
                               const uint8_t sk[LDP_MLKEM768_SECRETKEY]) {
#ifdef LDP_HAVE_MLKEM
    return PQCP_MLKEM_NATIVE_MLKEM768_dec(ss, ct, sk);
#else
    (void)ss;
    (void)ct;
    (void)sk;
    return -1;
#endif
}
