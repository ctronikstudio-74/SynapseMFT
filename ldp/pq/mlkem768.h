#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDP_MLKEM768_PUBLICKEY 1184
#define LDP_MLKEM768_SECRETKEY 2400
#define LDP_MLKEM768_CIPHERTEXT 1088
#define LDP_MLKEM768_SHARED 32

int ldp_mlkem768_available(void);
int ldp_mlkem768_keypair(uint8_t pk[LDP_MLKEM768_PUBLICKEY], uint8_t sk[LDP_MLKEM768_SECRETKEY]);
int ldp_mlkem768_enc(uint8_t ct[LDP_MLKEM768_CIPHERTEXT], uint8_t ss[LDP_MLKEM768_SHARED], const uint8_t pk[LDP_MLKEM768_PUBLICKEY]);
int ldp_mlkem768_dec(uint8_t ss[LDP_MLKEM768_SHARED], const uint8_t ct[LDP_MLKEM768_CIPHERTEXT], const uint8_t sk[LDP_MLKEM768_SECRETKEY]);

#ifdef __cplusplus
}
#endif
