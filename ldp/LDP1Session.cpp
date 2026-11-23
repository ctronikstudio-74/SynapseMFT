#include "LDP1Session.h"
#include "pq/mlkem768.h"
#include "pq/mldsa65.h"

#include <sodium.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

constexpr uint8_t kClientHello = 1;
constexpr uint8_t kServerHello = 2;
constexpr uint8_t kData = 3;
constexpr uint8_t kAck = 4;
constexpr uint8_t kRekey = 5;
constexpr uint8_t kClose = 6;
constexpr uint8_t kClientAuth = 7;
constexpr uint8_t kRekeyAck = 8;
constexpr char kServerIdentityContext[] = "ldp1-server-hello";
constexpr char kClientIdentityContext[] = "ldp1-client-auth";
constexpr char kCloseIdentityContext[] = "ldp1-close";
constexpr size_t kPreamble = 8;
constexpr size_t kNonce = 24;
constexpr size_t kKey = 32;

void require(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

void write_be32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    out[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[3] = static_cast<uint8_t>(value & 0xff);
}

uint32_t read_be32(const uint8_t* in) {
    return (uint32_t(in[0]) << 24) | (uint32_t(in[1]) << 16) | (uint32_t(in[2]) << 8) | uint32_t(in[3]);
}

void write_be64(uint8_t* out, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out[i] = static_cast<uint8_t>(value & 0xff);
        value >>= 8;
    }
}

uint64_t read_be64(const uint8_t* in) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | in[i];
    }
    return value;
}

void ensure_sodium() {
    static const int ready = sodium_init();
    require(ready >= 0, "sodium_init failed");
}

}  // namespace

Ldp1Session::Ldp1Session(Role role, Profile profile, Ldp1Transport& transport, int ack_timeout_ms)
    : role_(role), profile_(profile), transport_(transport), ack_timeout_ms_(ack_timeout_ms),
      hybrid_(profile == Profile::HybridPq1) {
    ensure_sodium();
}

Ldp1Session::~Ldp1Session() {
    sodium_memzero(client_tx_, sizeof(client_tx_));
    sodium_memzero(server_tx_, sizeof(server_tx_));
    for (auto& epoch : data_epochs_) {
        sodium_memzero(epoch.key, sizeof(epoch.key));
    }
    if (kem_secret_live_) {
        sodium_memzero(kem_secret_, sizeof(kem_secret_));
    }
    sodium_memzero(identity_sk_, sizeof(identity_sk_));
    sodium_memzero(peer_pk_, sizeof(peer_pk_));
}

void Ldp1Session::set_identity(const uint8_t secret_key[kMldsaSecretKey], const uint8_t peer_public_key[kMldsaPublicKey]) {
    require(hybrid_, "identity belongs to the hybrid profile");
    require(ldp_mldsa65_available() == 1, "ML-DSA-65 is not linked into this build");
    std::memcpy(identity_sk_, secret_key, sizeof(identity_sk_));
    std::memcpy(peer_pk_, peer_public_key, sizeof(peer_pk_));
    identity_set_ = true;
}

void Ldp1Session::sign_identity(uint8_t signature[kMldsaSignature], const uint8_t* message, size_t message_len,
                               const char* context) const {
    require(identity_set_, "hybrid profile requires a pinned identity");
    require(ldp_mldsa65_sign(signature, message, message_len, reinterpret_cast<const uint8_t*>(context),
                             std::strlen(context), identity_sk_) == 0,
            "ml-dsa sign failed");
}

void Ldp1Session::verify_peer(const uint8_t signature[kMldsaSignature], const uint8_t* message, size_t message_len,
                             const char* context) const {
    require(identity_set_, "hybrid profile requires a pinned identity");
    require(ldp_mldsa65_verify(signature, message, message_len, reinterpret_cast<const uint8_t*>(context),
                               std::strlen(context), peer_pk_) == 0,
            "identity signature rejected");
}

