#pragma once

#include "envelope.hpp"
#include "net.hpp"

#include <string>
#include <utility>
#include <vector>

namespace synapse::redis {

struct Resp {
    enum class Kind { Simple, Error, Integer, Bulk, Null, Array };
    Kind kind = Kind::Null;
    std::string text;
    int64_t integer = 0;
    std::vector<Resp> items;
};

Result<std::pair<Resp, size_t>> parse_resp(std::string_view bytes);

struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    std::string password;
    std::string stream = "synapse.bus";
    int timeout_ms = 3000;
};

class Client {
public:
    explicit Client(Config config);
    Status connect();
    Status xadd(const bus::EventEnvelope& event);
    Result<std::vector<bus::EventEnvelope>> xread(const std::string& from_id, int count);
    void close();

private:
    Result<Resp> command(const std::vector<std::string>& parts);
    Config config_;
    net::TcpStream stream_;
    bool connected_ = false;
};

}  // namespace synapse::redis
