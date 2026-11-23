#pragma once

#include "result.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace synapse::bus {

bool pq_available();

struct HybridPublic {
    std::array<uint8_t, 32> x25519{};
    std::vector<uint8_t> kem;
    std::vector<uint8_t> dsa;
};

struct HybridIdentity {
    HybridPublic pub;
    std::array<uint8_t, 32> x25519_secret{};
    std::vector<uint8_t> kem_secret;
    std::vector<uint8_t> dsa_secret;
};

Result<HybridIdentity> generate_identity();
Result<std::vector<uint8_t>> seal(const HybridIdentity& sender, const HybridPublic& recipient,
                                  const std::string& plaintext, const std::string& aad);
Result<std::string> open_sealed(const HybridIdentity& recipient, const HybridPublic& sender,
                                const std::vector<uint8_t>& blob, const std::string& aad);

}  // namespace synapse::bus
