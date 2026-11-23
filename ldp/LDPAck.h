#pragma once
#include "LDPStructures.h"
#include "LDPMacUtils.h"
#include <set>
#include <cstdint>
#include <vector>
#include <cstring>

class LDPAck {
    std::set<uint32_t> received_packets;
public:
    LDPAck();
    void add_received(uint32_t seq_id);
    LDPAckPacket generate_ack_packet(const uint8_t* key);
    bool is_received(uint32_t seq_id) const;
};
