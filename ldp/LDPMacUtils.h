#pragma once
#include <sodium.h>
#include <vector>
#include <stdexcept>

class LDPMacUtils {
public:
    static std::vector<uint8_t> compute_mac(const std::vector<uint8_t>& data, const uint8_t* key);
    static bool verify_mac(const std::vector<uint8_t>& data, const uint8_t* mac, const uint8_t* key);
};
