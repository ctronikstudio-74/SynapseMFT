#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Public surface of the Synapse Delivery Protocol SDK (LDP-1).
// The engine is the session in the sibling ldp/ tree. This header is the
// stable entry for a local trial and a reproducible benchmark.

namespace ldp {
namespace sdk {

constexpr const char* kSdkVersion = "0.1.0";
constexpr const char* kProtocol = "LDP-1";

enum class Profile { Classic, HybridPq1 };

struct TransferOptions {
    Profile profile = Profile::Classic;
    uint32_t chunk_size = 4096;
    uint32_t rekey_every = 0;
    int ack_timeout_ms = 500;
    int idle_timeout_ms = 5000;
};

struct BenchRow {
    std::string profile;
    std::string transport;
    uint64_t payload_bytes = 0;
    uint32_t chunk_size = 0;
    uint32_t rekey_every = 0;
    double seconds = 0;
    double mebibytes_per_second = 0;
    bool match = false;
};

const char* profile_name(Profile profile);

// One process, two sessions, in-memory datagrams. Returns the bytes the server assembled.
std::vector<uint8_t> transfer_loopback(const std::vector<uint8_t>& payload, const TransferOptions& options);

BenchRow measure_loopback(const std::vector<uint8_t>& payload, const TransferOptions& options);

// Two UDP sockets on 127.0.0.1. chunk_size should stay at or below 1200 on a real network.
BenchRow measure_udp_loopback(const std::vector<uint8_t>& payload, const TransferOptions& options);

// DevOps pair. Start listen, then send from another terminal.
void udp_listen(uint16_t port, const std::string& output_path, const TransferOptions& options);
void udp_send(const std::string& host, uint16_t port, const std::vector<uint8_t>& payload, const TransferOptions& options);

// Fixed matrix used by ldp-bench. The text is the public result format, version 1.
std::vector<BenchRow> run_public_bench();
std::string format_public_bench(const std::vector<BenchRow>& rows);

}  // namespace sdk
}  // namespace ldp
