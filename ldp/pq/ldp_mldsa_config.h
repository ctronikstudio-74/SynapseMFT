#ifndef LDP_MLDSA_CONFIG_H
#define LDP_MLDSA_CONFIG_H

/* Build of pq-code-package/mldsa-native for ML-DSA-65 (FIPS 204).
 * Randomness comes from libsodium. This is not a CMVP-validated module.
 */

#define MLD_CONFIG_PARAMETER_SET 65
#define MLD_CONFIG_CUSTOM_RANDOMBYTES

#if !defined(__ASSEMBLER__)
#include <stddef.h>
#include <stdint.h>
#include <sodium.h>

static inline int mld_randombytes(uint8_t* ptr, size_t len) {
    randombytes_buf(ptr, len);
    return 0;
}
#endif

#include "mldsa_native_config.h"

#endif
