#include "ldp/ldp_c.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL " << message;
        if (ldp_last_error()[0] != '\0') {
            std::cerr << ": " << ldp_last_error();
        }
        std::cerr << "\n";
        ++g_failures;
    }
}

std::vector<uint8_t> pattern(size_t n) {
    std::vector<uint8_t> bytes(n);
    for (size_t i = 0; i < n; ++i) {
        bytes[i] = static_cast<uint8_t>(i & 0xff);
    }
    return bytes;
}

std::vector<uint8_t> take(uint8_t* block, size_t len) {
    std::vector<uint8_t> out(block, block + len);
    ldp_free(block);
    return out;
}

void test_loopback(int profile) {
    const auto payload = pattern(profile == LDP_PROFILE_HYBRID ? 4096 : 8192);
    ldp_transfer_options options{};
    ldp_options_init(&options);
    options.profile = profile;
    options.chunk_size = 512;
    options.rekey_every = profile == LDP_PROFILE_CLASSIC ? 4 : 0;
    uint8_t* out = nullptr;
    size_t out_len = 0;
    check(ldp_transfer_loopback(payload.data(), payload.size(), &options, &out, &out_len) == LDP_OK, "transfer");
    if (g_failures == 0) {
        check(take(out, out_len) == payload, "transfer bytes");
    }
}

struct Hub {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> queues[2];
};

struct End {
    Hub* hub = nullptr;
    int self = 0;
};

int cb_send(void* user, const uint8_t* data, size_t len) {
    auto* end = static_cast<End*>(user);
    std::lock_guard<std::mutex> lock(end->hub->mu);
    end->hub->queues[1 - end->self].emplace_back(data, data + len);
    end->hub->cv.notify_all();
    return 0;
}

int cb_recv(void* user, uint8_t* buffer, size_t capacity, int timeout_ms, size_t* out_len) {
    auto* end = static_cast<End*>(user);
    std::unique_lock<std::mutex> lock(end->hub->mu);
    if (!end->hub->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return !end->hub->queues[end->self].empty(); })) {
        return 0;
    }
    const std::vector<uint8_t> datagram = std::move(end->hub->queues[end->self].front());
    end->hub->queues[end->self].pop_front();
    if (datagram.size() > capacity) {
        return -1;
    }
    std::memcpy(buffer, datagram.data(), datagram.size());
    *out_len = datagram.size();
    return 1;
}

void test_callbacks() {
    Hub hub;
    End ends[2];
    ends[0].hub = &hub;
    ends[0].self = 0;
    ends[1].hub = &hub;
    ends[1].self = 1;
    ldp_session* server = ldp_session_create(LDP_ROLE_SERVER, LDP_PROFILE_CLASSIC, cb_send, cb_recv, &ends[1], 500);
    ldp_session* client = ldp_session_create(LDP_ROLE_CLIENT, LDP_PROFILE_CLASSIC, cb_send, cb_recv, &ends[0], 500);
    check(server != nullptr && client != nullptr, "callback sessions");
    const auto payload = pattern(3000);
    std::vector<uint8_t> received;
    std::string server_error;
    std::thread worker([&] {
        if (ldp_session_handshake(server) != LDP_OK) {
            server_error = ldp_last_error();
            return;
        }
        uint8_t* out = nullptr;
        size_t out_len = 0;
        if (ldp_session_receive_all(server, 5000, &out, &out_len) != LDP_OK) {
            server_error = ldp_last_error();
            return;
        }
        received.assign(out, out + out_len);
        ldp_free(out);
    });
    check(ldp_session_handshake(client) == LDP_OK, "callback handshake");
    check(ldp_session_send_all(client, payload.data(), payload.size(), 700, 3) == LDP_OK, "callback send");
    worker.join();
    check(server_error.empty(), "callback server");
    check(received == payload, "callback bytes");
    check(ldp_session_established(client) == 1, "callback established");
    ldp_session_destroy(client);
    ldp_session_destroy(server);
}

