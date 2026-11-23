#pragma once
#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

class LDPTransportTCP {
    int sock;
public:
    LDPTransportTCP();
    bool connect_to(const std::string& ip, int port);
    int send_data(const std::vector<uint8_t>& data);
    int receive_data(std::vector<uint8_t>& buffer);
    ~LDPTransportTCP();
};
