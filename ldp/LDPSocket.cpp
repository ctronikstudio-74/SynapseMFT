#include "LDPSocket.h"
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

LDPSocket::LDPSocket() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
#ifdef _WIN32
        WSACleanup();
        throw std::runtime_error("Socket creation failed");
#else
        throw std::runtime_error("Socket creation failed");
#endif
    }
}

void LDPSocket::send_packet(const std::string& ip, int port, const std::vector<uint8_t>& data) {
    struct sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) <= 0) {
        throw std::runtime_error("Invalid IP address");
    }
    
    ssize_t sent = sendto(sock, (const char*)data.data(), data.size(), 0, 
                          (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0) {
        throw std::runtime_error("Send failed");
    }
}

int LDPSocket::receive_packet(std::vector<uint8_t>& buffer, std::string& from_ip, int& from_port) {
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    
    ssize_t received = recvfrom(sock, (char*)buffer.data(), buffer.size(), 0,
                                (struct sockaddr*)&sender, &sender_len);
    
    if (received > 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, ip_str, INET_ADDRSTRLEN);
        from_ip = ip_str;
        from_port = ntohs(sender.sin_port);
        buffer.resize(received);
    }
    
    return static_cast<int>(received);
}

int LDPSocket::get_socket() const {
    return sock;
}

LDPSocket::~LDPSocket() {
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

