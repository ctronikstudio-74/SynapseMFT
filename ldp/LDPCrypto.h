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
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t* nonce, uint32_t seq_id) {
        std::vector<uint8_t> ciphertext(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        unsigned long long clen;
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            ciphertext.data(), &clen,
            plaintext.data(), plaintext.size(),
            (uint8_t*)&seq_id, sizeof(seq_id),
            nullptr, nonce, secret_key
        );
        ciphertext.resize(clen);
        return ciphertext;
    }
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, uint8_t* nonce, uint32_t seq_id) {
        std::vector<uint8_t> plaintext(ciphertext.size());
        unsigned long long plen;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plaintext.data(), &plen,
            nullptr,
            ciphertext.data(), ciphertext.size(),
            (uint8_t*)&seq_id, sizeof(seq_id),
            nonce, secret_key
        ) != 0) {
            throw std::runtime_error("Decryption failed");
        }
        plaintext.resize(plen);
        return plaintext;
    }
};
