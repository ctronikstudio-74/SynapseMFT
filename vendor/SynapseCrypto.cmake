# In-tree copies of the crypto libraries used by LDP-1, the SDK and the agentic bus.
# Prefer this tree. The gitignored third_party/ checkout remains a fallback.

set(SYNAPSE_CRYPTO_READY FALSE)
set(_synapse_vendor "${CMAKE_CURRENT_LIST_DIR}")

if(EXISTS "${_synapse_vendor}/libsodium/include/sodium.h"
   AND EXISTS "${_synapse_vendor}/libsodium/win-x64/libsodium.lib"
   AND EXISTS "${_synapse_vendor}/mlkem/mlkem_native.c"
   AND EXISTS "${_synapse_vendor}/mldsa/mldsa_native.c")
    set(SODIUM_INCLUDE "${_synapse_vendor}/libsodium/include")
    set(SODIUM_LIBRARY "${_synapse_vendor}/libsodium/win-x64/libsodium.lib")
    set(SODIUM_DLL "${_synapse_vendor}/libsodium/win-x64/libsodium.dll")
    set(MLKEM_NATIVE "${_synapse_vendor}/mlkem")
    set(MLDSA_NATIVE "${_synapse_vendor}/mldsa")
    set(SYNAPSE_CRYPTO_READY TRUE)
else()
    foreach(_cand "${_synapse_vendor}/../third_party" "${_synapse_vendor}/../ldp/third_party")
        if(EXISTS "${_cand}/libsodium-msvc/libsodium/include/sodium.h"
           AND EXISTS "${_cand}/mlkem-native/mlkem/mlkem_native.c"
           AND EXISTS "${_cand}/mldsa-native/mldsa/mldsa_native.c")
            set(SODIUM_INCLUDE "${_cand}/libsodium-msvc/libsodium/include")
            set(SODIUM_LIBRARY "${_cand}/libsodium-msvc/libsodium/x64/Release/v143/dynamic/libsodium.lib")
            set(SODIUM_DLL "${_cand}/libsodium-msvc/libsodium/x64/Release/v143/dynamic/libsodium.dll")
            set(MLKEM_NATIVE "${_cand}/mlkem-native/mlkem")
            set(MLDSA_NATIVE "${_cand}/mldsa-native/mldsa")
            set(SYNAPSE_CRYPTO_READY TRUE)
            break()
        endif()
    endforeach()
endif()
