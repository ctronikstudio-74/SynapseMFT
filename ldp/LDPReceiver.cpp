#include "LDPReceiver.h"
#include "LDPCrypto.h"
#include "LDPStructures.h"
#include <iostream>

LDPReceiver::LDPReceiver(const std::string& out_path) : out_file(out_path, std::ios::binary) {
    if (!out_file) {
        throw std::runtime_error("Cannot create output file: " + out_path);
    }
    std::cout << "[LDP] Receiver initialized, output: " << out_path << std::endl;
}

void LDPReceiver::on_packet_received(uint32_t seq_id, const std::vector<uint8_t>& decrypted_data, 
                                     const std::vector<uint8_t>& mac, const uint8_t* key) {
    if (!LDPMacUtils::verify_mac(decrypted_data, mac.data(), key)) {
        std::cerr << "[LDP] MAC verification failed for packet " << seq_id << std::endl;
        return;
    }
    
    assembly_buffer[seq_id] = decrypted_data;
    
    // Scrivi in ordine sequenziale
    while (assembly_buffer.count(expected_seq_id)) {
        write_to_final_file(assembly_buffer[expected_seq_id]);
        assembly_buffer.erase(expected_seq_id);
        expected_seq_id++;
        
        if (expected_seq_id % 100 == 0) {
            std::cout << "[LDP] Received and written " << expected_seq_id << " packets" << std::endl;
        }
    }
}

void LDPReceiver::write_to_final_file(const std::vector<uint8_t>& data) {
    out_file.write(reinterpret_cast<const char*>(data.data()), data.size());
    out_file.flush();
}

LDPReceiver::~LDPReceiver() {
    if (out_file.is_open()) {
        out_file.close();
    }
    std::cout << "[LDP] Receiver closed. Total packets received: " << expected_seq_id << std::endl;
}

