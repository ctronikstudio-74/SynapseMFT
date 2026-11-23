#include "ldp/sdk.hpp"

#include "udp_transport.hpp"

#include "LDP1Session.h"
#include "pq/mldsa65.h"

#include <sodium.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace ldp {
namespace sdk {
namespace {

struct Hub {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> queues[2];
};

class SideTransport : public Ldp1Transport {
public:
    Hub* hub = nullptr;
    int self = 0;

    void send(std::vector<uint8_t> datagram) override {
        std::lock_guard<std::mutex> lock(hub->mu);
        hub->queues[1 - self].push_back(std::move(datagram));
        hub->cv.notify_all();
    }

    bool recv(std::vector<uint8_t>& datagram, int timeout_ms) override {
        std::unique_lock<std::mutex> lock(hub->mu);
        if (!hub->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return !hub->queues[self].empty(); })) {
            return false;
        }
        datagram = std::move(hub->queues[self].front());
        hub->queues[self].pop_front();
        return true;
    }
};

struct Identity {
    uint8_t client_pk[LDP_MLDSA65_PUBLICKEY]{};
    uint8_t client_sk[LDP_MLDSA65_SECRETKEY]{};
    uint8_t server_pk[LDP_MLDSA65_PUBLICKEY]{};
    uint8_t server_sk[LDP_MLDSA65_SECRETKEY]{};

    Identity() {
        if (ldp_mldsa65_keypair(client_pk, client_sk) != 0 || ldp_mldsa65_keypair(server_pk, server_sk) != 0) {
            throw std::runtime_error("ML-DSA-65 keypair failed");
        }
    }
    ~Identity() {
        sodium_memzero(client_sk, sizeof(client_sk));
        sodium_memzero(server_sk, sizeof(server_sk));
    }
};

Ldp1Session::Profile to_engine(Profile profile) {
    return profile == Profile::HybridPq1 ? Ldp1Session::Profile::HybridPq1 : Ldp1Session::Profile::Classic;
}

std::vector<uint8_t> run_pair(Ldp1Transport& client_io, Ldp1Transport& server_io, const std::vector<uint8_t>& payload,
                              const TransferOptions& options) {
    if (sodium_init() < 0) {
        throw std::runtime_error("sodium_init failed");
    }
    const auto engine_profile = to_engine(options.profile);
    const bool hybrid = options.profile == Profile::HybridPq1;
    std::unique_ptr<Identity> identity;
    if (hybrid) {
        identity = std::make_unique<Identity>();
    }
    std::vector<uint8_t> received;
    std::exception_ptr server_error;
    std::thread server([&] {
        try {
            Ldp1Session session(Ldp1Session::Role::Server, engine_profile, server_io, options.ack_timeout_ms);
            if (identity) {
                session.set_identity(identity->server_sk, identity->client_pk);
            }
            session.handshake();
            received = session.receive_all(options.idle_timeout_ms);
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    std::exception_ptr client_error;
    try {
        Ldp1Session session(Ldp1Session::Role::Client, engine_profile, client_io, options.ack_timeout_ms);
        if (identity) {
            session.set_identity(identity->client_sk, identity->server_pk);
        }
        session.handshake();
        session.send_all(payload, options.chunk_size, options.rekey_every);
    } catch (...) {
        client_error = std::current_exception();
    }
    server.join();
    if (client_error) {
        std::rethrow_exception(client_error);
    }
    if (server_error) {
        std::rethrow_exception(server_error);
    }
    return received;
}

BenchRow row_from(Profile profile, const char* transport, const std::vector<uint8_t>& payload, const std::vector<uint8_t>& received,
                  const TransferOptions& options, double seconds) {
    BenchRow row;
    row.profile = profile_name(profile);
    row.transport = transport;
    row.payload_bytes = payload.size();
    row.chunk_size = options.chunk_size;
    row.rekey_every = options.rekey_every;
    row.seconds = seconds;
    row.match = received == payload;
    if (seconds > 0) {
        row.mebibytes_per_second = (static_cast<double>(payload.size()) / (1024.0 * 1024.0)) / seconds;
    }
    return row;
}

std::vector<uint8_t> pattern(size_t bytes) {
    std::vector<uint8_t> payload(bytes);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xff);
    }
    return payload;
}

}  // namespace

