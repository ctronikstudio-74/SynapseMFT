#include "synapse/codec.hpp"

#include <cctype>

namespace synapse::codec {
namespace {

uint32_t rotr(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

const uint32_t kSha256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256_block(uint32_t state[8], const uint8_t block[64]) {
    uint32_t words[64];
    for (int i = 0; i < 16; ++i) {
        words[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + kSha256[i] + words[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    Result<Json> parse() {
        skip();
        auto value = parse_value();
        if (!value.ok()) return value;
        skip();
        if (index_ != text_.size()) return Result<Json>::failure("trailing json");
        return value;
    }

private:
    std::string_view text_;
    size_t index_ = 0;

    void skip() {
        while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_]))) ++index_;
    }

    bool take(char expected) {
        skip();
        if (index_ < text_.size() && text_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    Result<Json> parse_value() {
        skip();
        if (index_ >= text_.size()) return Result<Json>::failure("unexpected end of json");
        const char c = text_[index_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') {
            auto text = parse_string();
            if (!text.ok()) return Result<Json>::failure(text.error);
            Json json;
            json.kind = Json::Kind::String;
            json.text = std::move(text.value);
            return Result<Json>::success(std::move(json));
        }
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        return Result<Json>::failure("invalid json value");
    }

    Result<Json> parse_object() {
        if (!take('{')) return Result<Json>::failure("expected object");
        Json json;
        json.kind = Json::Kind::Object;
        skip();
        if (take('}')) return Result<Json>::success(std::move(json));
        for (;;) {
            auto key = parse_string();
            if (!key.ok()) return Result<Json>::failure(key.error);
            if (!take(':')) return Result<Json>::failure("expected colon");
            auto value = parse_value();
            if (!value.ok()) return value;
            json.object.emplace(std::move(key.value), std::move(value.value));
            skip();
            if (take('}')) return Result<Json>::success(std::move(json));
            if (!take(',')) return Result<Json>::failure("expected comma");
        }
    }

    Result<Json> parse_array() {
        if (!take('[')) return Result<Json>::failure("expected array");
        Json json;
        json.kind = Json::Kind::Array;
        skip();
        if (take(']')) return Result<Json>::success(std::move(json));
        for (;;) {
            auto value = parse_value();
            if (!value.ok()) return value;
            json.array.push_back(std::move(value.value));
            skip();
            if (take(']')) return Result<Json>::success(std::move(json));
            if (!take(',')) return Result<Json>::failure("expected comma");
        }
    }

    Result<std::string> parse_string() {
        skip();
        if (index_ >= text_.size() || text_[index_] != '"') return Result<std::string>::failure("expected string");
        ++index_;
        std::string out;
        while (index_ < text_.size()) {
            const char c = text_[index_++];
            if (c == '"') return Result<std::string>::success(std::move(out));
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (index_ >= text_.size()) return Result<std::string>::failure("bad escape");
            const char esc = text_[index_++];
            switch (esc) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(esc);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    if (index_ + 4 > text_.size()) return Result<std::string>::failure("bad unicode escape");
                    int code = 0;
                    for (int n = 0; n < 4; ++n) {
                        const int digit = hex_value(text_[index_++]);
                        if (digit < 0) return Result<std::string>::failure("bad unicode escape");
                        code = (code << 4) | digit;
                    }
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default:
                    return Result<std::string>::failure("bad escape");
            }
        }
        return Result<std::string>::failure("unterminated string");
    }

    Result<Json> parse_bool() {
        Json json;
        json.kind = Json::Kind::Bool;
        if (text_.substr(index_, 4) == "true") {
            index_ += 4;
            json.boolean = true;
            return Result<Json>::success(std::move(json));
        }
        if (text_.substr(index_, 5) == "false") {
            index_ += 5;
            json.boolean = false;
            return Result<Json>::success(std::move(json));
        }
        return Result<Json>::failure("invalid bool");
    }

    Result<Json> parse_null() {
        if (text_.substr(index_, 4) != "null") return Result<Json>::failure("invalid null");
        index_ += 4;
        Json json;
        json.kind = Json::Kind::Null;
        return Result<Json>::success(std::move(json));
    }

    Result<Json> parse_number() {
        const size_t start = index_;
        if (text_[index_] == '-') ++index_;
        if (index_ >= text_.size() || text_[index_] < '0' || text_[index_] > '9') {
            return Result<Json>::failure("invalid number");
        }
        while (index_ < text_.size() && text_[index_] >= '0' && text_[index_] <= '9') ++index_;
        if (index_ < text_.size() && (text_[index_] == '.' || text_[index_] == 'e' || text_[index_] == 'E')) {
            return Result<Json>::failure("only integers are accepted");
        }
        try {
            Json json;
            json.kind = Json::Kind::Number;
            json.number = std::stoll(std::string(text_.substr(start, index_ - start)));
            return Result<Json>::success(std::move(json));
        } catch (...) {
            return Result<Json>::failure("number out of range");
        }
    }
};

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size) {
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block[64];
    size_t offset = 0;
    while (offset + 64 <= size) {
        sha256_block(state, data + offset);
        offset += 64;
    }
    size_t tail = size - offset;
    for (size_t i = 0; i < tail; ++i) block[i] = data[offset + i];
    block[tail++] = 0x80;
    if (tail > 56) {
        while (tail < 64) block[tail++] = 0;
        sha256_block(state, block);
        tail = 0;
    }
    while (tail < 56) block[tail++] = 0;
    const uint64_t bits = uint64_t(size) * 8;
    for (int i = 7; i >= 0; --i) block[tail++] = static_cast<uint8_t>((bits >> (i * 8)) & 0xff);
    sha256_block(state, block);

    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>(state[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
    return out;
}

std::array<uint8_t, 32> sha256(std::string_view data) {
    return sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::array<uint8_t, 32> hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len) {
    std::array<uint8_t, 64> key_block{};
    if (key_len > 64) {
        const auto hashed = sha256(key, key_len);
        for (size_t i = 0; i < hashed.size(); ++i) key_block[i] = hashed[i];
    } else {
        for (size_t i = 0; i < key_len; ++i) key_block[i] = key[i];
    }
    std::vector<uint8_t> inner(64 + data_len);
    std::vector<uint8_t> outer(64 + 32);
    for (size_t i = 0; i < 64; ++i) {
        inner[i] = static_cast<uint8_t>(key_block[i] ^ 0x36);
        outer[i] = static_cast<uint8_t>(key_block[i] ^ 0x5c);
    }
    for (size_t i = 0; i < data_len; ++i) inner[64 + i] = data[i];
    const auto inner_hash = sha256(inner.data(), inner.size());
    for (size_t i = 0; i < inner_hash.size(); ++i) outer[64 + i] = inner_hash[i];
    return sha256(outer.data(), outer.size());
}

std::array<uint8_t, 32> hmac_sha256(std::string_view key, std::string_view data) {
    return hmac_sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                       reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string hex_encode(const uint8_t* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out(size * 2, '0');
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    return out;
}

std::string b64_encode(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kB64[(triple >> 18) & 63]);
        out.push_back(kB64[(triple >> 12) & 63]);
        out.push_back(i + 1 < size ? kB64[(triple >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? kB64[triple & 63] : '=');
    }
    return out;
}

Result<std::vector<uint8_t>> b64_decode(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    uint32_t accumulator = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || std::isspace(static_cast<unsigned char>(c))) continue;
        const int digit = value(c);
        if (digit < 0) return Result<std::vector<uint8_t>>::failure("invalid base64");
        accumulator = (accumulator << 6) | uint32_t(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    return Result<std::vector<uint8_t>>::success(std::move(out));
}

uint32_t crc32_iso(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t low = crc & 1u;
            crc >>= 1;
            if (low) crc ^= 0xedb88320u;
        }
    }
    return ~crc;
}

uint32_t crc32c(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t low = crc & 1u;
            crc >>= 1;
            if (low) crc ^= 0x82f63b78u;
        }
    }
    return ~crc;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    static const char* digits = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(digits[c >> 4]);
                    out.push_back(digits[c & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string url_encode(std::string_view text) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 0x0f]);
        }
    }
    return out;
}