void Ldp1Session::bind_transcript(const std::vector<uint8_t>& client_auth) {
    std::vector<uint8_t> transcript;
    transcript.insert(transcript.end(), client_hello_.begin(), client_hello_.end());
    transcript.insert(transcript.end(), server_hello_.begin(), server_hello_.end());
    transcript.insert(transcript.end(), client_auth.begin(), client_auth.end());
    crypto_hash_sha256(transcript_hash_, transcript.data(), transcript.size());
}

std::vector<uint8_t> Ldp1Session::preamble(uint8_t type) const {
    return {'L', 'D', 'P', '1', 1, 0, static_cast<uint8_t>(hybrid_ ? 1 : 0), type};
}

void Ldp1Session::install_epoch(std::vector<Epoch>& epochs, const uint8_t key[32], uint32_t from_seq) {
    Epoch epoch{};
    std::memcpy(epoch.key, key, 32);
    epoch.from_seq = from_seq;
    epochs.push_back(epoch);
    std::sort(epochs.begin(), epochs.end(), [](const Epoch& a, const Epoch& b) {
        return a.from_seq < b.from_seq;
    });
}

const Ldp1Session::Epoch& Ldp1Session::epoch_for(const std::vector<Epoch>& epochs, uint32_t seq) const {
    require(!epochs.empty(), "no data key");
    const Epoch* chosen = &epochs.front();
    for (const auto& epoch : epochs) {
        if (epoch.from_seq <= seq) {
            chosen = &epoch;
        }
    }
    return *chosen;
}

void Ldp1Session::wipe_epochs_before(std::vector<Epoch>& epochs, uint32_t seq) {
    if (epochs.size() < 2) {
        return;
    }
    size_t keep = 0;
    for (size_t i = 0; i < epochs.size(); ++i) {
        if (epochs[i].from_seq <= seq) {
            keep = i;
        }
    }
    for (size_t i = 0; i < keep; ++i) {
        sodium_memzero(epochs[i].key, sizeof(epochs[i].key));
    }
    epochs.erase(epochs.begin(), epochs.begin() + static_cast<std::ptrdiff_t>(keep));
}

std::vector<uint8_t> Ldp1Session::seal(uint8_t type, const uint8_t key[32],
                                      const uint8_t* aad, size_t aad_len,
                                      const uint8_t* plain, size_t plain_len,
                                      const uint8_t nonce[24]) {
    auto header = preamble(type);
    std::vector<uint8_t> bound;
    bound.reserve(header.size() + aad_len);
    bound.insert(bound.end(), header.begin(), header.end());
    bound.insert(bound.end(), aad, aad + aad_len);

    std::vector<uint8_t> cipher(plain_len + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long clen = 0;
    require(crypto_aead_xchacha20poly1305_ietf_encrypt(
                cipher.data(), &clen, plain, plain_len, bound.data(), bound.size(),
                nullptr, nonce, key) == 0,
            "aead encrypt failed");
    cipher.resize(static_cast<size_t>(clen));
    return cipher;
}

std::vector<uint8_t> Ldp1Session::open(const uint8_t key[32], const uint8_t* aad, size_t aad_len,
                                      const uint8_t nonce[24], const uint8_t* cipher, size_t cipher_len) {
    std::vector<uint8_t> plain(cipher_len);
    unsigned long long plen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plain.data(), &plen, nullptr, cipher, cipher_len, aad, aad_len, nonce, key) != 0) {
        throw std::runtime_error("aead decrypt failed");
    }
    plain.resize(static_cast<size_t>(plen));
    return plain;
}

