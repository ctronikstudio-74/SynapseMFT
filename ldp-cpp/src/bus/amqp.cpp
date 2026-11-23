#include "synapse/amqp.hpp"
#include "synapse/codec.hpp"

namespace synapse::amqp {
namespace {

void append_shortstr(std::vector<uint8_t>& out, const std::string& value) {
    codec::append_u8(out, static_cast<uint8_t>(value.size()));
    codec::append_bytes(out, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void append_longstr(std::vector<uint8_t>& out, const std::string& value) {
    codec::append_u32(out, static_cast<uint32_t>(value.size()));
    codec::append_bytes(out, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

std::vector<uint8_t> frame(uint8_t type, uint16_t channel, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    codec::append_u8(out, type);
    codec::append_u16(out, channel);
    codec::append_u32(out, static_cast<uint32_t>(payload.size()));
    codec::append_bytes(out, payload.data(), payload.size());
    codec::append_u8(out, 0xce);
    return out;
}

}  // namespace

std::vector<uint8_t> method_frame(uint16_t channel, uint16_t class_id, uint16_t method_id,
                                  const std::vector<uint8_t>& args) {
    std::vector<uint8_t> payload;
    codec::append_u16(payload, class_id);
    codec::append_u16(payload, method_id);
    codec::append_bytes(payload, args.data(), args.size());
    return frame(1, channel, payload);
}

Client::Client(Config config) : config_(std::move(config)) {}

void Client::close() {
    stream_.close();
    connected_ = false;
}

Status Client::write_frame(const std::vector<uint8_t>& bytes) { return stream_.write_all(bytes); }

Result<Client::Method> Client::read_method() {
    for (;;) {
        uint8_t header[7];
        if (auto status = stream_.read_exact(header, 7, config_.timeout_ms); !status.ok()) {
            return Result<Method>::failure(status.error);
        }
        const uint8_t type = header[0];
        const uint16_t channel = (uint16_t(header[1]) << 8) | header[2];
        const uint32_t size = (uint32_t(header[3]) << 24) | (uint32_t(header[4]) << 16) |
                              (uint32_t(header[5]) << 8) | uint32_t(header[6]);
        if (size > 1024 * 1024) return Result<Method>::failure("amqp frame rejected");
        std::vector<uint8_t> payload(size + 1);
        if (auto status = stream_.read_exact(payload.data(), payload.size(), config_.timeout_ms); !status.ok()) {
            return Result<Method>::failure(status.error);
        }
        if (payload.back() != 0xce) return Result<Method>::failure("amqp frame end rejected");
        if (type == 8) continue;
        if (type != 1 || size < 4) return Result<Method>::failure("amqp method rejected");
        Method method;
        method.channel = channel;
        method.class_id = (uint16_t(payload[0]) << 8) | payload[1];
        method.method_id = (uint16_t(payload[2]) << 8) | payload[3];
        method.args.assign(payload.begin() + 4, payload.end() - 1);
        return Result<Method>::success(std::move(method));
    }
}

Status Client::connect() {
    auto stream = net::TcpStream::connect(config_.host, config_.port, config_.timeout_ms);
    if (!stream.ok()) return Status::failure(stream.error);
    stream_ = std::move(stream.value);
    const uint8_t protocol[] = {'A', 'M', 'Q', 'P', 0, 0, 9, 1};
    if (auto status = stream_.write_all(protocol, sizeof(protocol)); !status.ok()) return status;
    auto start = read_method();
    if (!start.ok()) return Status::failure(start.error);
    if (start.value.class_id != 10 || start.value.method_id != 10) return Status::failure("amqp start rejected");

    std::vector<uint8_t> start_ok;
    codec::append_u32(start_ok, 0);
    append_shortstr(start_ok, "PLAIN");
    const std::string response = std::string("\0", 1) + config_.user + std::string("\0", 1) + config_.password;
    append_longstr(start_ok, response);
    append_shortstr(start_ok, "en_US");
    if (auto status = write_frame(method_frame(0, 10, 11, start_ok)); !status.ok()) return status;

    auto tune = read_method();
    if (!tune.ok()) return Status::failure(tune.error);
    if (tune.value.class_id != 10 || tune.value.method_id != 30 || tune.value.args.size() < 8) {
        return Status::failure("amqp tune rejected");
    }
    tune.value.args[6] = 0;
    tune.value.args[7] = 0;
    if (auto status = write_frame(method_frame(0, 10, 31, tune.value.args)); !status.ok()) return status;

    std::vector<uint8_t> open_args;
    append_shortstr(open_args, config_.vhost);
    append_shortstr(open_args, "");
    codec::append_u8(open_args, 0);
    if (auto status = write_frame(method_frame(0, 10, 40, open_args)); !status.ok()) return status;
    auto opened = read_method();
    if (!opened.ok()) return Status::failure(opened.error);
    if (opened.value.class_id != 10 || opened.value.method_id != 41) return Status::failure("amqp open rejected");

    std::vector<uint8_t> channel_args;
    append_shortstr(channel_args, "");
    if (auto status = write_frame(method_frame(1, 20, 10, channel_args)); !status.ok()) return status;
    auto channel = read_method();
    if (!channel.ok()) return Status::failure(channel.error);
    if (channel.value.class_id != 20 || channel.value.method_id != 11) return Status::failure("amqp channel rejected");

    auto declare = [&](uint16_t class_id, uint16_t method_id, const std::vector<uint8_t>& args, uint16_t ok_method) -> Status {
        if (auto status = write_frame(method_frame(1, class_id, method_id, args)); !status.ok()) return status;
        auto reply = read_method();
        if (!reply.ok()) return Status::failure(reply.error);
        if (reply.value.class_id != class_id || reply.value.method_id != ok_method) {
            return Status::failure("amqp declare rejected");
        }
        return Status::success();
    };

    std::vector<uint8_t> exchange_args;
    codec::append_u16(exchange_args, 0);
    append_shortstr(exchange_args, config_.exchange);
    append_shortstr(exchange_args, "topic");
    codec::append_u8(exchange_args, 0);
    codec::append_u32(exchange_args, 0);
    if (auto status = declare(40, 10, exchange_args, 11); !status.ok()) return status;

    std::vector<uint8_t> queue_args;
    codec::append_u16(queue_args, 0);
    append_shortstr(queue_args, config_.queue);
    codec::append_u8(queue_args, 0);
    codec::append_u32(queue_args, 0);
    if (auto status = declare(50, 10, queue_args, 11); !status.ok()) return status;

    std::vector<uint8_t> bind_args;
    codec::append_u16(bind_args, 0);
    append_shortstr(bind_args, config_.queue);
    append_shortstr(bind_args, config_.exchange);
    append_shortstr(bind_args, "#");
    codec::append_u8(bind_args, 0);
    codec::append_u32(bind_args, 0);
    if (auto status = declare(50, 20, bind_args, 21); !status.ok()) return status;
    connected_ = true;
    return Status::success();
}

Status Client::publish(const std::string& routing_key, const std::string& body) {
    if (!connected_) return Status::failure("amqp is not connected");
    std::vector<uint8_t> args;
    codec::append_u16(args, 0);
    append_shortstr(args, config_.exchange);
    append_shortstr(args, routing_key);
    codec::append_u8(args, 0);
    if (auto status = write_frame(method_frame(1, 60, 40, args)); !status.ok()) return status;
    std::vector<uint8_t> header;
    codec::append_u16(header, 60);
    codec::append_u16(header, 0);
    codec::append_u64(header, static_cast<uint64_t>(body.size()));
    codec::append_u16(header, 0);
    if (auto status = write_frame(frame(2, 1, header)); !status.ok()) return status;
    std::vector<uint8_t> payload(body.begin(), body.end());
    return write_frame(frame(3, 1, payload));
}

}  // namespace synapse::amqp
