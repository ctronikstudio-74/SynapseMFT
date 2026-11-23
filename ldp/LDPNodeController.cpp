#include "LDPNodeController.h"
#include <cstdlib>
#include <cstring>

void LDPNodeController::handle_emergency_signal(const uint8_t* signed_signal, size_t len, 
                                                 const uint8_t* emergency_payload, size_t payload_len) {
    if (crypto_sign_verify_detached(signed_signal, emergency_payload, payload_len, admin_public_key) == 0) {
        trigger_kill_switch();
    }
}

void LDPNodeController::trigger_kill_switch() {
    is_compromised = true;
    sodium_memzero(session_keys, sizeof(session_keys));
    std::exit(EXIT_FAILURE);
}

