#include "LDPRemoteCommands.h"
#include <cstring>

void LDPRemoteCommands::send_ls_request(const std::string& ip, int port) {
    LDPControlHeader header{LDPCommandType::LS_REQUEST, 0};
    std::vector<uint8_t> payload(sizeof(header));
    std::memcpy(payload.data(), &header, sizeof(header));
    uint8_t nonce[24];
    randombytes_buf(nonce, sizeof(nonce));
    auto encrypted = crypto.encrypt(payload, nonce, 0);
    socket.send_packet(ip, port, encrypted);
    std::cout << "LS request sent." << std::endl;
}

void LDPRemoteCommands::send_mkdir_request(const std::string& ip, int port, const std::string& dirname) {
    LDPControlHeader header{LDPCommandType::MKDIR_REQ, static_cast<uint32_t>(dirname.size())};
    std::vector<uint8_t> payload(sizeof(header) + dirname.size());
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), dirname.data(), dirname.size());
    uint8_t nonce[24];
    randombytes_buf(nonce, sizeof(nonce));
    auto encrypted = crypto.encrypt(payload, nonce, 0);
    socket.send_packet(ip, port, encrypted);
    std::cout << "MKDIR request sent." << std::endl;
}

void LDPRemoteCommands::send_rm_request(const std::string& ip, int port, const std::string& filename) {
    LDPControlHeader header{LDPCommandType::RM_REQUEST, static_cast<uint32_t>(filename.size())};
    std::vector<uint8_t> payload(sizeof(header) + filename.size());
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), filename.data(), filename.size());
    uint8_t nonce[24];
    randombytes_buf(nonce, sizeof(nonce));
    auto encrypted = crypto.encrypt(payload, nonce, 0);
    socket.send_packet(ip, port, encrypted);
    std::cout << "RM request sent." << std::endl;
}

void LDPRemoteCommands::handle_command_packet(const std::vector<uint8_t>& encrypted, uint8_t* nonce) {
    auto decrypted = crypto.decrypt(encrypted, nonce, 0);
    LDPControlHeader header;
    std::memcpy(&header, decrypted.data(), sizeof(header));
    switch (header.type) {
        case LDPCommandType::LS_REQUEST:
            std::cout << "Received LS request" << std::endl;
            break;
        case LDPCommandType::MKDIR_REQ:
            std::cout << "Received MKDIR request" << std::endl;
            break;
        case LDPCommandType::RM_REQUEST:
            std::cout << "Received RM request" << std::endl;
            break;
        default:
            std::cout << "Unknown or error command" << std::endl;
    }
}

