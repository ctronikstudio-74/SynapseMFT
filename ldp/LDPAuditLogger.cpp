#include "LDPAuditLogger.h"
#include <iomanip>
#include <sstream>

LDPAuditLogger::LDPAuditLogger() {
    // Genera chiave per demo (in produzione da HSM)
    crypto_sign_keypair(server_sk, nullptr);
    last_hash = "0";
}

void LDPAuditLogger::log_action(const std::string& user, const std::string& action, const std::string& status) {
    auto now = std::chrono::system_clock::now();
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::string log_entry = std::to_string(timestamp) + "|" + user + "|" + action + "|" + status + "|" + last_hash;
    
    uint8_t signature[crypto_sign_BYTES];
    unsigned long long sig_len;
    crypto_sign_detached(signature, &sig_len, 
                        reinterpret_cast<const unsigned char*>(log_entry.c_str()), 
                        log_entry.length(), server_sk);
    
    std::ofstream audit_file("ldp_audit.log", std::ios::app);
    audit_file << log_entry << " [SIG:";
    for (size_t i = 0; i < sig_len; i++) {
        audit_file << std::hex << std::setw(2) << std::setfill('0') << (int)signature[i];
    }
    audit_file << "]" << std::endl;
    
    uint8_t hash[crypto_generichash_BYTES];
    crypto_generichash(hash, sizeof(hash), 
                      reinterpret_cast<const unsigned char*>(log_entry.c_str()), 
                      log_entry.length(), NULL, 0);
    
    std::ostringstream oss;
    for (size_t i = 0; i < sizeof(hash); i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    last_hash = oss.str();
}