void Ldp1Session::derive_keys(const uint8_t x25519_a[32], const uint8_t x25519_b[32],
                             const uint8_t* kem_ss, size_t kem_ss_len) {
    std::vector<uint8_t> ikm(64 + kem_ss_len);
    std::memcpy(ikm.data(), x25519_a, 32);
    std::memcpy(ikm.data() + 32, x25519_b, 32);
    if (kem_ss_len > 0) {
        std::memcpy(ikm.data() + 64, kem_ss, kem_ss_len);
    }
    uint8_t prk[crypto_kdf_hkdf_sha256_KEYBYTES];
    require(crypto_kdf_hkdf_sha256_extract(prk, nullptr, 0, ikm.data(), ikm.size()) == 0, "hkdf extract failed");
    require(crypto_kdf_hkdf_sha256_expand(client_tx_, 32, "ldp1-client-tx", 14, prk) == 0, "hkdf expand failed");
    require(crypto_kdf_hkdf_sha256_expand(server_tx_, 32, "ldp1-server-tx", 14, prk) == 0, "hkdf expand failed");
    sodium_memzero(prk, sizeof(prk));
    sodium_memzero(ikm.data(), ikm.size());
    install_epoch(data_epochs_, client_tx_, 0);
}

void Ldp1Session::send_client_hello() {
    if (hybrid_) {
        require(identity_set_, "hybrid profile requires a pinned identity");
        require(ldp_mlkem768_available() == 1, "ML-KEM-768 is not linked into this build");
    }
    uint8_t pk[crypto_kx_PUBLICKEYBYTES];
    uint8_t sk[crypto_kx_SECRETKEYBYTES];
    require(crypto_kx_keypair(pk, sk) == 0, "x25519 keypair failed");

    auto datagram = preamble(kClientHello);
    datagram.insert(datagram.end(), pk, pk + sizeof(pk));

    uint8_t kem_pk[kMlkemPublicKey];
    if (hybrid_) {
        require(ldp_mlkem768_keypair(kem_pk, kem_secret_) == 0, "ml-kem keypair failed");
        kem_secret_live_ = true;
        datagram.insert(datagram.end(), kem_pk, kem_pk + sizeof(kem_pk));
    }
    client_hello_ = datagram;
    transport_.send(std::move(datagram));

    std::vector<uint8_t> reply;
    require(transport_.recv(reply, ack_timeout_ms_ * 4), "server hello timeout");
    require(reply.size() >= kPreamble + crypto_kx_PUBLICKEYBYTES, "server hello truncated");
    require(std::memcmp(reply.data(), "LDP1", 4) == 0, "bad magic");
    require(reply[7] == kServerHello, "expected server hello");
    require((reply[6] & 1) == (hybrid_ ? 1 : 0), "profile mismatch");

    uint8_t server_pk[crypto_kx_PUBLICKEYBYTES];
    std::memcpy(server_pk, reply.data() + kPreamble, sizeof(server_pk));
    uint8_t rx[32], tx[32];
    require(crypto_kx_client_session_keys(rx, tx, pk, sk, server_pk) == 0, "x25519 client failed");

    uint8_t kem_ss[kMlkemShared];
    size_t kem_len = 0;
    if (hybrid_) {
        const size_t signed_prefix = kPreamble + crypto_kx_PUBLICKEYBYTES + kMlkemCiphertext;
        require(reply.size() == signed_prefix + kMldsaSignature, "server hello kem size");
        std::vector<uint8_t> signed_hello(reply.begin(), reply.begin() + static_cast<std::ptrdiff_t>(signed_prefix));
        std::vector<uint8_t> transcript = client_hello_;
        transcript.insert(transcript.end(), signed_hello.begin(), signed_hello.end());
        verify_peer(reply.data() + signed_prefix, transcript.data(), transcript.size(), kServerIdentityContext);
        require(ldp_mlkem768_dec(kem_ss, reply.data() + kPreamble + crypto_kx_PUBLICKEYBYTES, kem_secret_) == 0,
                "ml-kem decap failed");
        kem_len = kMlkemShared;
        sodium_memzero(kem_secret_, sizeof(kem_secret_));
        kem_secret_live_ = false;
        server_hello_ = reply;

        std::vector<uint8_t> client_transcript = client_hello_;
        client_transcript.insert(client_transcript.end(), server_hello_.begin(), server_hello_.end());
        auto client_auth = preamble(kClientAuth);
        uint8_t signature[kMldsaSignature];
        sign_identity(signature, client_transcript.data(), client_transcript.size(), kClientIdentityContext);
        client_auth.insert(client_auth.end(), signature, signature + sizeof(signature));
        sodium_memzero(signature, sizeof(signature));
        bind_transcript(client_auth);
        transport_.send(std::move(client_auth));
    }
    derive_keys(tx, rx, kem_len ? kem_ss : nullptr, kem_len);
    sodium_memzero(sk, sizeof(sk));
    sodium_memzero(rx, sizeof(rx));
    sodium_memzero(tx, sizeof(tx));
    sodium_memzero(kem_ss, sizeof(kem_ss));
    established_ = true;
}

