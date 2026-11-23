#include "LDPCrypto.h"
#include <stdexcept>
#include <cstring>

LDPCrypto::LDPCrypto() {
    if (sodium_init() < 0) {
        throw std::runtime_error("Sodium init failed");
    }
    // Generate random key if not set
    randombytes_buf(secret_key, sizeof(secret_key));
}

void LDPCrypto::set_key(const uint8_t* key) {
    std::memcpy(secret_key, key, sizeof(secret_key));
}

const uint8_t* LDPCrypto::get_key() const {
    return secret_key;
}

std::vector<uint8_t> LDPCrypto::encrypt(const std::vector<uint8_t>& plaintext, uint8_t* nonce, uint32_t seq_id) {
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

std::vector<uint8_t> LDPCrypto::decrypt(const std::vector<uint8_t>& ciphertext, uint8_t* nonce, uint32_t seq_id) {
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

