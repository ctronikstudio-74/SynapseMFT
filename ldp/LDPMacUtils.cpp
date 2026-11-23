#include "LDPMacUtils.h"
#include <stdexcept>

std::vector<uint8_t> LDPMacUtils::compute_mac(const std::vector<uint8_t>& data, const uint8_t* key) {
    std::vector<uint8_t> mac(crypto_auth_BYTES);
    if (crypto_auth(mac.data(), data.data(), data.size(), key) != 0) {
        throw std::runtime_error("MAC computation failed");
    }
    return mac;
}

bool LDPMacUtils::verify_mac(const std::vector<uint8_t>& data, const uint8_t* mac, const uint8_t* key) {
    return crypto_auth_verify(mac, data.data(), data.size(), key) == 0;
}