const Json* Json::find(std::string_view key) const {
    if (kind != Kind::Object) return nullptr;
    const auto it = object.find(std::string(key));
    if (it == object.end()) return nullptr;
    return &it->second;
}

Result<std::string> Json::str(std::string_view key) const {
    const Json* field = find(key);
    if (!field || field->kind != Kind::String) return Result<std::string>::failure("missing string " + std::string(key));
    return Result<std::string>::success(field->text);
}

Result<int64_t> Json::i64(std::string_view key) const {
    const Json* field = find(key);
    if (!field || field->kind != Kind::Number) return Result<int64_t>::failure("missing number " + std::string(key));
    return Result<int64_t>::success(field->number);
}

Result<bool> Json::boolean_at(std::string_view key) const {
    const Json* field = find(key);
    if (!field || field->kind != Kind::Bool) return Result<bool>::failure("missing bool " + std::string(key));
    return Result<bool>::success(field->boolean);
}

Result<Json> parse_json(std::string_view text) {
    return JsonParser(text).parse();
}

void append_u8(std::vector<uint8_t>& out, uint8_t value) { out.push_back(value); }

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void append_u64(std::vector<uint8_t>& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

void append_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
    out.insert(out.end(), data, data + size);
}

Status Reader::need(size_t count) const {
    if (size_ - index_ < count) return Status::failure("short buffer");
    return Status::success();
}

