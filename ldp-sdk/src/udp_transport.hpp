#pragma once

#include "LDP1Session.h"

#include <cstdint>
#include <string>

namespace ldp {
namespace sdk {

class UdpTransport : public Ldp1Transport {
public:
    // Bind a local UDP port. port 0 asks the OS for a free port.
    // If remote_host is empty, the peer is learned from the first datagram.
    UdpTransport(uint16_t local_port, const std::string& remote_host, uint16_t remote_port);
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    void send(std::vector<uint8_t> datagram) override;
    bool recv(std::vector<uint8_t>& datagram, int timeout_ms) override;

    uint16_t local_port() const { return local_port_; }

private:
    void* socket_ = nullptr;
    bool peer_known_ = false;
    uint32_t peer_addr_ = 0;
    uint16_t peer_port_ = 0;
    uint16_t local_port_ = 0;
};

}  // namespace sdk
}  // namespace ldp
