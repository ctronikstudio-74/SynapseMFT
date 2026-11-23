#include "synapse/kafka.hpp"
#include "synapse/codec.hpp"

#include <cstring>

namespace synapse::kafka {
namespace {

void append_varint(std::vector<uint8_t>& out, uint32_t value) {
    while (value >= 0x80u) {
        out.push_back(static_cast<uint8_t>(value | 0x80u));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

uint32_t zigzag32(int32_t value) {
    return (static_cast<uint32_t>(value) << 1) ^ static_cast<uint32_t>(value >> 31);
}

std::vector<uint8_t> encode_record(std::string_view key, std::string_view value) {
    std::vector<uint8_t> body;
    body.push_back(0);
    append_varint(body, zigzag32(0));
    append_varint(body, zigzag32(0));
    if (key.empty()) {
        append_varint(body, zigzag32(-1));
    } else {
        append_varint(body, zigzag32(static_cast<int32_t>(key.size())));
        body.insert(body.end(), key.begin(), key.end());
    }
    append_varint(body, zigzag32(static_cast<int32_t>(value.size())));
    body.insert(body.end(), value.begin(), value.end());
    append_varint(body, zigzag32(0));
    std::vector<uint8_t> record;
    append_varint(record, zigzag32(static_cast<int32_t>(body.size())));
    record.insert(record.end(), body.begin(), body.end());
    return record;
}

}  // namespace

std::vector<uint8_t> encode_record_batch(std::string_view key, std::string_view value, int64_t timestamp_ms) {
    const auto record = encode_record(key, value);
    std::vector<uint8_t> tail;
    codec::append_u16(tail, 0);
    codec::append_u32(tail, 0);
    codec::append_u64(tail, static_cast<uint64_t>(timestamp_ms));
    codec::append_u64(tail, static_cast<uint64_t>(timestamp_ms));
    codec::append_u64(tail, ~uint64_t{0});
    codec::append_u16(tail, 0xffff);
    codec::append_u32(tail, 0xffffffffu);
    codec::append_u32(tail, 1);
    codec::append_bytes(tail, record.data(), record.size());
    const uint32_t crc = codec::crc32c(tail.data(), tail.size());

    std::vector<uint8_t> batch;
    codec::append_u64(batch, 0);
    codec::append_u32(batch, 0);
    codec::append_u32(batch, 0xffffffffu);
    codec::append_u8(batch, 2);
    codec::append_u32(batch, crc);
    codec::append_bytes(batch, tail.data(), tail.size());
    const uint32_t batch_length = static_cast<uint32_t>(batch.size() - 12);
    batch[8] = static_cast<uint8_t>(batch_length >> 24);
    batch[9] = static_cast<uint8_t>(batch_length >> 16);
    batch[10] = static_cast<uint8_t>(batch_length >> 8);
    batch[11] = static_cast<uint8_t>(batch_length);
    return batch;
}

uint32_t record_batch_crc(const std::vector<uint8_t>& batch) {
    if (batch.size() < 21) return 0;
    return codec::crc32c(batch.data() + 21, batch.size() - 21);
}

Client::Client(Config config) : config_(std::move(config)) {}

void Client::close() {
    stream_.close();
    connected_ = false;
}

Result<std::vector<uint8_t>> Client::request(const std::vector<uint8_t>& frame) {
    if (auto status = stream_.write_all(frame); !status.ok()) return Result<std::vector<uint8_t>>::failure(status.error);
    uint8_t size_bytes[4];
    if (auto status = stream_.read_exact(size_bytes, 4, config_.timeout_ms); !status.ok()) {
        return Result<std::vector<uint8_t>>::failure(status.error);
    }
    const uint32_t size = (uint32_t(size_bytes[0]) << 24) | (uint32_t(size_bytes[1]) << 16) |
                          (uint32_t(size_bytes[2]) << 8) | uint32_t(size_bytes[3]);
    if (size < 4 || size > 8 * 1024 * 1024) return Result<std::vector<uint8_t>>::failure("kafka frame rejected");
    std::vector<uint8_t> payload(size);
    if (auto status = stream_.read_exact(payload.data(), payload.size(), config_.timeout_ms); !status.ok()) {
        return Result<std::vector<uint8_t>>::failure(status.error);
    }
    return Result<std::vector<uint8_t>>::success(std::move(payload));
}

Status Client::connect() {
    auto stream = net::TcpStream::connect(config_.host, config_.port, config_.timeout_ms);
    if (!stream.ok()) return Status::failure(stream.error);
    stream_ = std::move(stream.value);
    std::vector<uint8_t> body;
    codec::append_u16(body, 18);
    codec::append_u16(body, 0);
    codec::append_u32(body, static_cast<uint32_t>(correlation_++));
    codec::append_u16(body, static_cast<uint16_t>(config_.client_id.size()));
    codec::append_bytes(body, reinterpret_cast<const uint8_t*>(config_.client_id.data()), config_.client_id.size());
    std::vector<uint8_t> frame;
    codec::append_u32(frame, static_cast<uint32_t>(body.size()));
    codec::append_bytes(frame, body.data(), body.size());
    auto response = request(frame);
    if (!response.ok()) return Status::failure(response.error);
    codec::Reader reader(response.value);
    auto correlation = reader.i32();
    auto error = reader.i16();
    auto count = reader.i32();
    if (!correlation.ok() || !error.ok() || !count.ok()) return Status::failure("kafka apiversions rejected");
    if (error.value != 0) return Status::failure("kafka apiversions error");
    bool produce_v3 = false;
    for (int32_t i = 0; i < count.value; ++i) {
        auto key = reader.i16();
        auto min_version = reader.i16();
        auto max_version = reader.i16();
        if (!key.ok() || !min_version.ok() || !max_version.ok()) return Status::failure("kafka apiversions rejected");
        if (key.value == 0 && min_version.value <= 3 && max_version.value >= 3) produce_v3 = true;
    }
    if (!produce_v3) return Status::failure("kafka broker has no produce v3");
    connected_ = true;
    return Status::success();
}

Status Client::produce(const std::string& topic, const std::string& key, const std::string& value) {
    if (!connected_) return Status::failure("kafka is not connected");
    if (topic.size() > 32767) return Status::failure("kafka topic rejected");
    const auto batch = encode_record_batch(key, value, 0);
    const int32_t correlation = correlation_++;
    std::vector<uint8_t> body;
    codec::append_u16(body, 0);
    codec::append_u16(body, 3);
    codec::append_u32(body, static_cast<uint32_t>(correlation));
    codec::append_u16(body, static_cast<uint16_t>(config_.client_id.size()));
    codec::append_bytes(body, reinterpret_cast<const uint8_t*>(config_.client_id.data()), config_.client_id.size());
    codec::append_u16(body, 0xffff);
    codec::append_u16(body, 1);
    codec::append_u32(body, 5000);
    codec::append_u32(body, 1);
    codec::append_u16(body, static_cast<uint16_t>(topic.size()));
    codec::append_bytes(body, reinterpret_cast<const uint8_t*>(topic.data()), topic.size());
    codec::append_u32(body, 1);
    codec::append_u32(body, static_cast<uint32_t>(config_.partition));
    codec::append_u32(body, static_cast<uint32_t>(batch.size()));
    codec::append_bytes(body, batch.data(), batch.size());
    std::vector<uint8_t> frame;
    codec::append_u32(frame, static_cast<uint32_t>(body.size()));
    codec::append_bytes(frame, body.data(), body.size());
    auto response = request(frame);
    if (!response.ok()) return Status::failure(response.error);
    codec::Reader reader(response.value);
    auto echoed = reader.i32();
    auto topic_count = reader.i32();
    if (!echoed.ok() || echoed.value != correlation || !topic_count.ok() || topic_count.value != 1) {
        return Status::failure("kafka produce rejected");
    }
    auto name_len = reader.i16();
    if (!name_len.ok() || name_len.value < 0) return Status::failure("kafka produce rejected");
    auto name = reader.take(static_cast<size_t>(name_len.value));
    auto partitions = reader.i32();
    auto partition = reader.i32();
    auto error = reader.i16();
    if (!name.ok() || !partitions.ok() || !partition.ok() || !error.ok()) return Status::failure("kafka produce rejected");
    if (error.value != 0) return Status::failure("kafka produce error " + std::to_string(error.value));
    return Status::success();
}

}  // namespace synapse::kafka
