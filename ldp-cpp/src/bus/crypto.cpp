#include "synapse/crypto.hpp"
#include "synapse/codec.hpp"

#if defined(SYNAPSE_HAVE_PQ)
#include <sodium.h>
#include "mlkem768.h"
#include "mldsa65.h"
#endif

namespace synapse::bus {
namespace {

const char* kContext = "synapse-bus-v1";

#if defined(SYNAPSE_HAVE_PQ)
Status hkdf_key(const uint8_t x[32], const uint8_t kem[32], uint8_t out[32]) {
    std::vector<uint8_t> ikm(64);
    for (int i = 0; i < 32; ++i) {
        ikm[i] = x[i];
        ikm[32 + i] = kem[i];
    }
    unsigned char prk[crypto_kdf_hkdf_sha256_KEYBYTES];
    if (crypto_kdf_hkdf_sha256_extract(prk, nullptr, 0, ikm.data(), ikm.size()) != 0) {
        return Status::failure("hkdf extract failed");
    }
    if (crypto_kdf_hkdf_sha256_expand(out, 32, kContext, sizeof("synapse-bus-v1") - 1, prk) != 0) {
        return Status::failure("hkdf expand failed");
    }
    return Status::success();
}
#endif

}  // namespace

bool pq_available() {
#if defined(SYNAPSE_HAVE_PQ)
    return ldp_mlkem768_available() == 1 && ldp_mldsa65_available() == 1 && sodium_init() >= 0;
#else
    return false;
#endif
}

Result<HybridIdentity> generate_identity() {
#if !defined(SYNAPSE_HAVE_PQ)
    return Result<HybridIdentity>::failure("post-quantum profile is not linked");
#else
    if (sodium_init() < 0) return Result<HybridIdentity>::failure("sodium init failed");
    HybridIdentity id;
    randombytes_buf(id.x25519_secret.data(), id.x25519_secret.size());
    if (crypto_scalarmult_curve25519_base(id.pub.x25519.data(), id.x25519_secret.data()) != 0) {
        return Result<HybridIdentity>::failure("x25519 keygen failed");
    }
    id.pub.kem.resize(LDP_MLKEM768_PUBLICKEY);
    id.kem_secret.resize(LDP_MLKEM768_SECRETKEY);
    id.pub.dsa.resize(LDP_MLDSA65_PUBLICKEY);
    id.dsa_secret.resize(LDP_MLDSA65_SECRETKEY);
    if (ldp_mlkem768_keypair(id.pub.kem.data(), id.kem_secret.data()) != 0) {
        return Result<HybridIdentity>::failure("ml-kem keypair failed");
    }
    if (ldp_mldsa65_keypair(id.pub.dsa.data(), id.dsa_secret.data()) != 0) {
        return Result<HybridIdentity>::failure("ml-dsa keypair failed");
    }
    return Result<HybridIdentity>::success(std::move(id));
#endif
}

Result<std::vector<uint8_t>> seal(const HybridIdentity& sender, const HybridPublic& recipient,
                                  const std::string& plaintext, const std::string& aad) {
#if !defined(SYNAPSE_HAVE_PQ)
    (void)sender;
    (void)recipient;
    (void)plaintext;
    (void)aad;
    return Result<std::vector<uint8_t>>::failure("post-quantum profile is not linked");
#else
    if (recipient.kem.size() != LDP_MLKEM768_PUBLICKEY || recipient.dsa.size() != LDP_MLDSA65_PUBLICKEY ||
        sender.dsa_secret.size() != LDP_MLDSA65_SECRETKEY) {
        return Result<std::vector<uint8_t>>::failure("identity size mismatch");
    }
    uint8_t eph_sk[32];
    uint8_t eph_pk[32];
    randombytes_buf(eph_sk, sizeof(eph_sk));
    if (crypto_scalarmult_curve25519_base(eph_pk, eph_sk) != 0) {
        return Result<std::vector<uint8_t>>::failure("ephemeral x25519 failed");
    }
    uint8_t x_ss[32];
    if (crypto_scalarmult_curve25519(x_ss, eph_sk, recipient.x25519.data()) != 0) {
        return Result<std::vector<uint8_t>>::failure("x25519 shared secret failed");
    }
    std::vector<uint8_t> kem_ct(LDP_MLKEM768_CIPHERTEXT);
    uint8_t kem_ss[32];
    if (ldp_mlkem768_enc(kem_ct.data(), kem_ss, recipient.kem.data()) != 0) {
        return Result<std::vector<uint8_t>>::failure("ml-kem encaps failed");
    }
    uint8_t key[32];
    if (auto status = hkdf_key(x_ss, kem_ss, key); !status.ok()) {
        return Result<std::vector<uint8_t>>::failure(status.error);
    }
    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    std::vector<uint8_t> cipher(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long clen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            cipher.data(), &clen, reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
            reinterpret_cast<const uint8_t*>(aad.data()), aad.size(), nullptr, nonce, key) != 0) {
        return Result<std::vector<uint8_t>>::failure("aead seal failed");
    }
    cipher.resize(static_cast<size_t>(clen));
    std::vector<uint8_t> signed_bytes;
    codec::append_bytes(signed_bytes, eph_pk, 32);
    codec::append_bytes(signed_bytes, kem_ct.data(), kem_ct.size());
    codec::append_bytes(signed_bytes, nonce, sizeof(nonce));
    codec::append_u32(signed_bytes, static_cast<uint32_t>(cipher.size()));
    codec::append_bytes(signed_bytes, cipher.data(), cipher.size());
    codec::append_bytes(signed_bytes, reinterpret_cast<const uint8_t*>(aad.data()), aad.size());
    std::vector<uint8_t> signature(LDP_MLDSA65_SIGNATURE);
    if (ldp_mldsa65_sign(signature.data(), signed_bytes.data(), signed_bytes.size(),
                         reinterpret_cast<const uint8_t*>(kContext), sizeof("synapse-bus-v1") - 1,
                         sender.dsa_secret.data()) != 0) {
        return Result<std::vector<uint8_t>>::failure("ml-dsa sign failed");
    }
    std::vector<uint8_t> blob;
    blob.push_back(1);
    codec::append_bytes(blob, eph_pk, 32);
    codec::append_bytes(blob, kem_ct.data(), kem_ct.size());
    codec::append_bytes(blob, nonce, sizeof(nonce));
    codec::append_u32(blob, static_cast<uint32_t>(cipher.size()));
    codec::append_bytes(blob, cipher.data(), cipher.size());
    codec::append_bytes(blob, signature.data(), signature.size());
    return Result<std::vector<uint8_t>>::success(std::move(blob));
#endif
}