void Ldp1Session::read_client_hello() {
    if (hybrid_) {
        require(identity_set_, "hybrid profile requires a pinned identity");
        require(ldp_mlkem768_available() == 1, "ML-KEM-768 is not linked into this build");
    }
    std::vector<uint8_t> hello;
    require(transport_.recv(hello, ack_timeout_ms_ * 4), "client hello timeout");
    require(hello.size() >= kPreamble + crypto_kx_PUBLICKEYBYTES, "client hello truncated");
    require(std::memcmp(hello.data(), "LDP1", 4) == 0, "bad magic");
    require(hello[7] == kClientHello, "expected client hello");
    require((hello[6] & 1) == (hybrid_ ? 1 : 0), "profile mismatch");
    client_hello_ = hello;

    uint8_t client_pk[crypto_kx_PUBLICKEYBYTES];
    std::memcpy(client_pk, hello.data() + kPreamble, sizeof(client_pk));

    uint8_t pk[crypto_kx_PUBLICKEYBYTES];
    uint8_t sk[crypto_kx_SECRETKEYBYTES];
    require(crypto_kx_keypair(pk, sk) == 0, "x25519 keypair failed");
    uint8_t rx[32], tx[32];
    require(crypto_kx_server_session_keys(rx, tx, pk, sk, client_pk) == 0, "x25519 server failed");

    auto reply = preamble(kServerHello);
    reply.insert(reply.end(), pk, pk + sizeof(pk));

    uint8_t kem_ss[kMlkemShared];
    size_t kem_len = 0;
    if (hybrid_) {
        require(hello.size() == kPreamble + crypto_kx_PUBLICKEYBYTES + kMlkemPublicKey, "client hello kem size");
        uint8_t ct[kMlkemCiphertext];
        require(ldp_mlkem768_enc(ct, kem_ss, hello.data() + kPreamble + crypto_kx_PUBLICKEYBYTES) == 0,
                "ml-kem encap failed");
        reply.insert(reply.end(), ct, ct + sizeof(ct));
        sodium_memzero(ct, sizeof(ct));
        std::vector<uint8_t> transcript = client_hello_;
        transcript.insert(transcript.end(), reply.begin(), reply.end());
        uint8_t signature[kMldsaSignature];
        sign_identity(signature, transcript.data(), transcript.size(), kServerIdentityContext);
        reply.insert(reply.end(), signature, signature + sizeof(signature));
        sodium_memzero(signature, sizeof(signature));
        kem_len = kMlkemShared;
    }
    server_hello_ = reply;
    transport_.send(reply);
    if (hybrid_) {
        std::vector<uint8_t> client_auth;
        require(transport_.recv(client_auth, ack_timeout_ms_ * 4), "client auth timeout");
        require(client_auth.size() == kPreamble + kMldsaSignature, "client auth size");
        require(std::memcmp(client_auth.data(), "LDP1", 4) == 0 && client_auth[7] == kClientAuth, "expected client auth");
        std::vector<uint8_t> transcript = client_hello_;
        transcript.insert(transcript.end(), server_hello_.begin(), server_hello_.end());
        verify_peer(client_auth.data() + kPreamble, transcript.data(), transcript.size(), kClientIdentityContext);
        bind_transcript(client_auth);
    }
    // Server's rx is the client's tx from crypto_kx. Feed them in the same order the client uses (tx, rx).
    derive_keys(rx, tx, kem_len ? kem_ss : nullptr, kem_len);
    sodium_memzero(sk, sizeof(sk));
    sodium_memzero(rx, sizeof(rx));
    sodium_memzero(tx, sizeof(tx));
    sodium_memzero(kem_ss, sizeof(kem_ss));
    established_ = true;
}

