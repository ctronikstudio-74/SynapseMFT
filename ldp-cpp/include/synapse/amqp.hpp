#pragma once

#include "net.hpp"
#include "result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace synapse::amqp {

std::vector<uint8_t> method_frame(uint16_t channel, uint16_t class_id, uint16_t method_id,
                                  const std::vector<uint8_t>& args);

struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 5672;
    std::string vhost = "/";
    std::string user = "guest";
    std::string password = "guest";
    std::string exchange = "synapse.bus";
    std::string queue = "synapse.bus";
    int timeout_ms = 3000;
};

class Client {
public:
    explicit Client(Config config);
    Status connect();
    Status publish(const std::string& routing_key, const std::string& body);
    void close();

private:
    struct Method {
        uint16_t channel = 0;
        uint16_t class_id = 0;
        uint16_t method_id = 0;
        std::vector<uint8_t> args;
    };
    Result<Method> read_method();
    Status write_frame(const std::vector<uint8_t>& frame);
    Config config_;
    net::TcpStream stream_;
    bool connected_ = false;
};

}  // namespace synapse::amqp
