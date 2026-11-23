#include "synapse/amqp.hpp"
#include "synapse/bus.hpp"
#include "synapse/cloud.hpp"
#include "synapse/codec.hpp"
#include "synapse/crypto.hpp"
#include "synapse/kafka.hpp"
#include "synapse/model.hpp"
#include "synapse/net.hpp"
#include "synapse/redis.hpp"

#include <filesystem>
#include <iostream>
#include <thread>

namespace {

int g_failed = 0;

void check(bool condition, const char* text, int line) {
    if (!condition) {
        std::cerr << "FAIL " << line << " " << text << "\n";
        ++g_failed;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::filesystem::path fresh_dir(const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void test_codecs() {
    const auto abc = synapse::codec::sha256("abc");
    CHECK(synapse::codec::hex_encode(abc.data(), abc.size()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::vector<uint8_t> key(20, 0x0b);
    const auto mac = synapse::codec::hmac_sha256(key.data(), key.size(), reinterpret_cast<const uint8_t*>("Hi There"), 8);
    CHECK(synapse::codec::hex_encode(mac.data(), mac.size()) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const auto crc = synapse::codec::crc32c(reinterpret_cast<const uint8_t*>("123456789"), 9);
    CHECK(crc == 0xE3069283u);
    synapse::bus::EventEnvelope event;
    event.id = "id1";
    event.correlation = "corr";
    event.actor.kind = synapse::bus::ActorKind::Agent;
    event.actor.id = "agent";
    event.verb = synapse::bus::Verb::Observe;
    event.topic = "obs.file";
    event.subject = "file-1";
    event.payload = "quote \" here";
    auto parsed = synapse::bus::parse_envelope(synapse::bus::canonical_envelope(event));
    CHECK(parsed.ok());
    CHECK(parsed.value.payload == event.payload);
    const auto batch = synapse::kafka::encode_record_batch("", "payload", 0);
    CHECK(batch.size() > 21);
    const uint32_t stored = (uint32_t(batch[17]) << 24) | (uint32_t(batch[18]) << 16) | (uint32_t(batch[19]) << 8) |
                            uint32_t(batch[20]);
    CHECK(stored == synapse::kafka::record_batch_crc(batch));
}

void test_bus() {
    const auto dir = fresh_dir("synapse-bus-core");
    synapse::bus::AgenticEventBus bus;
    auto opened = bus.open(dir);
    CHECK(opened.ok());
    synapse::bus::CapabilityCard node;
    node.id = "node-1";
    node.publish_prefixes = {"obs."};
    node.subscribe_prefixes = {"obs."};
    synapse::bus::CapabilityCard agent;
    agent.id = "agent-1";
    agent.publish_prefixes = {"act."};
    agent.subscribe_prefixes = {"obs.", "act."};
    agent.may_propose = true;
    agent.may_approve = true;
    synapse::bus::CapabilityCard human;
    human.id = "human-1";
    human.subscribe_prefixes = {"act."};
    human.may_approve = true;
    CHECK(bus.register_agent(node).ok());
    CHECK(bus.register_agent(agent).ok());
    CHECK(bus.register_agent(human).ok());
    int executed = 0;
    bus.set_executor([&](const synapse::bus::EventEnvelope&) {
        ++executed;
        return synapse::Status::success();
    });
    synapse::bus::EventEnvelope direct;
    direct.actor.id = "agent-1";
    direct.verb = synapse::bus::Verb::Act;
    direct.topic = "act.transfer";
    CHECK(!bus.publish(direct).ok());
    synapse::bus::EventEnvelope observed;
    observed.actor.kind = synapse::bus::ActorKind::Node;
    observed.actor.id = "node-1";
    observed.verb = synapse::bus::Verb::Observe;
    observed.topic = "obs.file";
    observed.subject = "file-1";
    observed.correlation = "job-1";
    observed.payload = "please transfer";
    CHECK(bus.publish(observed).ok());
    synapse::bus::EventEnvelope early;
    early.actor.id = "agent-1";
    early.verb = synapse::bus::Verb::Propose;
    early.topic = "act.transfer";
    early.subject = "file-1";
    CHECK(bus.publish(early).error.find("lease") != std::string::npos);
    CHECK(bus.claim("file-1", "agent-1", 60000).ok());
    CHECK(!bus.claim("file-1", "human-1", 60000).ok());
    early.requires_approval = true;
    early.deadline_ms = synapse::bus::now_ms() + 60000;
    early.correlation = "job-1";
    CHECK(bus.publish(early).ok());
    CHECK(executed == 0);
    synapse::bus::EventEnvelope self_approve;
    self_approve.actor.id = "agent-1";
    self_approve.verb = synapse::bus::Verb::Approve;
    self_approve.causation = early.id.empty() ? bus.records().back().id : early.id;
    const auto proposal_id = bus.records().back().id;
    self_approve.causation = proposal_id;
    self_approve.topic = "act.approve";
    CHECK(bus.publish(self_approve).error.find("four-eyes") != std::string::npos);
    synapse::bus::EventEnvelope approved;
    approved.actor.kind = synapse::bus::ActorKind::Human;
    approved.actor.id = "human-1";
    approved.verb = synapse::bus::Verb::Approve;
    approved.causation = proposal_id;
    approved.topic = "act.approve";
    CHECK(bus.publish(approved).ok());
    CHECK(executed == 1);
    auto memory = bus.compact("job-1", 4);
    CHECK(memory.ok());
    CHECK(memory.value.verb == synapse::bus::Verb::Memory);
    bus.close();
    synapse::bus::AgenticEventBus reopened;
    CHECK(reopened.open(dir).ok());
    CHECK(reopened.register_agent(agent).ok());
    CHECK(reopened.commit_cursor("agent-1", reopened.records().back().offset).ok());
    CHECK(reopened.replay("agent-1").empty());
}

void test_model_and_expiry() {
    const auto dir = fresh_dir("synapse-bus-model");
    synapse::bus::AgenticEventBus bus;
    CHECK(bus.open(dir).ok());
    synapse::bus::CapabilityCard node{"node-1", {"obs."}, {"obs."}, false, false, false, 65536};
    synapse::bus::CapabilityCard agent{"planner", {"act.", "obs."}, {"obs.", "act."}, true, false, false, 65536};
    CHECK(bus.register_agent(node).ok());
    CHECK(bus.register_agent(agent).ok());
    synapse::bus::EventEnvelope observed;
    observed.actor.id = "node-1";
    observed.actor.kind = synapse::bus::ActorKind::Node;
    observed.verb = synapse::bus::Verb::Observe;
    observed.topic = "obs.file";
    observed.subject = "file-9";
    observed.payload = "transfer the file";
    observed.correlation = "job-9";
    CHECK(bus.publish(observed).ok());
    synapse::bus::LinearScorer scorer;
    scorer.weights = {{"transfer", 2}, {"delete", -5}};
    scorer.threshold = 1;
    synapse::bus::PolicyRule rule;
    rule.payload_substring = "transfer";
    synapse::bus::EmbeddedAgent embedded(bus, agent, synapse::bus::EmbeddedPolicyModel({rule}, scorer));
    CHECK(embedded.tick().ok());
    bool proposed = false;
    for (const auto& event : bus.records()) {
        if (event.verb == synapse::bus::Verb::Propose && event.actor.model == "embedded-linear-v1") proposed = true;
    }
    CHECK(proposed);
    synapse::bus::EventEnvelope expired;
    expired.actor.id = "planner";
    expired.verb = synapse::bus::Verb::Propose;
    expired.topic = "act.transfer";
    expired.subject = "file-8";
    expired.deadline_ms = 1;
    expired.requires_approval = true;
    CHECK(bus.claim("file-8", "planner", 60000).ok());
    CHECK(bus.publish(expired).ok());
    CHECK(bus.sweep(synapse::bus::now_ms()).ok());
    bool saw_expire = false;
    for (const auto& event : bus.records()) {
        if (event.verb == synapse::bus::Verb::Expire) saw_expire = true;
    }
    CHECK(saw_expire);
}

void test_redis() {
    auto listener = synapse::net::TcpListener::listen("127.0.0.1", 0);
    CHECK(listener.ok());
    if (!listener.ok()) return;
    std::thread server([&] {
        auto client = listener.value.accept(3000);
        if (!client.ok()) return;
        std::string buffer;
        int seen = 0;
        while (seen < 2) {
            uint8_t chunk[1024];
            auto read = client.value.read_some(chunk, sizeof(chunk), 3000);
            if (!read.ok() || read.value == 0) return;
            buffer.append(reinterpret_cast<char*>(chunk), read.value);
            auto parsed = synapse::redis::parse_resp(buffer);
            if (!parsed.ok()) continue;
            buffer.erase(0, parsed.value.second);
            ++seen;
            const std::string reply = seen == 1 ? "+PONG\r\n" : "$3\r\n1-0\r\n";
            client.value.write_all(reply);
        }
    });
    synapse::redis::Config config;
    config.port = listener.value.port();
    synapse::redis::Client client(config);
    CHECK(client.connect().ok());
    synapse::bus::EventEnvelope event;
    event.id = "id-redis";
    event.actor.id = "agent";
    event.verb = synapse::bus::Verb::Observe;
    event.topic = "obs.file";
    CHECK(client.xadd(event).ok());
    server.join();
}

void test_kafka() {
    auto listener = synapse::net::TcpListener::listen("127.0.0.1", 0);
    CHECK(listener.ok());
    if (!listener.ok()) return;
    std::thread server([&] {
        auto client = listener.value.accept(3000);
        if (!client.ok()) return;
        for (int turn = 0; turn < 2; ++turn) {
            uint8_t size_bytes[4];
            if (!client.value.read_exact(size_bytes, 4, 3000).ok()) return;
            const uint32_t size = (uint32_t(size_bytes[0]) << 24) | (uint32_t(size_bytes[1]) << 16) |
                                  (uint32_t(size_bytes[2]) << 8) | uint32_t(size_bytes[3]);
            std::vector<uint8_t> payload(size);
            if (!client.value.read_exact(payload.data(), payload.size(), 3000).ok()) return;
            const uint32_t correlation = (uint32_t(payload[4]) << 24) | (uint32_t(payload[5]) << 16) |
                                         (uint32_t(payload[6]) << 8) | uint32_t(payload[7]);
            std::vector<uint8_t> body;
            synapse::codec::append_u32(body, correlation);
            if (turn == 0) {
                synapse::codec::append_u16(body, 0);
                synapse::codec::append_u32(body, 1);
                synapse::codec::append_u16(body, 0);
                synapse::codec::append_u16(body, 0);
                synapse::codec::append_u16(body, 3);
            } else {
                synapse::codec::append_u32(body, 1);
                synapse::codec::append_u16(body, 4);
                synapse::codec::append_bytes(body, reinterpret_cast<const uint8_t*>("demo"), 4);
                synapse::codec::append_u32(body, 1);
                synapse::codec::append_u32(body, 0);
                synapse::codec::append_u16(body, 0);
                synapse::codec::append_u64(body, 0);
                synapse::codec::append_u64(body, 0);
                synapse::codec::append_u32(body, 0);
            }
            std::vector<uint8_t> frame;
            synapse::codec::append_u32(frame, static_cast<uint32_t>(body.size()));
            frame.insert(frame.end(), body.begin(), body.end());
            client.value.write_all(frame);
        }
    });
    synapse::kafka::Config config;
    config.port = listener.value.port();
    synapse::kafka::Client client(config);
    CHECK(client.connect().ok());
    CHECK(client.produce("demo", "", "hello").ok());
    server.join();
}

void test_amqp() {
    auto listener = synapse::net::TcpListener::listen("127.0.0.1", 0);
    CHECK(listener.ok());
    if (!listener.ok()) return;
    std::vector<uint8_t> delivered;
    std::thread server([&] {
        auto client = listener.value.accept(3000);
        if (!client.ok()) return;
        uint8_t protocol[8];
        if (!client.value.read_exact(protocol, 8, 3000).ok()) return;
        auto send = [&](uint16_t class_id, uint16_t method_id) {
            client.value.write_all(synapse::amqp::method_frame(class_id == 10 ? 0 : 1, class_id, method_id, {}));
        };
        auto read_frame = [&](std::vector<uint8_t>& payload, uint8_t& type) -> bool {
            uint8_t header[7];
            if (!client.value.read_exact(header, 7, 3000).ok()) return false;
            type = header[0];
            const uint32_t size = (uint32_t(header[3]) << 24) | (uint32_t(header[4]) << 16) |
                                  (uint32_t(header[5]) << 8) | uint32_t(header[6]);
            payload.resize(size + 1);
            return client.value.read_exact(payload.data(), payload.size(), 3000).ok();
        };
        std::vector<uint8_t> tune;
        synapse::codec::append_u16(tune, 2047);
        synapse::codec::append_u32(tune, 131072);
        synapse::codec::append_u16(tune, 0);
        client.value.write_all(synapse::amqp::method_frame(0, 10, 10, {}));
        std::vector<uint8_t> ignore;
        uint8_t type = 0;
        if (!read_frame(ignore, type)) return;
        client.value.write_all(synapse::amqp::method_frame(0, 10, 30, tune));
        if (!read_frame(ignore, type)) return;
        if (!read_frame(ignore, type)) return;
        send(10, 41);
        if (!read_frame(ignore, type)) return;
        send(20, 11);
        if (!read_frame(ignore, type)) return;
        send(40, 11);
        if (!read_frame(ignore, type)) return;
        send(50, 11);
        if (!read_frame(ignore, type)) return;
        send(50, 21);
        if (!read_frame(ignore, type)) return;
        if (!read_frame(ignore, type)) return;
        if (!read_frame(delivered, type)) return;
    });
    synapse::amqp::Config config;
    config.port = listener.value.port();
    synapse::amqp::Client client(config);
    CHECK(client.connect().ok());
    CHECK(client.publish("act.transfer", "hello-amqp").ok());
    server.join();
    CHECK(std::string(delivered.begin(), delivered.end()).find("hello-amqp") != std::string::npos);
}

void test_cloud() {
    synapse::cloud::SigV4Request request;
    request.method = "GET";
    request.canonical_uri = "/";
    request.canonical_query = "Action=ListUsers&Version=2010-05-08";
    request.headers = {{"content-type", "application/x-www-form-urlencoded; charset=utf-8"},
                       {"host", "iam.amazonaws.com"},
                       {"x-amz-date", "20150830T123600Z"}};
    request.access_key = "AKIAIOSFODNN7EXAMPLE";
    request.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    request.region = "us-east-1";
    request.service = "iam";
    request.amz_date = "20150830T123600Z";
    const auto signed_request = synapse::cloud::sigv4_sign(request);
    CHECK(signed_request.canonical_hash == "f536975d06c0309214f805bb90ccff089219ecd68b2577efef23edd43b7e1a59");
    CHECK(signed_request.signature == "5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7");
    const std::string sas = synapse::cloud::event_grid_sas("https://topic.example/api/events", "c2VjcmV0", 1735689600);
    CHECK(sas.find("r=") == 0);
    CHECK(sas.find("&e=1735689600&s=") != std::string::npos);
    synapse::bus::EventEnvelope event;
    event.id = "cloud-1";
    event.actor.id = "agent";
    event.topic = "act.transfer";
    event.subject = "file-1";
    synapse::cloud::AwsConfig aws;
    aws.access_key = request.access_key;
    aws.secret_key = request.secret_key;
    aws.region = "eu-west-1";
    aws.amz_date = "20150830T123600Z";
    int status = 0;
    synapse::cloud::AwsEventBridge bridge(aws, [&](const synapse::net::HttpRequest& http) {
        status = http.headers.empty() ? 0 : 200;
        CHECK(http.tls);
        synapse::net::HttpResponse response;
        response.status = 200;
        return synapse::Result<synapse::net::HttpResponse>::success(response);
    });
    CHECK(bridge.put(event).ok());
    CHECK(status == 200);
    auto listener = synapse::net::TcpListener::listen("127.0.0.1", 0);
    CHECK(listener.ok());
    if (!listener.ok()) return;
    std::thread server([&] {
        auto client = listener.value.accept(3000);
        if (!client.ok()) return;
        std::string raw;
        while (raw.find("\r\n\r\n") == std::string::npos) {
            uint8_t chunk[1024];
            auto read = client.value.read_some(chunk, sizeof(chunk), 3000);
            if (!read.ok() || read.value == 0) return;
            raw.append(reinterpret_cast<char*>(chunk), read.value);
        }
        const std::string reply = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        client.value.write_all(reply);
    });
    auto http = synapse::net::parse_url("GET", "http://127.0.0.1:" + std::to_string(listener.value.port()) + "/health", "");
    CHECK(http.ok());
    auto response = synapse::net::http_exchange(http.value, 3000);
    CHECK(response.ok());
    CHECK(response.value.status == 204);
    server.join();
    if (!synapse::net::tls_available()) {
        auto secure = synapse::net::parse_url("GET", "https://example.invalid/", "");
        auto denied = synapse::net::http_exchange(secure.value, 200);
        CHECK(!denied.ok());
        CHECK(denied.error.find("OpenSSL") != std::string::npos);
    }
}

void test_pq() {
    if (!synapse::bus::pq_available()) {
        auto missing = synapse::bus::generate_identity();
        CHECK(!missing.ok());
        return;
    }
    auto alice = synapse::bus::generate_identity();
    auto bob = synapse::bus::generate_identity();
    CHECK(alice.ok());
    CHECK(bob.ok());
    if (!alice.ok() || !bob.ok()) return;
    const std::string aad = "obs.file";
    auto sealed = synapse::bus::seal(alice.value, bob.value.pub, "transfer", aad);
    CHECK(sealed.ok());
    if (!sealed.ok()) return;
    auto opened = synapse::bus::open_sealed(bob.value, alice.value.pub, sealed.value, aad);
    CHECK(opened.ok());
    CHECK(opened.value == "transfer");
    sealed.value[sealed.value.size() / 2] ^= 0x01;
    CHECK(!synapse::bus::open_sealed(bob.value, alice.value.pub, sealed.value, aad).ok());
}

}  // namespace

int main() {
    test_codecs();
    test_bus();
    test_model_and_expiry();
    test_redis();
    test_kafka();
    test_amqp();
    test_cloud();
    test_pq();
    if (g_failed) {
        std::cerr << g_failed << " checks failed\n";
        return 1;
    }
    std::cout << "synapse bus tests passed\n";
    return 0;
}
