#pragma once
#include <sodium.h>
#include <atomic>

class LDPNodeController {
    std::atomic<bool> is_compromised{false};
    uint8_t admin_public_key[crypto_sign_PUBLICKEYBYTES];
    uint8_t session_keys[64];
public:
    void handle_emergency_signal(const uint8_t* signed_signal, size_t len, 
                                 const uint8_t* emergency_payload, size_t payload_len);
private:
    void trigger_kill_switch();
};
