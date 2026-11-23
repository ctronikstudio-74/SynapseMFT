#include "udp_transport.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace ldp {
namespace sdk {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalid = INVALID_SOCKET;
int last_error() { return WSAGetLastError(); }
void close_socket(SocketHandle s) { closesocket(s); }
constexpr int kTimedOut = WSAETIMEDOUT;
constexpr int kWouldBlock = WSAEWOULDBLOCK;
constexpr int kConnReset = WSAECONNRESET;

struct WsaOnce {
    WsaOnce() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WsaOnce() { WSACleanup(); }
};
void ensure_wsa() { static WsaOnce once; }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalid = -1;
int last_error() { return errno; }
void close_socket(SocketHandle s) { ::close(s); }
constexpr int kTimedOut = EAGAIN;
constexpr int kWouldBlock = EWOULDBLOCK;
constexpr int kConnReset = ECONNREFUSED;
void ensure_wsa() {}
#endif

void set_timeout(SocketHandle socket, int timeout_ms) {
#ifdef _WIN32
    DWORD ms = timeout_ms < 0 ? 0 : static_cast<DWORD>(timeout_ms);
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms)) != 0) {
        throw std::runtime_error("SO_RCVTIMEO failed");
    }
#else
    timeval tv{};
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        throw std::runtime_error("SO_RCVTIMEO failed");
    }
#endif
}

}  // namespace

UdpTransport::UdpTransport(uint16_t local_port, const std::string& remote_host, uint16_t remote_port) {
    ensure_wsa();
    socket_ = reinterpret_cast<void*>(static_cast<uintptr_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)));
    auto handle = static_cast<SocketHandle>(reinterpret_cast<uintptr_t>(socket_));
    if (handle == kInvalid) {
        throw std::runtime_error("udp socket failed");
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(local_port);
    if (!remote_host.empty() && remote_host != "127.0.0.1" && remote_host != "localhost") {
        local.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        close_socket(handle);
        throw std::runtime_error("udp bind failed");
    }
    sockaddr_in bound{};
#ifdef _WIN32
    int bound_len = sizeof(bound);
#else
    socklen_t bound_len = sizeof(bound);
#endif
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        close_socket(handle);
        throw std::runtime_error("udp getsockname failed");
    }
    local_port_ = ntohs(bound.sin_port);
    int buffer = 1 << 20;
    setsockopt(handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    setsockopt(handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    if (!remote_host.empty()) {
        in_addr addr{};
        if (inet_pton(AF_INET, remote_host.c_str(), &addr) != 1) {
            close_socket(handle);
            throw std::runtime_error("udp host is not an IPv4 address");
        }
        peer_addr_ = addr.s_addr;
        peer_port_ = htons(remote_port);
        peer_known_ = true;
    }
}

UdpTransport::~UdpTransport() {
    auto handle = static_cast<SocketHandle>(reinterpret_cast<uintptr_t>(socket_));
    if (handle != kInvalid) {
        close_socket(handle);
    }
}

void UdpTransport::send(std::vector<uint8_t> datagram) {
    if (!peer_known_) {
        throw std::runtime_error("udp peer is not known yet");
    }
    auto handle = static_cast<SocketHandle>(reinterpret_cast<uintptr_t>(socket_));
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = peer_addr_;
    peer.sin_port = peer_port_;
    const int sent = sendto(handle, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(datagram.size()), 0,
                             reinterpret_cast<sockaddr*>(&peer), sizeof(peer));
    if (sent < 0 || static_cast<size_t>(sent) != datagram.size()) {
        throw std::runtime_error("udp send failed");
    }
}

bool UdpTransport::recv(std::vector<uint8_t>& datagram, int timeout_ms) {
    auto handle = static_cast<SocketHandle>(reinterpret_cast<uintptr_t>(socket_));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
    std::vector<uint8_t> buffer(65536);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            return false;
        }
        set_timeout(handle, static_cast<int>(left));
        sockaddr_in from{};
#ifdef _WIN32
        int from_len = sizeof(from);
#else
        socklen_t from_len = sizeof(from);
#endif
        const int got = recvfrom(handle, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
        if (got >= 0) {
            if (!peer_known_) {
                peer_addr_ = from.sin_addr.s_addr;
                peer_port_ = from.sin_port;
                peer_known_ = true;
            }
            datagram.assign(buffer.begin(), buffer.begin() + got);
            return true;
        }
        const int err = last_error();
        if (err == kConnReset) {
            continue;
        }
        if (err == kTimedOut || err == kWouldBlock) {
            return false;
        }
        throw std::runtime_error("udp recv failed");
    }
}

}  // namespace sdk
}  // namespace ldp
