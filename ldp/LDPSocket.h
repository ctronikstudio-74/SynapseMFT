#pragma once
#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif
#include <string>
#include <vector>

class LDPSocket {
    int sock;
public:
    LDPSocket();
    void send_packet(const std::string& ip, int port, const std::vector<uint8_t>& data);
    int receive_packet(std::vector<uint8_t>& buffer, std::string& from_ip, int& from_port);
    int get_socket() const;
    ~LDPSocket();
};
