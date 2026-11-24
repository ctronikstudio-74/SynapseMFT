#include "ldp/ldp_c.h"

#include "ldp/sdk.hpp"
#include "udp_transport.hpp"

#include "LDP1Session.h"
#include "pq/mldsa65.h"

#include <sodium.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

thread_local char g_error[512];

void set_error(const char* message) {
    if (message == nullptr || message[0] == '\0') {
        message = "unknown error";
    }
    std::snprintf(g_error, sizeof(g_error), "%s", message);
}

void clear_error() { g_error[0] = '\0'; }

bool ensure_sodium() {
    if (sodium_init() < 0) {
        set_error("sodium_init failed");
        return false;
    }
    return true;
}

int fail_arg(const char* message) {
    set_error(message);
    return LDP_ERR_ARG;
}

template <typename Fn>
int guard(Fn&& fn) {
    try {
        fn();
        clear_error();
        return LDP_OK;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

bool valid_role(int role) { return role == LDP_ROLE_CLIENT || role == LDP_ROLE_SERVER; }

bool valid_profile(int profile) { return profile == LDP_PROFILE_CLASSIC || profile == LDP_PROFILE_HYBRID; }

Ldp1Session::Role engine_role(int role) {
    return role == LDP_ROLE_SERVER ? Ldp1Session::Role::Server : Ldp1Session::Role::Client;
}

Ldp1Session::Profile engine_profile(int profile) {
    return profile == LDP_PROFILE_HYBRID ? Ldp1Session::Profile::HybridPq1 : Ldp1Session::Profile::Classic;
}

ldp::sdk::Profile sdk_profile(int profile) {
    return profile == LDP_PROFILE_HYBRID ? ldp::sdk::Profile::HybridPq1 : ldp::sdk::Profile::Classic;
}

ldp::sdk::TransferOptions to_sdk(const ldp_transfer_options* options) {
    ldp_transfer_options local{};
    if (options == nullptr) {
        ldp_options_init(&local);
        options = &local;
    }
    ldp::sdk::TransferOptions out;
    out.profile = sdk_profile(options->profile);
    out.chunk_size = options->chunk_size;
    out.rekey_every = options->rekey_every;
    out.ack_timeout_ms = options->ack_timeout_ms;
    out.idle_timeout_ms = options->idle_timeout_ms;
    return out;
}

int check_options(const ldp_transfer_options* options) {
    if (options == nullptr) {
        return LDP_OK;
    }
    if (!valid_profile(options->profile) || options->chunk_size == 0 || options->ack_timeout_ms <= 0 || options->idle_timeout_ms <= 0) {
        return fail_arg("invalid transfer options");
    }
    if (options->profile == LDP_PROFILE_HYBRID) {
        return fail_arg("two-process hybrid requires pinned identities; use a session and ldp_session_set_identity");
    }
    return LDP_OK;
}

void copy_field(char* dest, size_t size, const std::string& value) {
    std::memset(dest, 0, size);
    const size_t n = value.size() < size - 1 ? value.size() : size - 1;
    std::memcpy(dest, value.data(), n);
}

void fill_row(ldp_bench_row* dest, const ldp::sdk::BenchRow& src) {
    std::memset(dest, 0, sizeof(*dest));
    copy_field(dest->profile, sizeof(dest->profile), src.profile);
    copy_field(dest->transport, sizeof(dest->transport), src.transport);
    dest->payload_bytes = src.payload_bytes;
    dest->chunk_size = src.chunk_size;
    dest->rekey_every = src.rekey_every;
    dest->seconds = src.seconds;
    dest->mebibytes_per_second = src.mebibytes_per_second;
    dest->match = src.match ? 1 : 0;
}

int publish(const std::vector<uint8_t>& bytes, uint8_t** out, size_t* out_len) {
    if (out == nullptr || out_len == nullptr) {
        return fail_arg("missing output");
    }
    auto* block = static_cast<uint8_t*>(std::malloc(bytes.empty() ? 1 : bytes.size()));
    if (block == nullptr) {
        set_error("out of memory");
        return LDP_ERR;
    }
    if (!bytes.empty()) {
        std::memcpy(block, bytes.data(), bytes.size());
    }
    *out = block;
    *out_len = bytes.size();
    clear_error();
    return LDP_OK;
}

}  // namespace

namespace ldp_detail {

struct CallbackTransport : Ldp1Transport {
    ldp_send_fn send_fn = nullptr;
    ldp_recv_fn recv_fn = nullptr;
    void* user = nullptr;

    void send(std::vector<uint8_t> datagram) override {
        if (send_fn == nullptr || send_fn(user, datagram.data(), datagram.size()) != 0) {
            throw std::runtime_error("send callback failed");
        }
    }

    bool recv(std::vector<uint8_t>& datagram, int timeout_ms) override {
        std::vector<uint8_t> buffer(65535);
        size_t got = 0;
        const int rc = recv_fn(user, buffer.data(), buffer.size(), timeout_ms, &got);
        if (rc == 0) {
            return false;
        }
        if (rc < 0 || got > buffer.size()) {
            throw std::runtime_error("recv callback failed");
        }
        datagram.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got));
        return true;
    }
};

