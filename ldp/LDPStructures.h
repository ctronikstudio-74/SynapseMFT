#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

#pragma pack(push, 1)
struct LDPHeader {
    uint8_t  nonce[24];      // Nonce per XChaCha20-Poly1305
    uint32_t sequence_id;    // ID per ordinamento e prevenzione replay
    uint16_t payload_size;   // Dimensione effettiva dei dati
    uint8_t  type;           // Tipo pacchetto: dati, comando, ecc.
};
#pragma pack(pop)

struct LDPPacket {
    LDPHeader header;
    std::vector<uint8_t> encrypted_payload;
    uint8_t mac[16];         // Tag di autenticazione
};

#pragma pack(push, 1)
struct LDPAckPacket {
    uint32_t base_seq_id;
    uint64_t sack_bitmap;
    uint8_t mac[16];
};
#pragma pack(pop)

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

// Helper functions
namespace LDP {
    bool validate_packet(const LDPPacket& packet);
    size_t packet_size(const LDPPacket& packet);
    std::vector<uint8_t> serialize_packet(const LDPPacket& packet);
    bool deserialize_packet(const std::vector<uint8_t>& data, LDPPacket& packet);
}
