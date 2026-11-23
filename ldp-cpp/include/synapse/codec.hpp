#pragma once

#include "result.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace synapse::codec {

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size);
std::array<uint8_t, 32> sha256(std::string_view data);
std::array<uint8_t, 32> hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len);
std::array<uint8_t, 32> hmac_sha256(std::string_view key, std::string_view data);

std::string hex_encode(const uint8_t* data, size_t size);
std::string b64_encode(const uint8_t* data, size_t size);
Result<std::vector<uint8_t>> b64_decode(std::string_view text);

uint32_t crc32_iso(const uint8_t* data, size_t size);
uint32_t crc32c(const uint8_t* data, size_t size);

std::string json_escape(std::string_view text);
std::string url_encode(std::string_view text);

struct Json {
    enum class Kind { Null, Bool, Number, String, Object, Array };
    Kind kind = Kind::Null;
    bool boolean = false;
    int64_t number = 0;
    std::string text;
    std::map<std::string, Json> object;
    std::vector<Json> array;

    const Json* find(std::string_view key) const;
    Result<std::string> str(std::string_view key) const;
    Result<int64_t> i64(std::string_view key) const;
    Result<bool> boolean_at(std::string_view key) const;
};

Result<Json> parse_json(std::string_view text);

void append_u8(std::vector<uint8_t>& out, uint8_t value);
void append_u16(std::vector<uint8_t>& out, uint16_t value);
void append_u32(std::vector<uint8_t>& out, uint32_t value);
void append_u64(std::vector<uint8_t>& out, uint64_t value);
void append_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t size);

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit Reader(const std::vector<uint8_t>& bytes) : Reader(bytes.data(), bytes.size()) {}
    explicit Reader(std::string_view bytes)
        : Reader(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) {}

    size_t remaining() const { return size_ - index_; }
    size_t offset() const { return index_; }
    Status need(size_t count) const;

    Result<uint8_t> u8();
    Result<uint16_t> u16();
    Result<uint32_t> u32();
    Result<uint64_t> u64();
    Result<int16_t> i16();
    Result<int32_t> i32();
    Result<int64_t> i64();
    Result<std::vector<uint8_t>> take(size_t count);

private:
    const uint8_t* data_;
    size_t size_;
    size_t index_ = 0;
};

}  // namespace synapse::codec
