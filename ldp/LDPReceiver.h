#pragma once
#include <map>
#include <vector>
#include <cstdint>
#include <fstream>
#include "LDPMacUtils.h"

class LDPReceiver {
    std::map<uint32_t, std::vector<uint8_t>> assembly_buffer;
    uint32_t expected_seq_id = 0;
    std::ofstream out_file;
public:
    LDPReceiver(const std::string& out_path);
    ~LDPReceiver();
    void on_packet_received(uint32_t seq_id, const std::vector<uint8_t>& decrypted_data, 
                           const std::vector<uint8_t>& mac, const uint8_t* key);
private:
    void write_to_final_file(const std::vector<uint8_t>& data);
};