void Ldp1Session::handshake() {
    if (role_ == Role::Client) {
        send_client_hello();
    } else {
        read_client_hello();
    }
}

void Ldp1Session::send_data(uint32_t seq, const std::vector<uint8_t>& chunk) {
    uint8_t seq_be[4];
    write_be32(seq_be, seq);
    uint8_t nonce[kNonce];
    randombytes_buf(nonce, sizeof(nonce));
    uint8_t key[32];
    std::memcpy(key, epoch_for(data_epochs_, seq).key, sizeof(key));
    auto cipher = seal(kData, key, seq_be, sizeof(seq_be), chunk.data(), chunk.size(), nonce);
    sodium_memzero(key, sizeof(key));
    auto header = preamble(kData);
    std::vector<uint8_t> datagram;
    datagram.insert(datagram.end(), header.begin(), header.end());
    datagram.insert(datagram.end(), nonce, nonce + kNonce);
    datagram.insert(datagram.end(), seq_be, seq_be + 4);
    datagram.insert(datagram.end(), cipher.begin(), cipher.end());
    transport_.send(std::move(datagram));
}

void Ldp1Session::send_ack(uint32_t base, uint64_t bitmap) {
    uint8_t fields[12];
    write_be32(fields, base);
    write_be64(fields + 4, bitmap);
    uint8_t nonce[kNonce];
    randombytes_buf(nonce, sizeof(nonce));
    auto cipher = seal(kAck, server_tx_, fields, sizeof(fields), fields, sizeof(fields), nonce);
    auto header = preamble(kAck);
    std::vector<uint8_t> datagram;
    datagram.insert(datagram.end(), header.begin(), header.end());
    datagram.insert(datagram.end(), nonce, nonce + kNonce);
    datagram.insert(datagram.end(), fields, fields + sizeof(fields));
    datagram.insert(datagram.end(), cipher.begin(), cipher.end());
    transport_.send(std::move(datagram));
}

std::vector<uint8_t> Ldp1Session::send_rekey(uint32_t activate_at) {
    uint8_t next[32];
    randombytes_buf(next, sizeof(next));
    uint8_t at[4];
    write_be32(at, activate_at);
    uint8_t nonce[kNonce];
    randombytes_buf(nonce, sizeof(nonce));
    uint8_t key[32];
    std::memcpy(key, epoch_for(data_epochs_, activate_at == 0 ? 0 : activate_at - 1).key, sizeof(key));
    auto cipher = seal(kRekey, key, at, sizeof(at), next, sizeof(next), nonce);
    sodium_memzero(key, sizeof(key));
    install_epoch(data_epochs_, next, activate_at);
    auto header = preamble(kRekey);
    std::vector<uint8_t> datagram;
    datagram.insert(datagram.end(), header.begin(), header.end());
    datagram.insert(datagram.end(), nonce, nonce + kNonce);
    datagram.insert(datagram.end(), at, at + 4);
    datagram.insert(datagram.end(), cipher.begin(), cipher.end());
    sodium_memzero(next, sizeof(next));
    return datagram;
}

void Ldp1Session::send_rekey_ack(uint32_t activate_at) {
    uint8_t at[4];
    write_be32(at, activate_at);
    uint8_t nonce[kNonce];
    randombytes_buf(nonce, sizeof(nonce));
    auto cipher = seal(kRekeyAck, server_tx_, at, sizeof(at), at, sizeof(at), nonce);
    auto header = preamble(kRekeyAck);
    std::vector<uint8_t> datagram;
    datagram.insert(datagram.end(), header.begin(), header.end());
    datagram.insert(datagram.end(), nonce, nonce + kNonce);
    datagram.insert(datagram.end(), at, at + 4);
    datagram.insert(datagram.end(), cipher.begin(), cipher.end());
    transport_.send(std::move(datagram));
}

