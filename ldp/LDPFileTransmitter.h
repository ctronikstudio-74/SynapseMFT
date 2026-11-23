#pragma once
#include "LDPCrypto.h"
#include "LDPSocket.h"
#include "LDPMacUtils.h"
#include <fstream>
#include <vector>
#include <iostream>

class LDPFileTransmitter {
    static const size_t CHUNK_SIZE = 16384; // 16KB
    LDPCrypto crypto;
    LDPSocket socket;
public:
    void send_file(const std::string& path, const std::string& target_ip, int port);
private:
    void perform_rekeying();
};
