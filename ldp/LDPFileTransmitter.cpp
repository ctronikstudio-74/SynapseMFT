#include "LDPFileTransmitter.h"
#include "LDPStructures.h"
#include <iostream>
#include <iomanip>

void LDPFileTransmitter::send_file(const std::string& path, const std::string& target_ip, int port) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[LDP] Cannot open file: " << path << std::endl;
        return;
    }
    
    std::vector<char> buffer(CHUNK_SIZE);
    uint32_t seq_id = 0;
    uint64_t total_sent = 0;
    
    std::cout << "[LDP] Starting file transfer: " << path << " -> " << target_ip << ":" << port << std::endl;
    
    while (file.read(buffer.data(), CHUNK_SIZE) || file.gcount() > 0) {
        size_t bytes_read = static_cast<size_t>(file.gcount());
        
        // Rekeying ogni 1000 pacchetti
        if (seq_id % 1000 == 0 && seq_id != 0) {
            perform_rekeying();
        }
        
        std::vector<uint8_t> data(buffer.begin(), buffer.begin() + bytes_read);
        uint8_t nonce[24];
        randombytes_buf(nonce, sizeof(nonce));
        
        // Cifratura
        auto packet_body = crypto.encrypt(data, nonce, seq_id);
        
        // Calcolo MAC
        auto mac = LDPMacUtils::compute_mac(packet_body, crypto.get_key());
        
        // Costruzione pacchetto LDP
        LDPPacket packet;
        std::memcpy(packet.header.nonce, nonce, sizeof(nonce));
        packet.header.sequence_id = seq_id;
        packet.header.payload_size = static_cast<uint16_t>(packet_body.size());
        packet.header.type = static_cast<uint8_t>(LDPCommandType::DATA);
        packet.encrypted_payload = packet_body;
        std::memcpy(packet.mac, mac.data(), sizeof(packet.mac));
        
        // Serializzazione pacchetto
        std::vector<uint8_t> packet_data;
        packet_data.resize(sizeof(LDPHeader) + packet.encrypted_payload.size() + sizeof(packet.mac));
        
        std::memcpy(packet_data.data(), &packet.header, sizeof(LDPHeader));
        std::memcpy(packet_data.data() + sizeof(LDPHeader), 
                   packet.encrypted_payload.data(), packet.encrypted_payload.size());
        std::memcpy(packet_data.data() + sizeof(LDPHeader) + packet.encrypted_payload.size(),
                   packet.mac, sizeof(packet.mac));
        
        // Invio pacchetto
        socket.send_packet(target_ip, port, packet_data);
        
        total_sent += bytes_read;
        seq_id++;
        
        if (seq_id % 100 == 0) {
            std::cout << "[LDP] Sent " << seq_id << " packets, " 
                     << (total_sent / 1024) << " KB" << std::endl;
        }
    }
    
    std::cout << "[LDP] File transfer completed. Total packets: " << seq_id 
             << ", Total size: " << (total_sent / 1024) << " KB" << std::endl;
}

void LDPFileTransmitter::perform_rekeying() {
    std::cout << "[LDP] Rotating keys for enhanced forward secrecy..." << std::endl;
    // Genera nuova chiave
    uint8_t new_key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    randombytes_buf(new_key, sizeof(new_key));
    crypto.set_key(new_key);
}