Result<uint8_t> Reader::u8() {
    if (auto status = need(1); !status.ok()) return Result<uint8_t>::failure(status.error);
    return Result<uint8_t>::success(data_[index_++]);
}

Result<uint16_t> Reader::u16() {
    if (auto status = need(2); !status.ok()) return Result<uint16_t>::failure(status.error);
    const uint16_t value = (uint16_t(data_[index_]) << 8) | data_[index_ + 1];
    index_ += 2;
    return Result<uint16_t>::success(value);
}

Result<uint32_t> Reader::u32() {
    if (auto status = need(4); !status.ok()) return Result<uint32_t>::failure(status.error);
    const uint32_t value = (uint32_t(data_[index_]) << 24) | (uint32_t(data_[index_ + 1]) << 16) |
                           (uint32_t(data_[index_ + 2]) << 8) | uint32_t(data_[index_ + 3]);
    index_ += 4;
    return Result<uint32_t>::success(value);
}

Result<uint64_t> Reader::u64() {
    if (auto status = need(8); !status.ok()) return Result<uint64_t>::failure(status.error);
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | data_[index_++];
    return Result<uint64_t>::success(value);
}

Result<int16_t> Reader::i16() {
    auto value = u16();
    if (!value.ok()) return Result<int16_t>::failure(value.error);
    return Result<int16_t>::success(static_cast<int16_t>(value.value));
}

Result<int32_t> Reader::i32() {
    auto value = u32();
    if (!value.ok()) return Result<int32_t>::failure(value.error);
    return Result<int32_t>::success(static_cast<int32_t>(value.value));
}

Result<int64_t> Reader::i64() {
    auto value = u64();
    if (!value.ok()) return Result<int64_t>::failure(value.error);
    return Result<int64_t>::success(static_cast<int64_t>(value.value));
}

Result<std::vector<uint8_t>> Reader::take(size_t count) {
    if (auto status = need(count); !status.ok()) return Result<std::vector<uint8_t>>::failure(status.error);
    std::vector<uint8_t> out(data_ + index_, data_ + index_ + count);
    index_ += count;
    return Result<std::vector<uint8_t>>::success(std::move(out));
}

}  // namespace synapse::codec
