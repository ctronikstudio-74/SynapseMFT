#include "LDPStructures.h"
#include "LDPCongestionControl.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, line, expression);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void test_packet_roundtrip() {
    LDPPacket packet{};
    packet.header.sequence_id = 42;
    packet.header.payload_size = 4;
    packet.header.type = static_cast<uint8_t>(LDPCommandType::DATA);
    packet.encrypted_payload = {1, 2, 3, 4};
    std::memset(packet.mac, 0xAB, sizeof(packet.mac));

    const std::vector<uint8_t> bytes = LDP::serialize_packet(packet);
    CHECK(bytes.size() == LDP::packet_size(packet));
    CHECK(bytes.size() == sizeof(LDPHeader) + 4 + 16);

    LDPPacket decoded{};
    CHECK(LDP::deserialize_packet(bytes, decoded));
    CHECK(decoded.header.sequence_id == 42);
    CHECK(decoded.header.type == static_cast<uint8_t>(LDPCommandType::DATA));
    CHECK(decoded.encrypted_payload.size() == 4);
    CHECK(decoded.encrypted_payload[0] == 1);
    CHECK(decoded.encrypted_payload[3] == 4);
    CHECK(std::memcmp(decoded.mac, packet.mac, sizeof(packet.mac)) == 0);
    CHECK(LDP::validate_packet(decoded));
}

void test_packet_rejects_truncated_buffer() {
    const std::vector<uint8_t> truncated(sizeof(LDPHeader), 0);
    LDPPacket packet{};
    CHECK(!LDP::deserialize_packet(truncated, packet));
}

void test_validate_rejects_empty_payload() {
    LDPPacket packet{};
    std::memset(packet.mac, 1, sizeof(packet.mac));
    CHECK(!LDP::validate_packet(packet));
}

void test_congestion_bounds() {
    LDPCongestionControl slow;
    CHECK(slow.get_interval() == 1.0);
    for (int i = 0; i < 200; ++i) {
        slow.adjust_rate(500);
    }
    CHECK(slow.get_interval() > 1.0);
    CHECK(slow.get_interval() <= 100.0);

    LDPCongestionControl fast;
    for (int i = 0; i < 200; ++i) {
        fast.adjust_rate(10);
    }
    CHECK(fast.get_interval() < 1.0);
    CHECK(fast.get_interval() >= 0.1);
}

}  // namespace

int main() {
    test_packet_roundtrip();
    test_packet_rejects_truncated_buffer();
    test_validate_rejects_empty_payload();
    test_congestion_bounds();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d protocol checks failed\n", g_failures);
        return 1;
    }

    std::puts("ldp protocol unit tests passed");
    return 0;
}