void Ldp1Session::send_close(const uint8_t hash[32]) {
    uint8_t nonce[kNonce];
    randombytes_buf(nonce, sizeof(nonce));
    uint8_t key[32];
    std::memcpy(key, data_epochs_.back().key, sizeof(key));
    std::vector<uint8_t> clear(hash, hash + 32);
    if (hybrid_) {
        uint8_t message[64];
        std::memcpy(message, hash, 32);
        std::memcpy(message + 32, transcript_hash_, 32);
        uint8_t signature[kMldsaSignature];
        sign_identity(signature, message, sizeof(message), kCloseIdentityContext);
        clear.insert(clear.end(), signature, signature + sizeof(signature));
        sodium_memzero(signature, sizeof(signature));
        sodium_memzero(message, sizeof(message));
    }
    auto cipher = seal(kClose, key, clear.data(), clear.size(), hash, 32, nonce);
    sodium_memzero(key, sizeof(key));
    auto header = preamble(kClose);
    std::vector<uint8_t> datagram;
    datagram.insert(datagram.end(), header.begin(), header.end());
    datagram.insert(datagram.end(), nonce, nonce + kNonce);
    datagram.insert(datagram.end(), clear.begin(), clear.end());
    datagram.insert(datagram.end(), cipher.begin(), cipher.end());
    transport_.send(std::move(datagram));
}


bool Ldp1Session::handle_during_send(const std::vector<uint8_t>& datagram, uint32_t& ack_base, uint64_t& ack_bitmap,
                                    bool& got_ack, bool& got_rekey_ack, uint32_t& rekey_ack_at) {
    if (datagram.size() < kPreamble + kNonce || std::memcmp(datagram.data(), "LDP1", 4) != 0) {
        return false;
    }
    const uint8_t type = datagram[7];
    const uint8_t* nonce = datagram.data() + kPreamble;
    try {
        if (type == kAck) {
            if (datagram.size() < kPreamble + kNonce + 12 + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
                return false;
            }
            const uint8_t* fields = datagram.data() + kPreamble + kNonce;
            auto header = preamble(kAck);
            std::vector<uint8_t> aad(header.begin(), header.end());
            aad.insert(aad.end(), fields, fields + 12);
            open(server_tx_, aad.data(), aad.size(), nonce, fields + 12, datagram.size() - (kPreamble + kNonce + 12));
            ack_base = read_be32(fields);
            ack_bitmap = read_be64(fields + 4);
            got_ack = true;
            return true;
        }
        if (type == kRekeyAck) {
            if (datagram.size() < kPreamble + kNonce + 4 + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
                return false;
            }
            const uint8_t* at = datagram.data() + kPreamble + kNonce;
            auto header = preamble(kRekeyAck);
            std::vector<uint8_t> aad(header.begin(), header.end());
            aad.insert(aad.end(), at, at + 4);
            open(server_tx_, aad.data(), aad.size(), nonce, at + 4, datagram.size() - (kPreamble + kNonce + 4));
            rekey_ack_at = read_be32(at);
            got_rekey_ack = true;
            return true;
        }
    } catch (const std::runtime_error&) {
        return false;
    }
    return false;
}


