#include "synapse/redis.hpp"
#include "synapse/codec.hpp"

namespace synapse::redis {
namespace {

Result<std::pair<Resp, size_t>> parse_at(std::string_view bytes, size_t index);

Result<std::pair<std::string, size_t>> line_at(std::string_view bytes, size_t index) {
    const auto end = bytes.find("\r\n", index);
    if (end == std::string_view::npos) return Result<std::pair<std::string, size_t>>::failure("need more");
    return Result<std::pair<std::string, size_t>>::success({std::string(bytes.substr(index, end - index)), end + 2});
}

Result<std::pair<Resp, size_t>> parse_at(std::string_view bytes, size_t index) {
    if (index >= bytes.size()) return Result<std::pair<Resp, size_t>>::failure("need more");
    const char kind = bytes[index];
    auto line = line_at(bytes, index + 1);
    if (!line.ok()) return Result<std::pair<Resp, size_t>>::failure(line.error);
    Resp value;
    if (kind == '+') {
        value.kind = Resp::Kind::Simple;
        value.text = line.value.first;
        return Result<std::pair<Resp, size_t>>::success({value, line.value.second});
    }
    if (kind == '-') {
        value.kind = Resp::Kind::Error;
        value.text = line.value.first;
        return Result<std::pair<Resp, size_t>>::success({value, line.value.second});
    }
    if (kind == ':') {
        value.kind = Resp::Kind::Integer;
        try {
            value.integer = std::stoll(line.value.first);
        } catch (...) {
            return Result<std::pair<Resp, size_t>>::failure("bad redis integer");
        }
        return Result<std::pair<Resp, size_t>>::success({value, line.value.second});
    }
    if (kind == '$') {
        long long length = 0;
        try {
            length = std::stoll(line.value.first);
        } catch (...) {
            return Result<std::pair<Resp, size_t>>::failure("bad redis bulk");
        }
        if (length < 0) {
            value.kind = Resp::Kind::Null;
            return Result<std::pair<Resp, size_t>>::success({value, line.value.second});
        }
        if (line.value.second + static_cast<size_t>(length) + 2 > bytes.size()) {
            return Result<std::pair<Resp, size_t>>::failure("need more");
        }
        value.kind = Resp::Kind::Bulk;
        value.text = std::string(bytes.substr(line.value.second, static_cast<size_t>(length)));
        return Result<std::pair<Resp, size_t>>::success({value, line.value.second + static_cast<size_t>(length) + 2});
    }
    if (kind == '*') {
        long long count = 0;
        try {
            count = std::stoll(line.value.first);
        } catch (...) {
            return Result<std::pair<Resp, size_t>>::failure("bad redis array");
        }
        value.kind = Resp::Kind::Array;
        size_t cursor = line.value.second;
        if (count < 0) {
            value.kind = Resp::Kind::Null;
            return Result<std::pair<Resp, size_t>>::success({value, cursor});
        }
        for (long long i = 0; i < count; ++i) {
            auto item = parse_at(bytes, cursor);
            if (!item.ok()) return item;
            value.items.push_back(std::move(item.value.first));
            cursor = item.value.second;
        }
        return Result<std::pair<Resp, size_t>>::success({value, cursor});
    }
    return Result<std::pair<Resp, size_t>>::failure("bad redis type");
}

}  // namespace

Result<std::pair<Resp, size_t>> parse_resp(std::string_view bytes) { return parse_at(bytes, 0); }

Client::Client(Config config) : config_(std::move(config)) {}

void Client::close() {
    stream_.close();
    connected_ = false;
}

Status Client::connect() {
    auto stream = net::TcpStream::connect(config_.host, config_.port, config_.timeout_ms);
    if (!stream.ok()) return Status::failure(stream.error);
    stream_ = std::move(stream.value);
    connected_ = true;
    if (!config_.password.empty()) {
        auto reply = command({"AUTH", config_.password});
        if (!reply.ok()) return Status::failure(reply.error);
        if (reply.value.kind == Resp::Kind::Error) return Status::failure(reply.value.text);
    }
    auto ping = command({"PING"});
    if (!ping.ok()) return Status::failure(ping.error);
    if (ping.value.kind == Resp::Kind::Error) return Status::failure(ping.value.text);
    return Status::success();
}

Result<Resp> Client::command(const std::vector<std::string>& parts) {
    if (!connected_) return Result<Resp>::failure("redis is not connected");
    std::string request = "*" + std::to_string(parts.size()) + "\r\n";
    for (const auto& part : parts) request += "$" + std::to_string(part.size()) + "\r\n" + part + "\r\n";
    if (auto status = stream_.write_all(request); !status.ok()) return Result<Resp>::failure(status.error);
    std::string buffer;
    while (buffer.size() < 16 * 1024 * 1024) {
        uint8_t chunk[4096];
        auto read = stream_.read_some(chunk, sizeof(chunk), config_.timeout_ms);
        if (!read.ok()) return Result<Resp>::failure(read.error);
        if (read.value == 0) return Result<Resp>::failure("redis closed");
        buffer.append(reinterpret_cast<char*>(chunk), read.value);
        auto parsed = parse_resp(buffer);
        if (parsed.ok()) return Result<Resp>::success(std::move(parsed.value.first));
        if (parsed.error != "need more") return Result<Resp>::failure(parsed.error);
    }
    return Result<Resp>::failure("redis response too large");
}

Status Client::xadd(const bus::EventEnvelope& event) {
    auto reply = command({"XADD", config_.stream, "*", "envelope", bus::canonical_envelope(event)});
    if (!reply.ok()) return Status::failure(reply.error);
    if (reply.value.kind == Resp::Kind::Error) return Status::failure(reply.value.text);
    if (reply.value.kind != Resp::Kind::Bulk) return Status::failure("redis xadd rejected");
    return Status::success();
}

Result<std::vector<bus::EventEnvelope>> Client::xread(const std::string& from_id, int count) {
    auto reply = command({"XREAD", "COUNT", std::to_string(count), "STREAMS", config_.stream, from_id});
    if (!reply.ok()) return Result<std::vector<bus::EventEnvelope>>::failure(reply.error);
    if (reply.value.kind == Resp::Kind::Null) return Result<std::vector<bus::EventEnvelope>>::success({});
    if (reply.value.kind == Resp::Kind::Error) {
        return Result<std::vector<bus::EventEnvelope>>::failure(reply.value.text);
    }
    std::vector<bus::EventEnvelope> events;
    if (reply.value.items.empty()) return Result<std::vector<bus::EventEnvelope>>::success(events);
    const Resp& stream = reply.value.items[0];
    if (stream.items.size() < 2) return Result<std::vector<bus::EventEnvelope>>::failure("redis xread shape");
    for (const auto& entry : stream.items[1].items) {
        if (entry.items.size() < 2 || entry.items[1].items.size() < 2) {
            return Result<std::vector<bus::EventEnvelope>>::failure("redis entry shape");
        }
        const std::string& body = entry.items[1].items[1].text;
        auto event = bus::parse_envelope(body);
        if (!event.ok()) return Result<std::vector<bus::EventEnvelope>>::failure(event.error);
        events.push_back(std::move(event.value));
    }
    return Result<std::vector<bus::EventEnvelope>>::success(std::move(events));
}

}  // namespace synapse::redis
