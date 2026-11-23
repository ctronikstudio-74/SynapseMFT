#ifndef LDP_MLKEM_CONFIG_H
#define LDP_MLKEM_CONFIG_H

/* Build of pq-code-package/mlkem-native for ML-KEM-768 (FIPS 203).
 * Randomness comes from libsodium. This is not a CMVP-validated module.
 */

#define MLK_CONFIG_PARAMETER_SET 768
#define MLK_CONFIG_CUSTOM_RANDOMBYTES

#if !defined(__ASSEMBLER__)
#include <stddef.h>
#include <stdint.h>
#include <sodium.h>

static inline int mlk_randombytes(uint8_t* ptr, size_t len) {
    randombytes_buf(ptr, len);
    return 0;
}
#endif

#include "mlkem_native_config.h"

#endif