struct PipeEnd;

}  // namespace ldp_detail

struct ldp_pipe {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> queues[2];
    ldp_detail::PipeEnd* ends[2]{};
    ldp_pipe();
    ~ldp_pipe();
};

namespace ldp_detail {

struct PipeEnd : Ldp1Transport {
    ldp_pipe* pipe = nullptr;
    int self = 0;

    void send(std::vector<uint8_t> datagram) override {
        std::lock_guard<std::mutex> lock(pipe->mu);
        pipe->queues[1 - self].push_back(std::move(datagram));
        pipe->cv.notify_all();
    }

    bool recv(std::vector<uint8_t>& datagram, int timeout_ms) override {
        std::unique_lock<std::mutex> lock(pipe->mu);
        if (!pipe->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return !pipe->queues[self].empty(); })) {
            return false;
        }
        datagram = std::move(pipe->queues[self].front());
        pipe->queues[self].pop_front();
        return true;
    }
};

}  // namespace ldp_detail

ldp_pipe::ldp_pipe() {
    for (int i = 0; i < 2; ++i) {
        ends[i] = new ldp_detail::PipeEnd();
        ends[i]->pipe = this;
        ends[i]->self = i;
    }
}

ldp_pipe::~ldp_pipe() {
    delete ends[0];
    delete ends[1];
}

struct ldp_session {
    std::unique_ptr<Ldp1Transport> owned;
    std::unique_ptr<Ldp1Session> engine;
    uint16_t udp_port = 0;
    bool udp = false;
};

namespace {

ldp_session* open_session(Ldp1Transport& transport, int role, int profile, int ack_timeout_ms) {
    if (ack_timeout_ms <= 0) {
        ack_timeout_ms = 500;
    }
    auto session = std::unique_ptr<ldp_session>(new ldp_session());
    session->engine = std::make_unique<Ldp1Session>(engine_role(role), engine_profile(profile), transport, ack_timeout_ms);
    return session.release();
}

}  // namespace