const char* profile_name(Profile profile) {
    return profile == Profile::HybridPq1 ? "hybrid-pq1" : "classic";
}

std::vector<uint8_t> transfer_loopback(const std::vector<uint8_t>& payload, const TransferOptions& options) {
    Hub hub;
    SideTransport client;
    SideTransport server;
    client.hub = &hub;
    client.self = 0;
    server.hub = &hub;
    server.self = 1;
    return run_pair(client, server, payload, options);
}

BenchRow measure_loopback(const std::vector<uint8_t>& payload, const TransferOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    const auto received = transfer_loopback(payload, options);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return row_from(options.profile, "memory", payload, received, options, seconds);
}

BenchRow measure_udp_loopback(const std::vector<uint8_t>& payload, const TransferOptions& options) {
    UdpTransport server_io(0, "", 0);
    UdpTransport client_io(0, "127.0.0.1", server_io.local_port());
    const auto start = std::chrono::steady_clock::now();
    const auto received = run_pair(client_io, server_io, payload, options);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return row_from(options.profile, "udp-loopback", payload, received, options, seconds);
}

void udp_listen(uint16_t port, const std::string& output_path, const TransferOptions& options) {
    if (sodium_init() < 0) {
        throw std::runtime_error("sodium_init failed");
    }
    UdpTransport transport(port, "", 0);
    Ldp1Session session(Ldp1Session::Role::Server, to_engine(options.profile), transport, options.ack_timeout_ms);
    session.handshake();
    const auto received = session.receive_all(options.idle_timeout_ms);
    if (!output_path.empty()) {
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("cannot write output file");
        }
        out.write(reinterpret_cast<const char*>(received.data()), static_cast<std::streamsize>(received.size()));
    }
}

void udp_send(const std::string& host, uint16_t port, const std::vector<uint8_t>& payload, const TransferOptions& options) {
    if (sodium_init() < 0) {
        throw std::runtime_error("sodium_init failed");
    }
    UdpTransport transport(0, host, port);
    Ldp1Session session(Ldp1Session::Role::Client, to_engine(options.profile), transport, options.ack_timeout_ms);
    session.handshake();
    session.send_all(payload, options.chunk_size, options.rekey_every);
}

std::vector<BenchRow> run_public_bench() {
    std::vector<BenchRow> rows;
    TransferOptions classic;
    classic.profile = Profile::Classic;
    classic.chunk_size = 4096;
    rows.push_back(measure_loopback(pattern(1 << 20), classic));

    TransferOptions rekey = classic;
    rekey.rekey_every = 64;
    rows.push_back(measure_loopback(pattern(1 << 20), rekey));

    TransferOptions hybrid;
    hybrid.profile = Profile::HybridPq1;
    hybrid.chunk_size = 4096;
    rows.push_back(measure_loopback(pattern(256 * 1024), hybrid));

    TransferOptions udp = classic;
    udp.chunk_size = 1200;
    rows.push_back(measure_udp_loopback(pattern(1 << 20), udp));
    return rows;
}

std::string format_public_bench(const std::vector<BenchRow>& rows) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "# ldp-sdk bench v1\n";
    out << "# sdk " << kSdkVersion << "\n";
    out << "# protocol " << kProtocol << " Synapse Delivery Protocol\n";
    out << "# columns profile transport payload_bytes chunk_size rekey_every seconds MiB/s match\n";
    out << "# memory and udp-loopback are same-host measurements, not a wide-area network\n";
    for (const auto& row : rows) {
        out << row.profile << " " << row.transport << " " << row.payload_bytes << " " << row.chunk_size << " "
            << row.rekey_every << " " << row.seconds << " " << row.mebibytes_per_second << " "
            << (row.match ? "match" : "MISMATCH") << "\n";
    }
    return out.str();
}

}  // namespace sdk
}  // namespace ldp
