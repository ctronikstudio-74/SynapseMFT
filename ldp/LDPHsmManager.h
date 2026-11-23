#pragma once
#include <pkcs11.h>
#include <string>

class LDPHsmManager {
    CK_SESSION_HANDLE hSession;
public:
    void initialize_hsm(CK_SLOT_ID slotID, const std::string& pin);
    CK_OBJECT_HANDLE get_key_handle(const std::string& label);
    void finalize_hsm();
};