void Ldp1Session::send_all(const std::vector<uint8_t>& plaintext, size_t chunk_size, uint32_t rekey_every) {
    require(established_ && role_ == Role::Client, "send_all requires an established client");
    require(chunk_size > 0, "chunk size");
    std::vector<std::vector<uint8_t>> chunks;
    if (plaintext.empty()) {
        chunks.emplace_back();
    } else {
        for (size_t offset = 0; offset < plaintext.size(); offset += chunk_size) {
            size_t n = std::min(chunk_size, plaintext.size() - offset);
            chunks.emplace_back(plaintext.begin() + static_cast<std::ptrdiff_t>(offset),
                                plaintext.begin() + static_cast<std::ptrdiff_t>(offset + n));
        }
    }

    std::map<uint32_t, std::vector<uint8_t>> outstanding;
    uint32_t next = 0;
    uint32_t last_rekey = 0;
    uint8_t file_hash[32];
    const uint8_t* bytes = plaintext.empty() ? reinterpret_cast<const uint8_t*>("") : plaintext.data();
    crypto_hash_sha256(file_hash, bytes, plaintext.size());

    auto mark_acked = [&](uint32_t base, uint64_t bitmap) {
        for (auto it = outstanding.begin(); it != outstanding.end();) {
            if (it->first < base) {
                it = outstanding.erase(it);
            } else {
                ++it;
            }
        }
        for (uint32_t i = 0; i < 64; ++i) {
            if ((bitmap & (1ull << i)) != 0) {
                outstanding.erase(base + i);
            }
        }
        wipe_epochs_before(data_epochs_, base);
    };

    while (next < chunks.size() || !outstanding.empty()) {
        if (rekey_every > 0 && next > 0 && next < chunks.size() && (next % rekey_every) == 0 && last_rekey != next) {
            auto rekey_packet = send_rekey(next);
            transport_.send(rekey_packet);
            bool got_rekey_ack = false;
            uint32_t echoed = 0;
            for (int attempt = 0; attempt < 8 && !got_rekey_ack; ++attempt) {
                std::vector<uint8_t> incoming;
                if (!transport_.recv(incoming, ack_timeout_ms_)) {
                    transport_.send(rekey_packet);
                    continue;
                }
                uint32_t ack_base = 0;
                uint64_t ack_bitmap = 0;
                bool got_ack = false;
                uint32_t rekey_at = 0;
                bool rekey_ack = false;
                handle_during_send(incoming, ack_base, ack_bitmap, got_ack, rekey_ack, rekey_at);
                if (got_ack) {
                    mark_acked(ack_base, ack_bitmap);
                }
                if (rekey_ack && rekey_at == next) {
                    got_rekey_ack = true;
                    echoed = rekey_at;
                }
            }
            require(got_rekey_ack && echoed == next, "rekey was not acknowledged");
            last_rekey = next;
        }

        while (outstanding.size() < kWindow && next < chunks.size()) {
            if (rekey_every > 0 && next > 0 && (next % rekey_every) == 0 && last_rekey != next) {
                break;
            }
            send_data(next, chunks[next]);
            outstanding[next] = chunks[next];
            ++next;
        }

        std::vector<uint8_t> incoming;
        if (!transport_.recv(incoming, ack_timeout_ms_)) {
            for (const auto& item : outstanding) {
                send_data(item.first, item.second);
            }
            continue;
        }
        uint32_t ack_base = 0;
        uint64_t ack_bitmap = 0;
        bool got_ack = false;
        bool got_rekey_ack = false;
        uint32_t rekey_at = 0;
        handle_during_send(incoming, ack_base, ack_bitmap, got_ack, got_rekey_ack, rekey_at);
        if (got_ack) {
            mark_acked(ack_base, ack_bitmap);
        }
    }

    send_close(file_hash);
    sodium_memzero(file_hash, sizeof(file_hash));
}

