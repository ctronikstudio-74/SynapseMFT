#pragma once
#include <sodium.h>
#include <vector>
#include <stdexcept>
#include <cstring>

class LDPCrypto {
private:
    uint8_t secret_key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
public:
    LDPCrypto();
    void set_key(const uint8_t* key);
    const uint8_t* get_key() const;
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t* nonce, uint32_t seq_id);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, uint8_t* nonce, uint32_t seq_id);
};
