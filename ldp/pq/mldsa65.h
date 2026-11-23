#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDP_MLDSA65_PUBLICKEY 1952
#define LDP_MLDSA65_SECRETKEY 4032
#define LDP_MLDSA65_SIGNATURE 3309

int ldp_mldsa65_available(void);
int ldp_mldsa65_keypair(uint8_t pk[LDP_MLDSA65_PUBLICKEY], uint8_t sk[LDP_MLDSA65_SECRETKEY]);
int ldp_mldsa65_sign(uint8_t sig[LDP_MLDSA65_SIGNATURE], const uint8_t* message, size_t message_len,
                     const uint8_t* context, size_t context_len, const uint8_t sk[LDP_MLDSA65_SECRETKEY]);
int ldp_mldsa65_verify(const uint8_t sig[LDP_MLDSA65_SIGNATURE], const uint8_t* message, size_t message_len,
                       const uint8_t* context, size_t context_len, const uint8_t pk[LDP_MLDSA65_PUBLICKEY]);

#ifdef __cplusplus
}
#endif
