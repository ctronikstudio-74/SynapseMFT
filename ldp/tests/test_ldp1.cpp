#include "LDP1Session.h"
#include "pq/mlkem768.h"
#include "pq/mldsa65.h"

#include <sodium.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

int g_failures = 0;

struct Hub {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> queues[2];
    bool drop_first_quarter = false;
    std::map<uint32_t, int> data_sends;
};

class SideTransport : public Ldp1Transport {
public:
    Hub* hub = nullptr;
    int self = 0;

    SideTransport(Hub* hubIn, int selfIn) : hub(hubIn), self(selfIn) {}

    void send(std::vector<uint8_t> datagram) override {
        std::lock_guard<std::mutex> lock(hub->mu);
        if (hub->drop_first_quarter && datagram.size() >= 36 && datagram[7] == 3) {
            uint32_t seq = (uint32_t(datagram[32]) << 24) | (uint32_t(datagram[33]) << 16) |
                           (uint32_t(datagram[34]) << 8) | uint32_t(datagram[35]);
            int seen = ++hub->data_sends[seq];
            if (seen == 1 && (seq % 4) == 0) {
                return;
            }
        }
        hub->queues[1 - self].push_back(std::move(datagram));
        hub->cv.notify_all();
    }

    bool recv(std::vector<uint8_t>& datagram, int timeout_ms) override {
        std::unique_lock<std::mutex> lock(hub->mu);
        if (!hub->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
                return !hub->queues[self].empty();
            })) {
            return false;
        }
        datagram = std::move(hub->queues[self].front());
        hub->queues[self].pop_front();
        return true;
    }
};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL " << message << "\n";
        ++g_failures;
    }
}

struct IdentityKeys {
    uint8_t client_pk[LDP_MLDSA65_PUBLICKEY]{};
    uint8_t client_sk[LDP_MLDSA65_SECRETKEY]{};
    uint8_t server_pk[LDP_MLDSA65_PUBLICKEY]{};
    uint8_t server_sk[LDP_MLDSA65_SECRETKEY]{};

    IdentityKeys() {
        if (ldp_mldsa65_keypair(client_pk, client_sk) != 0 || ldp_mldsa65_keypair(server_pk, server_sk) != 0) {
            throw std::runtime_error("ml-dsa keypair failed");
        }
    }

    ~IdentityKeys() {
        sodium_memzero(client_sk, sizeof(client_sk));
        sodium_memzero(server_sk, sizeof(server_sk));
    }
};

void transfer(Ldp1Session::Profile profile, bool drop, uint32_t rekey_every) {
    Hub hub;
    hub.drop_first_quarter = drop;
    SideTransport client_side{&hub, 0};
    SideTransport server_side{&hub, 1};
    std::vector<uint8_t> received;
    std::exception_ptr error;
    const bool hybrid = profile == Ldp1Session::Profile::HybridPq1;
    std::unique_ptr<IdentityKeys> identities;
    if (hybrid) {
        identities = std::make_unique<IdentityKeys>();
    }

    std::thread server([&] {
        try {
            Ldp1Session session(Ldp1Session::Role::Server, profile, server_side, 1000);
            if (identities) {
                session.set_identity(identities->server_sk, identities->client_pk);
            }
            session.handshake();
            received = session.receive_all(3000);
        } catch (...) {
            error = std::current_exception();
        }
    });

    try {
        Ldp1Session client(Ldp1Session::Role::Client, profile, client_side, 1000);
        if (identities) {
            client.set_identity(identities->client_sk, identities->server_pk);
        }
        client.handshake();
        std::vector<uint8_t> payload(5000);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<uint8_t>(i * 17u);
        }
        client.send_all(payload, 200, rekey_every);
        server.join();
        if (error) {
            std::rethrow_exception(error);
        }
        expect(received == payload, drop ? "lossy transfer mismatch" : "clean transfer mismatch");
    } catch (...) {
        if (server.joinable()) {
            server.join();
        }
        throw;
    }
}

}  // namespace

void expect_identity_rejection() {
    Hub hub;
    SideTransport client_side{&hub, 0};
    SideTransport server_side{&hub, 1};
    IdentityKeys identities;
    uint8_t stranger[LDP_MLDSA65_PUBLICKEY];
    uint8_t stranger_sk[LDP_MLDSA65_SECRETKEY];
    if (ldp_mldsa65_keypair(stranger, stranger_sk) != 0) {
        throw std::runtime_error("ml-dsa keypair failed");
    }
    sodium_memzero(stranger_sk, sizeof(stranger_sk));
    std::exception_ptr error;
    std::thread server([&] {
        try {
            Ldp1Session session(Ldp1Session::Role::Server, Ldp1Session::Profile::HybridPq1, server_side, 300);
            session.set_identity(identities.server_sk, identities.client_pk);
            session.handshake();
        } catch (...) {
            error = std::current_exception();
        }
    });
    bool rejected = false;
    try {
        Ldp1Session client(Ldp1Session::Role::Client, Ldp1Session::Profile::HybridPq1, client_side, 300);
        client.set_identity(identities.client_sk, stranger);
        client.handshake();
    } catch (const std::runtime_error& ex) {
        rejected = std::string(ex.what()).find("identity signature rejected") != std::string::npos;
        if (!rejected) {
            std::cerr << "FAIL unexpected handshake error: " << ex.what() << "\n";
            ++g_failures;
        }
    }
    server.join();
    expect(rejected, "wrong peer identity was accepted");
}

void expect_signature_tamper_rejected() {
    uint8_t pk[LDP_MLDSA65_PUBLICKEY];
    uint8_t sk[LDP_MLDSA65_SECRETKEY];
    uint8_t sig[LDP_MLDSA65_SIGNATURE];
    const uint8_t message[] = "ldp1-close-sample";
    const uint8_t context[] = "ldp1-close";
    expect(ldp_mldsa65_keypair(pk, sk) == 0, "ml-dsa keypair");
    expect(ldp_mldsa65_sign(sig, message, sizeof(message) - 1, context, sizeof(context) - 1, sk) == 0, "ml-dsa sign");
    expect(ldp_mldsa65_verify(sig, message, sizeof(message) - 1, context, sizeof(context) - 1, pk) == 0,
           "ml-dsa verify");
    sig[0] ^= 0x01;
    expect(ldp_mldsa65_verify(sig, message, sizeof(message) - 1, context, sizeof(context) - 1, pk) != 0,
           "tampered ml-dsa signature was accepted");
    sodium_memzero(sk, sizeof(sk));
}

int main() {
    try {
        if (ldp_mldsa65_available()) {
            expect_signature_tamper_rejected();
        }
        transfer(Ldp1Session::Profile::Classic, false, 0);
        transfer(Ldp1Session::Profile::Classic, false, 4);
        transfer(Ldp1Session::Profile::Classic, true, 4);
        if (ldp_mlkem768_available() && ldp_mldsa65_available()) {
            transfer(Ldp1Session::Profile::HybridPq1, true, 4);
            expect_identity_rejection();
            std::cout << "ldp1 hybrid pq transfer passed\n";
        } else {
            std::cout << "ldp1 hybrid pq skipped: ML-KEM-768 or ML-DSA-65 is not linked\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAIL exception: " << ex.what() << "\n";
        return 1;
    }
    if (g_failures != 0) {
        return 1;
    }
    std::cout << "ldp1 session tests passed\n";
    return 0;
}
