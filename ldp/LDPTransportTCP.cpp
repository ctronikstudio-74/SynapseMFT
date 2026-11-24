#include "LDPTransportTCP.h"
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

LDPTransportTCP::LDPTransportTCP() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
#ifdef _WIN32
        WSACleanup();
        throw std::runtime_error("Socket creation failed");
#else
        throw std::runtime_error("Socket creation failed");
#endif
    }
}

bool LDPTransportTCP::connect_to(const std::string& ip, int port) {
    struct sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) <= 0) {
        return false;
    }
    
    int result = ::connect(sock, (struct sockaddr*)&dest, sizeof(dest));
    return result == 0;
}

int LDPTransportTCP::send_data(const std::vector<uint8_t>& data) {
    return static_cast<int>(::send(sock, (const char*)data.data(), data.size(), 0));
}

int LDPTransportTCP::receive_data(std::vector<uint8_t>& buffer) {
    const int received = ::recv(sock, (char*)buffer.data(), static_cast<int>(buffer.size()), 0);
    if (received > 0) {
        buffer.resize(received);
    }
    return static_cast<int>(received);
}

LDPTransportTCP::~LDPTransportTCP() {
#ifdef _WIN32
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        WSACleanup();
    }
#else
    if (sock >= 0) {
        close(sock);
    }
#endif
}

