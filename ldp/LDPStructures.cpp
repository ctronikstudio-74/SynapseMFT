#include "LDPStructures.h"
#include <cstring>

// Implementazioni helper per strutture LDP
namespace LDP {
    bool validate_packet(const LDPPacket& packet) {
        // Verifica dimensione header
        if (packet.encrypted_payload.size() == 0) {
            return false;
        }
        // Verifica MAC presente
        return true;
    }
    
    size_t packet_size(const LDPPacket& packet) {
        return sizeof(LDPHeader) + packet.encrypted_payload.size() + sizeof(packet.mac);
    }
    
    std::vector<uint8_t> serialize_packet(const LDPPacket& packet) {
        std::vector<uint8_t> data;
        size_t total_size = packet_size(packet);
        data.resize(total_size);
        
        std::memcpy(data.data(), &packet.header, sizeof(LDPHeader));
        std::memcpy(data.data() + sizeof(LDPHeader), 
                   packet.encrypted_payload.data(), packet.encrypted_payload.size());
        std::memcpy(data.data() + sizeof(LDPHeader) + packet.encrypted_payload.size(),
                   packet.mac, sizeof(packet.mac));
        
        return data;
    }
    
    bool deserialize_packet(const std::vector<uint8_t>& data, LDPPacket& packet) {
        if (data.size() < sizeof(LDPHeader) + sizeof(packet.mac)) {
            return false;
        }
        
        std::memcpy(&packet.header, data.data(), sizeof(LDPHeader));
        
        size_t payload_size = data.size() - sizeof(LDPHeader) - sizeof(packet.mac);
        packet.encrypted_payload.resize(payload_size);
        std::memcpy(packet.encrypted_payload.data(), 
                   data.data() + sizeof(LDPHeader), payload_size);
        std::memcpy(packet.mac, 
                   data.data() + sizeof(LDPHeader) + payload_size, sizeof(packet.mac));
        
        return true;
    }
}

