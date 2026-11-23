#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>

// LDP-1 session: versioned preamble, one AEAD (XChaCha20-Poly1305),
// selective ACK with retransmission, and a negotiated rekey.
// Profile::HybridPq1 mixes X25519 with ML-KEM-768 (FIPS 203) before HKDF
// and requires pinned ML-DSA-65 (FIPS 204) identity keys.
// The linked code implements those algorithms. It is not a CMVP-validated module.

class Ldp1Transport {
public:
    virtual ~Ldp1Transport() = default;
    virtual void send(std::vector<uint8_t> datagram) = 0;
    virtual bool recv(std::vector<uint8_t>& datagram, int timeout_ms) = 0;
};

class Ldp1Session {
public:
    enum class Role { Client, Server };
    enum class Profile { Classic, HybridPq1 };

    static constexpr uint32_t kWindow = 8;
    static constexpr size_t kMlkemPublicKey = 1184;
    static constexpr size_t kMlkemSecretKey = 2400;
    static constexpr size_t kMlkemCiphertext = 1088;
    static constexpr size_t kMlkemShared = 32;
    static constexpr size_t kMldsaPublicKey = 1952;
    static constexpr size_t kMldsaSecretKey = 4032;
    static constexpr size_t kMldsaSignature = 3309;

    Ldp1Session(Role role, Profile profile, Ldp1Transport& transport, int ack_timeout_ms = 500);
    ~Ldp1Session();

    Ldp1Session(const Ldp1Session&) = delete;
    Ldp1Session& operator=(const Ldp1Session&) = delete;

    // Hybrid profile only. secret_key is this node's ML-DSA-65 key.
    // peer_public_key is pinned out of band: it is not learned from the handshake.
    void set_identity(const uint8_t secret_key[kMldsaSecretKey], const uint8_t peer_public_key[kMldsaPublicKey]);

    void handshake();
    void send_all(const std::vector<uint8_t>& plaintext, size_t chunk_size, uint32_t rekey_every);
    std::vector<uint8_t> receive_all(int idle_timeout_ms);

    bool established() const { return established_; }
    Profile profile() const { return profile_; }

private:
    struct Epoch {
        uint8_t key[32];
        uint32_t from_seq;
    };

    void send_client_hello();
    void read_server_hello();
    void read_client_hello();
    void send_server_hello();
    void derive_keys(const uint8_t x25519_a[32], const uint8_t x25519_b[32],
                     const uint8_t* kem_ss, size_t kem_ss_len);
    void install_epoch(std::vector<Epoch>& epochs, const uint8_t key[32], uint32_t from_seq);
    const Epoch& epoch_for(const std::vector<Epoch>& epochs, uint32_t seq) const;
    void wipe_epochs_before(std::vector<Epoch>& epochs, uint32_t seq);

    std::vector<uint8_t> seal(uint8_t type, const uint8_t key[32],
                              const uint8_t* aad, size_t aad_len,
                              const uint8_t* plain, size_t plain_len,
                              const uint8_t nonce[24]);
    std::vector<uint8_t> open(const uint8_t key[32],
                              const uint8_t* aad, size_t aad_len,
                              const uint8_t nonce[24],
                              const uint8_t* cipher, size_t cipher_len);

    void send_data(uint32_t seq, const std::vector<uint8_t>& chunk);
    void send_ack(uint32_t base, uint64_t bitmap);
    std::vector<uint8_t> send_rekey(uint32_t activate_at);
    void send_rekey_ack(uint32_t activate_at);
    void send_close(const uint8_t hash[32]);
    void sign_identity(uint8_t signature[kMldsaSignature], const uint8_t* message, size_t message_len,
                       const char* context) const;
    void verify_peer(const uint8_t signature[kMldsaSignature], const uint8_t* message, size_t message_len,
                     const char* context) const;
    void bind_transcript(const std::vector<uint8_t>& client_auth);

    bool handle_during_send(const std::vector<uint8_t>& datagram, uint32_t& ack_base, uint64_t& ack_bitmap, bool& got_ack, bool& got_rekey_ack, uint32_t& rekey_ack_at);

    std::vector<uint8_t> preamble(uint8_t type) const;

    Role role_;
    Profile profile_;
    Ldp1Transport& transport_;
    int ack_timeout_ms_;
    bool established_ = false;
    bool hybrid_ = false;

    uint8_t client_tx_[32]{};
    uint8_t server_tx_[32]{};
    std::vector<Epoch> data_epochs_;
    uint8_t kem_secret_[kMlkemSecretKey]{};
    bool kem_secret_live_ = false;
    uint8_t identity_sk_[kMldsaSecretKey]{};
    uint8_t peer_pk_[kMldsaPublicKey]{};
    bool identity_set_ = false;
    std::vector<uint8_t> client_hello_;
    std::vector<uint8_t> server_hello_;
    uint8_t transcript_hash_[32]{};
};
