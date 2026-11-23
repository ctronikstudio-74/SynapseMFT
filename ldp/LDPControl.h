#pragma once
#include <cstdint>

enum class LDPCommandType : uint8_t {
    DATA        = 0x00,
    LS_REQUEST  = 0x01,
    LS_RESPONSE = 0x02,
    RM_REQUEST  = 0x03,
    MKDIR_REQ   = 0x04,
    ERROR_MSG   = 0xFF
};

#pragma pack(push, 1)
struct LDPControlHeader {
    LDPCommandType type;
    uint32_t payload_len;
};
#pragma pack(pop)
