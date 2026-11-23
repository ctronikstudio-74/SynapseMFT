#include "LDPAck.h"
#include "LDPStructures.h"
#include "LDPMacUtils.h"
#include <iostream>

LDPAck::LDPAck() {
}

void LDPAck::add_received(uint32_t seq_id) {
    received_packets.insert(seq_id);
}

LDPAckPacket LDPAck::generate_ack_packet(const uint8_t* key) {
    LDPAckPacket ack;
    
    if (received_packets.empty()) {
        ack.base_seq_id = 0;
        ack.sack_bitmap = 0;
    } else {
        uint32_t min_seq = *received_packets.begin();
        ack.base_seq_id = min_seq;
        ack.sack_bitmap = 0;
        
        // Costruisci bitmap per i pacchetti ricevuti
        for (uint32_t seq : received_packets) {
            if (seq >= min_seq && seq < min_seq + 64) {
                uint32_t offset = seq - min_seq;
                ack.sack_bitmap |= (1ULL << offset);
            }
        }
    }
    
    // Calcola MAC
    std::vector<uint8_t> ack_data(sizeof(LDPAckPacket) - sizeof(ack.mac));
    std::memcpy(ack_data.data(), &ack, ack_data.size());
    auto mac = LDPMacUtils::compute_mac(ack_data, key);
    std::memcpy(ack.mac, mac.data(), sizeof(ack.mac));
    
    return ack;
}

bool LDPAck::is_received(uint32_t seq_id) const {
    return received_packets.count(seq_id) > 0;
}