std::vector<uint8_t> Ldp1Session::receive_all(int idle_timeout_ms) {
    require(established_ && role_ == Role::Server, "receive_all requires an established server");
    std::vector<std::vector<uint8_t>> slots;
    uint32_t expected = 0;
    std::vector<uint8_t> output;
    uint8_t expected_hash[32];
    bool have_hash = false;

    while (true) {
        std::vector<uint8_t> datagram;
        if (!transport_.recv(datagram, idle_timeout_ms)) {
            throw std::runtime_error("receive idle timeout");
        }
        if (datagram.size() < kPreamble + kNonce || std::memcmp(datagram.data(), "LDP1", 4) != 0) {
            continue;
        }
        const uint8_t type = datagram[7];
        const uint8_t* nonce = datagram.data() + kPreamble;
        try {
            if (type == kData) {
                if (datagram.size() < kPreamble + kNonce + 4 + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
                    continue;
                }
                const uint8_t* seq_be = datagram.data() + kPreamble + kNonce;
                uint32_t seq = read_be32(seq_be);
                auto header = preamble(kData);
                std::vector<uint8_t> aad(header.begin(), header.end());
                aad.insert(aad.end(), seq_be, seq_be + 4);
                uint8_t key[32];
                std::memcpy(key, epoch_for(data_epochs_, seq).key, sizeof(key));
                auto plain = open(key, aad.data(), aad.size(), nonce, seq_be + 4,
                                  datagram.size() - (kPreamble + kNonce + 4));
                sodium_memzero(key, sizeof(key));
                if (slots.size() <= seq) {
                    slots.resize(seq + 1);
                }
                if (slots[seq].empty()) {
                    slots[seq] = std::move(plain);
                }
                while (expected < slots.size() && !slots[expected].empty()) {
                    ++expected;
                }
                uint64_t bitmap = 0;
                for (uint32_t i = 0; i < 64; ++i) {
                    uint32_t seq_i = expected + i;
                    if (seq_i < slots.size() && !slots[seq_i].empty()) {
                        bitmap |= (1ull << i);
                    }
                }
                send_ack(expected, bitmap);
            } else if (type == kRekey) {
                if (datagram.size() < kPreamble + kNonce + 4 + 32 + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
                    continue;
                }
                const uint8_t* at = datagram.data() + kPreamble + kNonce;
                uint32_t activate_at = read_be32(at);
                auto header = preamble(kRekey);
                std::vector<uint8_t> aad(header.begin(), header.end());
                aad.insert(aad.end(), at, at + 4);
                uint8_t key[32];
                std::memcpy(key, epoch_for(data_epochs_, activate_at == 0 ? 0 : activate_at - 1).key, sizeof(key));
                auto plain = open(key, aad.data(), aad.size(), nonce, at + 4,
                                  datagram.size() - (kPreamble + kNonce + 4));
                sodium_memzero(key, sizeof(key));
                require(plain.size() == 32, "rekey key size");
                install_epoch(data_epochs_, plain.data(), activate_at);
                sodium_memzero(plain.data(), plain.size());
                send_rekey_ack(activate_at);
            } else if (type == kClose) {
                const size_t clear_len = hybrid_ ? 32 + kMldsaSignature : 32;
                if (datagram.size() < kPreamble + kNonce + clear_len + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
                    continue;
                }
                const uint8_t* hash = datagram.data() + kPreamble + kNonce;
                if (hybrid_) {
                    uint8_t message[64];
                    std::memcpy(message, hash, 32);
                    std::memcpy(message + 32, transcript_hash_, 32);
                    verify_peer(hash + 32, message, sizeof(message), kCloseIdentityContext);
                    sodium_memzero(message, sizeof(message));
                }
                auto header = preamble(kClose);
                std::vector<uint8_t> aad(header.begin(), header.end());
                aad.insert(aad.end(), hash, hash + clear_len);
                uint8_t key[32];
                std::memcpy(key, data_epochs_.back().key, sizeof(key));
                auto plain = open(key, aad.data(), aad.size(), nonce, hash + clear_len,
                                  datagram.size() - (kPreamble + kNonce + clear_len));
                sodium_memzero(key, sizeof(key));
                require(plain.size() == 32 && std::memcmp(plain.data(), hash, 32) == 0, "close hash mismatch");
                std::memcpy(expected_hash, hash, 32);
                have_hash = true;
                break;
            }
        } catch (const std::runtime_error& ex) {
            if (std::strcmp(ex.what(), "aead decrypt failed") == 0) {
                continue;
            }
            throw;
        }
    }

    require(have_hash, "missing close");
    for (uint32_t seq = 0; seq < slots.size(); ++seq) {
        require(!slots[seq].empty(), "missing chunk at close");
        output.insert(output.end(), slots[seq].begin(), slots[seq].end());
    }
    uint8_t actual[32];
    crypto_hash_sha256(actual, output.data(), output.size());
    require(std::memcmp(actual, expected_hash, 32) == 0, "file hash mismatch");
    return output;
}
