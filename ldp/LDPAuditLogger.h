#pragma once
#include <sodium.h>
#include <fstream>
#include <string>
#include <chrono>

class LDPAuditLogger {
    uint8_t server_sk[crypto_sign_SECRETKEYBYTES];
    std::string last_hash;
public:
    LDPAuditLogger();
    void log_action(const std::string& user, const std::string& action, const std::string& status);
};
