#pragma once
#include <string>
#if defined(__has_include)
#  if __has_include(<pkcs11.h>)
#    include <pkcs11.h>
#    define LDP_HAVE_PKCS11 1
#  endif
#endif
#ifndef LDP_HAVE_PKCS11
using CK_SLOT_ID = unsigned long;
using CK_SESSION_HANDLE = unsigned long;
using CK_OBJECT_HANDLE = unsigned long;
#endif

class LDPHsmManager {
    CK_SESSION_HANDLE hSession;
public:
    void initialize_hsm(CK_SLOT_ID slotID, const std::string& pin);
    CK_OBJECT_HANDLE get_key_handle(const std::string& label);
    void finalize_hsm();
};
