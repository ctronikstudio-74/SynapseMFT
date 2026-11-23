#pragma once

#include "result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace synapse::net {

class TcpStream {
public:
    TcpStream() = default;
    TcpStream(TcpStream&& other) noexcept;
    TcpStream& operator=(TcpStream&& other) noexcept;
    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    static Result<TcpStream> connect(const std::string& host, uint16_t port, int timeout_ms);
    Status write_all(const uint8_t* data, size_t size);
    Status write_all(const std::vector<uint8_t>& data) { return write_all(data.data(), data.size()); }
    Status write_all(const std::string& data) {
        return write_all(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }
    Result<size_t> read_some(uint8_t* data, size_t size, int timeout_ms);
    Status read_exact(uint8_t* data, size_t size, int timeout_ms);
    void close();
    bool valid() const { return socket_ >= 0; }
    intptr_t native_handle() const { return socket_; }

private:
    friend class TcpListener;
    explicit TcpStream(intptr_t socket) : socket_(socket) {}
    intptr_t socket_ = -1;
};

class TcpListener {
public:
    TcpListener() = default;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    static Result<TcpListener> listen(const std::string& host, uint16_t port);
    Result<TcpStream> accept(int timeout_ms);
    uint16_t port() const { return port_; }
    void close();

private:
    intptr_t socket_ = -1;
    uint16_t port_ = 0;
};

struct HttpRequest {
    std::string method = "POST";
    std::string host;
    uint16_t port = 80;
    bool tls = false;
    std::string target = "/";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    int status = 0;
    std::string headers;
    std::string body;
};

bool tls_available();
Result<HttpRequest> parse_url(const std::string& method, const std::string& url, const std::string& body);
Result<HttpResponse> http_exchange(const HttpRequest& request, int timeout_ms);

}  // namespace synapse::net
