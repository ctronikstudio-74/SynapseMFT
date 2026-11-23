#pragma once

#include "net.hpp"
#include "net.hpp"
#include "result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace synapse::kafka {

std::vector<uint8_t> encode_record_batch(std::string_view key, std::string_view value, int64_t timestamp_ms);
uint32_t record_batch_crc(const std::vector<uint8_t>& batch);

struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 9092;
    std::string client_id = "synapse-bus";
    int timeout_ms = 3000;
    int32_t partition = 0;
};

class Client {
public:
    explicit Client(Config config);
    Status connect();
    Status produce(const std::string& topic, const std::string& key, const std::string& value);
    void close();

private:
    Result<std::vector<uint8_t>> request(const std::vector<uint8_t>& frame);
    Config config_;
    net::TcpStream stream_;
    bool connected_ = false;
    int32_t correlation_ = 1;
};

}  // namespace synapse::kafka