void test_pipe_identity() {
    uint8_t client_pk[LDP_MLDSA_PUBLICKEY];
    uint8_t client_sk[LDP_MLDSA_SECRETKEY];
    uint8_t server_pk[LDP_MLDSA_PUBLICKEY];
    uint8_t server_sk[LDP_MLDSA_SECRETKEY];
    check(ldp_identity_generate(client_pk, client_sk) == LDP_OK, "client identity");
    check(ldp_identity_generate(server_pk, server_sk) == LDP_OK, "server identity");
    ldp_pipe* pipe = ldp_pipe_create();
    ldp_session* server = ldp_pipe_session(pipe, 1, LDP_ROLE_SERVER, LDP_PROFILE_HYBRID, 500);
    ldp_session* client = ldp_pipe_session(pipe, 0, LDP_ROLE_CLIENT, LDP_PROFILE_HYBRID, 500);
    check(server != nullptr && client != nullptr, "pipe sessions");
    check(ldp_session_set_identity(server, server_sk, LDP_MLDSA_SECRETKEY, client_pk, LDP_MLDSA_PUBLICKEY) == LDP_OK, "server identity set");
    check(ldp_session_set_identity(client, client_sk, LDP_MLDSA_SECRETKEY, server_pk, LDP_MLDSA_PUBLICKEY) == LDP_OK, "client identity set");
    check(ldp_session_set_identity(client, client_sk, 3, server_pk, LDP_MLDSA_PUBLICKEY) == LDP_ERR_ARG, "identity length");
    const auto payload = pattern(2048);
    std::vector<uint8_t> received;
    std::string server_error;
    std::thread worker([&] {
        if (ldp_session_handshake(server) != LDP_OK || ldp_session_established(server) != 1) {
            server_error = ldp_last_error();
            return;
        }
        uint8_t* out = nullptr;
        size_t out_len = 0;
        if (ldp_session_receive_all(server, 5000, &out, &out_len) != LDP_OK) {
            server_error = ldp_last_error();
            return;
        }
        received.assign(out, out + out_len);
        ldp_free(out);
    });
    check(ldp_session_handshake(client) == LDP_OK, "hybrid handshake");
    check(ldp_session_send_all(client, payload.data(), payload.size(), 256, 0) == LDP_OK, "hybrid send");
    worker.join();
    check(server_error.empty(), "hybrid server");
    check(received == payload, "hybrid pipe bytes");
    ldp_session_destroy(client);
    ldp_session_destroy(server);
    ldp_pipe_destroy(pipe);
    ldp_identity_wipe(client_sk, sizeof(client_sk));
    ldp_identity_wipe(server_sk, sizeof(server_sk));
}

void test_udp() {
    ldp_session* server = ldp_udp_session_create(LDP_ROLE_SERVER, LDP_PROFILE_CLASSIC, 0, nullptr, 0, 500);
    uint16_t port = 0;
    check(server != nullptr && ldp_udp_local_port(server, &port) == LDP_OK && port != 0, "udp bind");
    ldp_session* client = ldp_udp_session_create(LDP_ROLE_CLIENT, LDP_PROFILE_CLASSIC, 0, "127.0.0.1", port, 500);
    check(client != nullptr, "udp client");
    const auto payload = pattern(2500);
    std::vector<uint8_t> received;
    std::string server_error;
    std::thread worker([&] {
        if (ldp_session_handshake(server) != LDP_OK) {
            server_error = ldp_last_error();
            return;
        }
        uint8_t* out = nullptr;
        size_t out_len = 0;
        if (ldp_session_receive_all(server, 5000, &out, &out_len) != LDP_OK) {
            server_error = ldp_last_error();
            return;
        }
        received.assign(out, out + out_len);
        ldp_free(out);
    });
    check(ldp_session_handshake(client) == LDP_OK, "udp handshake");
    check(ldp_session_send_all(client, payload.data(), payload.size(), 1200, 0) == LDP_OK, "udp send");
    worker.join();
    check(server_error.empty(), "udp server");
    check(received == payload, "udp bytes");
    ldp_session_destroy(client);
    ldp_session_destroy(server);
}

void test_bench() {
    ldp_bench_row rows[4];
    size_t count = 0;
    check(ldp_run_public_bench(nullptr, 0, &count) == LDP_ERR_SPACE && count == 4, "bench space");
    check(ldp_run_public_bench(rows, 4, &count) == LDP_OK && count == 4, "bench run");
    char* text = nullptr;
    check(ldp_format_public_bench(rows, count, &text) == LDP_OK, "bench text");
    if (text != nullptr) {
        const std::string report(text);
        ldp_free(text);
        check(report.find("ldp-sdk bench v1") != std::string::npos, "bench header");
        check(report.find("sdk 0.2.0") != std::string::npos, "bench version");
        check(report.find("MISMATCH") == std::string::npos, "bench match");
    }
    ldp_transfer_options hybrid{};
    ldp_options_init(&hybrid);
    hybrid.profile = LDP_PROFILE_HYBRID;
    check(ldp_udp_send("127.0.0.1", 1, nullptr, 0, &hybrid) == LDP_ERR_ARG, "two-process hybrid refused");
}

}  // namespace

int main() {
    static_assert(sizeof(ldp_transfer_options) == 20, "options layout");
    static_assert(sizeof(ldp_bench_row) == 104, "bench layout");
    check(std::strcmp(ldp_version(), "0.2.0") == 0, "version");
    check(ldp_sizeof_options() == sizeof(ldp_transfer_options), "options size");
    check(ldp_sizeof_bench_row() == sizeof(ldp_bench_row), "bench size");
    test_loopback(LDP_PROFILE_CLASSIC);
    test_loopback(LDP_PROFILE_HYBRID);
    test_callbacks();
    test_pipe_identity();
    test_udp();
    test_bench();
    if (g_failures != 0) {
        std::cerr << g_failures << " ldp_c checks failed\n";
        return 1;
    }
    std::cout << "ldp_c tests passed\n";
    return 0;
}
