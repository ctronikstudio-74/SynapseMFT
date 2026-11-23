#pragma once
#include "LDPControl.h"
#include "LDPCrypto.h"
#include "LDPSocket.h"
#include <string>
#include <vector>
#include <iostream>

class LDPRemoteCommands {
    LDPCrypto crypto;
    LDPSocket socket;
public:
    void send_ls_request(const std::string& ip, int port);
    void send_mkdir_request(const std::string& ip, int port, const std::string& dirname);
    void send_rm_request(const std::string& ip, int port, const std::string& filename);
    void handle_command_packet(const std::vector<uint8_t>& encrypted, uint8_t* nonce);
};