Result<std::string> open_sealed(const HybridIdentity& recipient, const HybridPublic& sender,
                                const std::vector<uint8_t>& blob, const std::string& aad) {
#if !defined(SYNAPSE_HAVE_PQ)
    (void)recipient;
    (void)sender;
    (void)blob;
    (void)aad;
    return Result<std::string>::failure("post-quantum profile is not linked");
#else
    const size_t header = 1 + 32 + LDP_MLKEM768_CIPHERTEXT + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + 4;
    if (blob.size() < header + LDP_MLDSA65_SIGNATURE || blob[0] != 1) {
        return Result<std::string>::failure("sealed blob rejected");
    }
    codec::Reader reader(blob.data(), blob.size());
    auto version = reader.u8();
    auto eph = reader.take(32);
    auto kem_ct = reader.take(LDP_MLKEM768_CIPHERTEXT);
    auto nonce = reader.take(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    auto clen = reader.u32();
    if (!version.ok() || !eph.ok() || !kem_ct.ok() || !nonce.ok() || !clen.ok()) {
        return Result<std::string>::failure("sealed blob rejected");
    }
    auto cipher = reader.take(clen.value);
    auto signature = reader.take(LDP_MLDSA65_SIGNATURE);
    if (!cipher.ok() || !signature.ok() || reader.remaining() != 0) {
        return Result<std::string>::failure("sealed blob rejected");
    }
    if (sender.dsa.size() != LDP_MLDSA65_PUBLICKEY || recipient.kem_secret.size() != LDP_MLKEM768_SECRETKEY) {
        return Result<std::string>::failure("identity size mismatch");
    }
    std::vector<uint8_t> signed_bytes;
    codec::append_bytes(signed_bytes, eph.value.data(), eph.value.size());
    codec::append_bytes(signed_bytes, kem_ct.value.data(), kem_ct.value.size());
    codec::append_bytes(signed_bytes, nonce.value.data(), nonce.value.size());
    codec::append_u32(signed_bytes, clen.value);
    codec::append_bytes(signed_bytes, cipher.value.data(), cipher.value.size());
    codec::append_bytes(signed_bytes, reinterpret_cast<const uint8_t*>(aad.data()), aad.size());
    if (ldp_mldsa65_verify(signature.value.data(), signed_bytes.data(), signed_bytes.size(),
                           reinterpret_cast<const uint8_t*>(kContext), sizeof("synapse-bus-v1") - 1,
                           sender.dsa.data()) != 0) {
        return Result<std::string>::failure("ml-dsa verify failed");
    }
    uint8_t x_ss[32];
    if (crypto_scalarmult_curve25519(x_ss, recipient.x25519_secret.data(), eph.value.data()) != 0) {
        return Result<std::string>::failure("x25519 shared secret failed");
    }
    uint8_t kem_ss[32];
    if (ldp_mlkem768_dec(kem_ss, kem_ct.value.data(), recipient.kem_secret.data()) != 0) {
        return Result<std::string>::failure("ml-kem decaps failed");
    }
    uint8_t key[32];
    if (auto status = hkdf_key(x_ss, kem_ss, key); !status.ok()) return Result<std::string>::failure(status.error);
    std::vector<uint8_t> plain(cipher.value.size());
    unsigned long long plen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plain.data(), &plen, nullptr, cipher.value.data(), cipher.value.size(),
            reinterpret_cast<const uint8_t*>(aad.data()), aad.size(), nonce.value.data(), key) != 0) {
        return Result<std::string>::failure("aead open failed");
    }
    return Result<std::string>::success(std::string(reinterpret_cast<char*>(plain.data()), static_cast<size_t>(plen)));
#endif
}

}  // namespace synapse::bus