extern "C" {

const char* ldp_version(void) { return LDP_C_VERSION; }

const char* ldp_last_error(void) { return g_error; }

size_t ldp_sizeof_options(void) { return sizeof(ldp_transfer_options); }

size_t ldp_sizeof_bench_row(void) { return sizeof(ldp_bench_row); }

void ldp_free(void* block) { std::free(block); }

void ldp_options_init(ldp_transfer_options* options) {
    if (options == nullptr) {
        return;
    }
    options->profile = LDP_PROFILE_CLASSIC;
    options->chunk_size = 4096;
    options->rekey_every = 0;
    options->ack_timeout_ms = 500;
    options->idle_timeout_ms = 5000;
}

int ldp_identity_generate(uint8_t public_key[LDP_MLDSA_PUBLICKEY], uint8_t secret_key[LDP_MLDSA_SECRETKEY]) {
    if (public_key == nullptr || secret_key == nullptr) {
        return fail_arg("missing identity buffer");
    }
    if (!ensure_sodium()) {
        return LDP_ERR;
    }
    if (ldp_mldsa65_keypair(public_key, secret_key) != 0) {
        set_error("ML-DSA-65 keypair failed");
        return LDP_ERR;
    }
    clear_error();
    return LDP_OK;
}

void ldp_identity_wipe(uint8_t* secret_key, size_t len) {
    if (secret_key != nullptr && len > 0) {
        sodium_memzero(secret_key, len);
    }
}

ldp_session* ldp_session_create(int role, int profile, ldp_send_fn send, ldp_recv_fn recv, void* user, int ack_timeout_ms) {
    if (!valid_role(role) || !valid_profile(profile) || send == nullptr || recv == nullptr) {
        fail_arg("invalid session arguments");
        return nullptr;
    }
    try {
        auto transport = std::make_unique<ldp_detail::CallbackTransport>();
        transport->send_fn = send;
        transport->recv_fn = recv;
        transport->user = user;
        ldp_session* session = open_session(*transport, role, profile, ack_timeout_ms);
        session->owned = std::move(transport);
        clear_error();
        return session;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return nullptr;
    } catch (...) {
        set_error("unknown error");
        return nullptr;
    }
}

ldp_session* ldp_udp_session_create(int role, int profile, uint16_t local_port, const char* remote_host, uint16_t remote_port, int ack_timeout_ms) {
    if (!valid_role(role) || !valid_profile(profile)) {
        fail_arg("invalid session arguments");
        return nullptr;
    }
    try {
        if (!ensure_sodium()) {
            return nullptr;
        }
        const std::string host = remote_host == nullptr ? std::string() : std::string(remote_host);
        auto transport = std::make_unique<ldp::sdk::UdpTransport>(local_port, host, remote_port);
        const uint16_t bound = transport->local_port();
        ldp_session* session = open_session(*transport, role, profile, ack_timeout_ms);
        session->owned = std::move(transport);
        session->udp = true;
        session->udp_port = bound;
        clear_error();
        return session;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return nullptr;
    } catch (...) {
        set_error("unknown error");
        return nullptr;
    }
}

int ldp_udp_local_port(ldp_session* session, uint16_t* port) {
    if (session == nullptr || port == nullptr || !session->udp) {
        return fail_arg("not a udp session");
    }
    *port = session->udp_port;
    clear_error();
    return LDP_OK;
}

void ldp_session_destroy(ldp_session* session) { delete session; }

int ldp_session_set_identity(ldp_session* session, const uint8_t* secret_key, size_t secret_len, const uint8_t* peer_public_key, size_t public_len) {
    if (session == nullptr || secret_key == nullptr || peer_public_key == nullptr) {
        return fail_arg("missing identity");
    }
    if (secret_len != LDP_MLDSA_SECRETKEY || public_len != LDP_MLDSA_PUBLICKEY) {
        return fail_arg("identity length");
    }
    return guard([&] { session->engine->set_identity(secret_key, peer_public_key); });
}

int ldp_session_handshake(ldp_session* session) {
    if (session == nullptr) {
        return fail_arg("missing session");
    }
    return guard([&] { session->engine->handshake(); });
}

int ldp_session_established(ldp_session* session) {
    if (session == nullptr) {
        set_error("missing session");
        return 0;
    }
    return session->engine->established() ? 1 : 0;
}

int ldp_session_send_all(ldp_session* session, const uint8_t* data, size_t len, uint32_t chunk_size, uint32_t rekey_every) {
    if (session == nullptr || (data == nullptr && len > 0) || chunk_size == 0) {
        return fail_arg("invalid send");
    }
    return guard([&] {
        const std::vector<uint8_t> payload(data, data + len);
        session->engine->send_all(payload, chunk_size, rekey_every);
    });
}

int ldp_session_receive_all(ldp_session* session, int idle_timeout_ms, uint8_t** out, size_t* out_len) {
    if (session == nullptr || out == nullptr || out_len == nullptr || idle_timeout_ms <= 0) {
        return fail_arg("invalid receive");
    }
    try {
        const std::vector<uint8_t> received = session->engine->receive_all(idle_timeout_ms);
        return publish(received, out, out_len);
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

ldp_pipe* ldp_pipe_create(void) {
    try {
        clear_error();
        return new ldp_pipe();
    } catch (...) {
        set_error("out of memory");
        return nullptr;
    }
}

void ldp_pipe_destroy(ldp_pipe* pipe) { delete pipe; }

ldp_session* ldp_pipe_session(ldp_pipe* pipe, int end, int role, int profile, int ack_timeout_ms) {
    if (pipe == nullptr || (end != 0 && end != 1) || !valid_role(role) || !valid_profile(profile)) {
        fail_arg("invalid pipe session");
        return nullptr;
    }
    try {
        ldp_session* session = open_session(*pipe->ends[end], role, profile, ack_timeout_ms);
        clear_error();
        return session;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return nullptr;
    } catch (...) {
        set_error("unknown error");
        return nullptr;
    }
}

int ldp_transfer_loopback(const uint8_t* payload, size_t len, const ldp_transfer_options* options, uint8_t** out, size_t* out_len) {
    if ((payload == nullptr && len > 0) || out == nullptr || out_len == nullptr) {
        return fail_arg("invalid transfer");
    }
    if (options != nullptr && (!valid_profile(options->profile) || options->chunk_size == 0 || options->ack_timeout_ms <= 0 || options->idle_timeout_ms <= 0)) {
        return fail_arg("invalid transfer options");
    }
    try {
        const std::vector<uint8_t> input(payload, payload + len);
        const std::vector<uint8_t> received = ldp::sdk::transfer_loopback(input, to_sdk(options));
        return publish(received, out, out_len);
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

int ldp_measure_loopback(const uint8_t* payload, size_t len, const ldp_transfer_options* options, ldp_bench_row* row) {
    if ((payload == nullptr && len > 0) || row == nullptr) {
        return fail_arg("invalid measure");
    }
    if (options != nullptr && (!valid_profile(options->profile) || options->chunk_size == 0)) {
        return fail_arg("invalid transfer options");
    }
    try {
        const std::vector<uint8_t> input(payload, payload + len);
        fill_row(row, ldp::sdk::measure_loopback(input, to_sdk(options)));
        clear_error();
        return LDP_OK;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

int ldp_measure_udp_loopback(const uint8_t* payload, size_t len, const ldp_transfer_options* options, ldp_bench_row* row) {
    if ((payload == nullptr && len > 0) || row == nullptr) {
        return fail_arg("invalid measure");
    }
    if (options != nullptr && (!valid_profile(options->profile) || options->chunk_size == 0)) {
        return fail_arg("invalid transfer options");
    }
    try {
        const std::vector<uint8_t> input(payload, payload + len);
        fill_row(row, ldp::sdk::measure_udp_loopback(input, to_sdk(options)));
        clear_error();
        return LDP_OK;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

int ldp_udp_listen(uint16_t port, const char* output_path, const ldp_transfer_options* options) {
    const int checked = check_options(options);
    if (checked != LDP_OK) {
        return checked;
    }
    return guard([&] { ldp::sdk::udp_listen(port, output_path == nullptr ? std::string() : std::string(output_path), to_sdk(options)); });
}

int ldp_udp_send(const char* host, uint16_t port, const uint8_t* payload, size_t len, const ldp_transfer_options* options) {
    if (host == nullptr || (payload == nullptr && len > 0)) {
        return fail_arg("invalid udp send");
    }
    const int checked = check_options(options);
    if (checked != LDP_OK) {
        return checked;
    }
    return guard([&] {
        const std::vector<uint8_t> input(payload, payload + len);
        ldp::sdk::udp_send(host, port, input, to_sdk(options));
    });
}

int ldp_run_public_bench(ldp_bench_row* rows, size_t capacity, size_t* out_count) {
    if (out_count == nullptr) {
        return fail_arg("missing count");
    }
    try {
        const std::vector<ldp::sdk::BenchRow> result = ldp::sdk::run_public_bench();
        *out_count = result.size();
        if (rows == nullptr || capacity < result.size()) {
            set_error("bench buffer is short");
            return LDP_ERR_SPACE;
        }
        for (size_t i = 0; i < result.size(); ++i) {
            fill_row(&rows[i], result[i]);
        }
        clear_error();
        return LDP_OK;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

int ldp_format_public_bench(const ldp_bench_row* rows, size_t count, char** text) {
    if (text == nullptr || (rows == nullptr && count > 0)) {
        return fail_arg("invalid bench text");
    }
    try {
        std::vector<ldp::sdk::BenchRow> input;
        input.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ldp::sdk::BenchRow row;
            row.profile = rows[i].profile;
            row.transport = rows[i].transport;
            row.payload_bytes = rows[i].payload_bytes;
            row.chunk_size = rows[i].chunk_size;
            row.rekey_every = rows[i].rekey_every;
            row.seconds = rows[i].seconds;
            row.mebibytes_per_second = rows[i].mebibytes_per_second;
            row.match = rows[i].match != 0;
            input.push_back(std::move(row));
        }
        const std::string formatted = ldp::sdk::format_public_bench(input);
        auto* block = static_cast<char*>(std::malloc(formatted.size() + 1));
        if (block == nullptr) {
            set_error("out of memory");
            return LDP_ERR;
        }
        std::memcpy(block, formatted.data(), formatted.size() + 1);
        *text = block;
        clear_error();
        return LDP_OK;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return LDP_ERR;
    } catch (...) {
        set_error("unknown error");
        return LDP_ERR;
    }
}

}  // extern "C"
