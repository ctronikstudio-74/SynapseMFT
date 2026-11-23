#include "synapse/net.hpp"

#include <cerrno>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(SYNAPSE_HAVE_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace synapse::net {
namespace {

using clock = std::chrono::steady_clock;

int remaining_ms(clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
    if (left <= 0) return 0;
    if (left > 600000) return 600000;
    return static_cast<int>(left);
}

void startup() {
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef _WIN32
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
    });
}

void close_socket(intptr_t socket) {
    if (socket < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

Status set_nonblocking(intptr_t socket) {
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(static_cast<SOCKET>(socket), FIONBIO, &mode) != 0) return Status::failure("ioctlsocket failed");
#else
    const int flags = fcntl(static_cast<int>(socket), F_GETFL, 0);
    if (flags < 0 || fcntl(static_cast<int>(socket), F_SETFL, flags | O_NONBLOCK) != 0) {
        return Status::failure("fcntl failed");
    }
#endif
    return Status::success();
}

int last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool would_block(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS;
#endif
}

Status wait_socket(intptr_t socket, bool write, int timeout_ms) {
    if (timeout_ms <= 0) return Status::failure("timeout");
    fd_set set;
    FD_ZERO(&set);
#ifdef _WIN32
    SOCKET native = static_cast<SOCKET>(socket);
    FD_SET(native, &set);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int ready = select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
#else
    const int native = static_cast<int>(socket);
    FD_SET(native, &set);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int ready = select(native + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
#endif
    if (ready == 0) return Status::failure("timeout");
    if (ready < 0) return Status::failure("select failed");
    return Status::success();
}

}  // namespace

TcpStream::TcpStream(TcpStream&& other) noexcept : socket_(other.socket_) { other.socket_ = -1; }

TcpStream& TcpStream::operator=(TcpStream&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        other.socket_ = -1;
    }
    return *this;
}

TcpStream::~TcpStream() { close(); }

void TcpStream::close() {
    close_socket(socket_);
    socket_ = -1;
}

Result<TcpStream> TcpStream::connect(const std::string& host, uint16_t port, int timeout_ms) {
    startup();
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* result = nullptr;
    const std::string port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        return Result<TcpStream>::failure("dns failed");
    }
    Status error = Status::failure("connect failed");
    intptr_t connected = -1;
    for (addrinfo* it = result; it; it = it->ai_next) {
        const intptr_t socket = static_cast<intptr_t>(::socket(it->ai_family, it->ai_socktype, it->ai_protocol));
        if (socket < 0) continue;
        if (auto status = set_nonblocking(socket); !status.ok()) {
            close_socket(socket);
            error = status;
            continue;
        }
        const int rc = ::connect(static_cast<int>(socket), it->ai_addr, static_cast<int>(it->ai_addrlen));
#ifdef _WIN32
        const bool in_progress = rc != 0 && would_block(WSAGetLastError());
#else
        const bool in_progress = rc != 0 && would_block(errno);
#endif
        if (rc != 0 && !in_progress) {
            close_socket(socket);
            continue;
        }
        if (in_progress) {
            if (auto status = wait_socket(socket, true, timeout_ms); !status.ok()) {
                close_socket(socket);
                error = status;
                continue;
            }
            int so_error = 0;
            socklen_t length = sizeof(so_error);
            getsockopt(static_cast<int>(socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &length);
            if (so_error != 0) {
                close_socket(socket);
                continue;
            }
        }
        connected = socket;
        break;
    }
    freeaddrinfo(result);
    if (connected < 0) return Result<TcpStream>::failure(error.error);
    return Result<TcpStream>::success(TcpStream(connected));
}

Status TcpStream::write_all(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        if (auto status = wait_socket(socket_, true, 5000); !status.ok()) return status;
#ifdef _WIN32
        const int n = ::send(static_cast<SOCKET>(socket_), reinterpret_cast<const char*>(data + sent),
                             static_cast<int>(size - sent), 0);
#else
        const int n = ::send(static_cast<int>(socket_), data + sent, size - sent, 0);
#endif
        if (n <= 0) return Status::failure("send failed");
        sent += static_cast<size_t>(n);
    }
    return Status::success();
}

Result<size_t> TcpStream::read_some(uint8_t* data, size_t size, int timeout_ms) {
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const int left = remaining_ms(deadline);
        if (auto status = wait_socket(socket_, false, left); !status.ok()) return Result<size_t>::failure(status.error);
#ifdef _WIN32
        const int n = ::recv(static_cast<SOCKET>(socket_), reinterpret_cast<char*>(data), static_cast<int>(size), 0);
#else
        const int n = ::recv(static_cast<int>(socket_), data, size, 0);
#endif
        if (n == 0) return Result<size_t>::success(0);
        if (n > 0) return Result<size_t>::success(static_cast<size_t>(n));
        if (!would_block(last_error())) return Result<size_t>::failure("recv failed");
    }
}

Status TcpStream::read_exact(uint8_t* data, size_t size, int timeout_ms) {
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    size_t got = 0;
    while (got < size) {
        const int left = remaining_ms(deadline);
        auto chunk = read_some(data + got, size - got, left);
        if (!chunk.ok()) return Status::failure(chunk.error);
        if (chunk.value == 0) return Status::failure(got == 0 ? "closed" : "short read");
        got += chunk.value;
    }
    return Status::success();
}

TcpListener::TcpListener(TcpListener&& other) noexcept : socket_(other.socket_), port_(other.port_) {
    other.socket_ = -1;
    other.port_ = 0;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        port_ = other.port_;
        other.socket_ = -1;
        other.port_ = 0;
    }
    return *this;
}

TcpListener::~TcpListener() { close(); }

void TcpListener::close() {
    close_socket(socket_);
    socket_ = -1;
}

Result<TcpListener> TcpListener::listen(const std::string& host, uint16_t port) {
    startup();
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* result = nullptr;
    const std::string port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        return Result<TcpListener>::failure("listen dns failed");
    }
    TcpListener listener;
    for (addrinfo* it = result; it; it = it->ai_next) {
        const intptr_t socket = static_cast<intptr_t>(::socket(it->ai_family, it->ai_socktype, it->ai_protocol));
        if (socket < 0) continue;
        int reuse = 1;
        setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof(reuse));
        if (::bind(static_cast<int>(socket), it->ai_addr, static_cast<int>(it->ai_addrlen)) != 0 ||
            ::listen(static_cast<int>(socket), 16) != 0) {
            close_socket(socket);
            continue;
        }
        sockaddr_storage storage{};
        socklen_t length = sizeof(storage);
        getsockname(static_cast<int>(socket), reinterpret_cast<sockaddr*>(&storage), &length);
        listener.socket_ = socket;
        if (storage.ss_family == AF_INET) {
            listener.port_ = ntohs(reinterpret_cast<sockaddr_in*>(&storage)->sin_port);
        }
        break;
    }
    freeaddrinfo(result);
    if (listener.socket_ < 0) return Result<TcpListener>::failure("bind failed");
    if (auto status = set_nonblocking(listener.socket_); !status.ok()) return Result<TcpListener>::failure(status.error);
    return Result<TcpListener>::success(std::move(listener));
}

Result<TcpStream> TcpListener::accept(int timeout_ms) {
    if (auto status = wait_socket(socket_, false, timeout_ms); !status.ok()) return Result<TcpStream>::failure(status.error);
#ifdef _WIN32
    const intptr_t client = static_cast<intptr_t>(::accept(static_cast<SOCKET>(socket_), nullptr, nullptr));
#else
    const intptr_t client = static_cast<intptr_t>(::accept(static_cast<int>(socket_), nullptr, nullptr));
#endif
    if (client < 0) return Result<TcpStream>::failure("accept failed");
    if (auto status = set_nonblocking(client); !status.ok()) {
        close_socket(client);
        return Result<TcpStream>::failure(status.error);
    }
    return Result<TcpStream>::success(TcpStream(client));
}

Result<HttpRequest> parse_url(const std::string& method, const std::string& url, const std::string& body) {
    HttpRequest request;
    request.method = method;
    request.body = body;
    std::string rest = url;
    if (rest.rfind("https://", 0) == 0) {
        request.tls = true;
        request.port = 443;
        rest = rest.substr(8);
    } else if (rest.rfind("http://", 0) == 0) {
        request.tls = false;
        request.port = 80;
        rest = rest.substr(7);
    } else {
        return Result<HttpRequest>::failure("url must start with http:// or https://");
    }
    const auto slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    request.target = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = authority.find(':');
    if (colon == std::string::npos) {
        request.host = authority;
    } else {
        request.host = authority.substr(0, colon);
        try {
            request.port = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
        } catch (...) {
            return Result<HttpRequest>::failure("bad port");
        }
    }
    if (request.host.empty()) return Result<HttpRequest>::failure("missing host");
    return Result<HttpRequest>::success(std::move(request));
}

namespace {

std::string header_value(const std::string& headers, const std::string& name) {
    const std::string needle = name + ":";
    size_t pos = 0;
    while (pos < headers.size()) {
        const auto end = headers.find("\r\n", pos);
        const auto line = headers.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (line.size() >= needle.size()) {
            bool match = true;
            for (size_t i = 0; i < needle.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(line[i])) !=
                    std::tolower(static_cast<unsigned char>(needle[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                size_t value = needle.size();
                while (value < line.size() && line[value] == ' ') ++value;
                return line.substr(value);
            }
        }
        if (end == std::string::npos) break;
        pos = end + 2;
    }
    return {};
}

#if defined(SYNAPSE_HAVE_OPENSSL)
Result<HttpResponse> read_tls(SSL* ssl, const HttpRequest& request, int timeout_ms) {
    std::string message = request.method + " " + request.target + " HTTP/1.1\r\n";
    message += "Host: " + request.host + "\r\n";
    message += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
    message += "Connection: close\r\n";
    for (const auto& header : request.headers) message += header.first + ": " + header.second + "\r\n";
    message += "\r\n";
    message += request.body;
    size_t sent = 0;
    while (sent < message.size()) {
        const int n = SSL_write(ssl, message.data() + sent, static_cast<int>(message.size() - sent));
        if (n <= 0) return Result<HttpResponse>::failure("tls write failed");
        sent += static_cast<size_t>(n);
    }
    std::string raw;
    char buffer[4096];
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while (remaining_ms(deadline) > 0) {
        const int n = SSL_read(ssl, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0) return Result<HttpResponse>::failure("tls read failed");
        raw.append(buffer, buffer + n);
        if (raw.find("\r\n\r\n") != std::string::npos) {
            const auto length_text = header_value(raw, "content-length");
            if (!length_text.empty()) {
                const auto header_end = raw.find("\r\n\r\n");
                const size_t have = raw.size() - (header_end + 4);
                if (have >= static_cast<size_t>(std::stoul(length_text))) break;
            }
        }
    }
    const auto split = raw.find("\r\n\r\n");
    if (split == std::string::npos) return Result<HttpResponse>::failure("no http headers");
    HttpResponse response;
    response.headers = raw.substr(0, split);
    response.body = raw.substr(split + 4);
    if (raw.rfind("HTTP/", 0) != 0) return Result<HttpResponse>::failure("bad status line");
    const auto space = raw.find(' ');
    if (space == std::string::npos) return Result<HttpResponse>::failure("bad status line");
    response.status = std::stoi(raw.substr(space + 1));
    return Result<HttpResponse>::success(std::move(response));
}

Result<HttpResponse> https_exchange(const HttpRequest& request, int timeout_ms) {
    static std::once_flag once;
    std::call_once(once, [] { OPENSSL_init_ssl(0, nullptr); });
    auto stream = TcpStream::connect(request.host, request.port, timeout_ms);
    if (!stream.ok()) return Result<HttpResponse>::failure(stream.error);
    const intptr_t handle = stream.value.native_handle();
    if (handle < 0 || handle > 0x7fffffff) return Result<HttpResponse>::failure("ssl socket handle is too large");
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return Result<HttpResponse>::failure("ssl context failed");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        SSL_CTX_free(ctx);
        return Result<HttpResponse>::failure("ssl ca store failed");
    }
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        return Result<HttpResponse>::failure("ssl failed");
    }
    SSL_set_tlsext_host_name(ssl, request.host.c_str());
    SSL_set1_host(ssl, request.host.c_str());
    SSL_set_fd(ssl, static_cast<int>(handle));
#ifdef _WIN32
    u_long blocking = 0;
    ioctlsocket(static_cast<SOCKET>(handle), FIONBIO, &blocking);
#else
    const int flags = fcntl(static_cast<int>(handle), F_GETFL, 0);
    if (flags >= 0) fcntl(static_cast<int>(handle), F_SETFL, flags & ~O_NONBLOCK);
#endif
    if (SSL_connect(ssl) != 1) {
        char buffer[256];
        ERR_error_string_n(ERR_get_error(), buffer, sizeof(buffer));
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return Result<HttpResponse>::failure(std::string("tls handshake failed: ") + buffer);
    }
    auto response = read_tls(ssl, request, timeout_ms);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return response;
}
#endif

}  // namespace

bool tls_available() {
#if defined(SYNAPSE_HAVE_OPENSSL)
    return true;
#else
    return false;
#endif
}

Result<HttpResponse> http_exchange(const HttpRequest& request, int timeout_ms) {
#if defined(SYNAPSE_HAVE_OPENSSL)
    if (request.tls) return https_exchange(request, timeout_ms);
#else
    if (request.tls) return Result<HttpResponse>::failure("this build has no OpenSSL; https is unavailable");
#endif
    auto stream = TcpStream::connect(request.host, request.port, timeout_ms);
    if (!stream.ok()) return Result<HttpResponse>::failure(stream.error);
    std::string message = request.method + " " + request.target + " HTTP/1.1\r\n";
    message += "Host: " + request.host + "\r\n";
    message += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
    message += "Connection: close\r\n";
    for (const auto& header : request.headers) message += header.first + ": " + header.second + "\r\n";
    message += "\r\n";
    message += request.body;
    if (auto status = stream.value.write_all(message); !status.ok()) return Result<HttpResponse>::failure(status.error);
    std::string raw;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while (remaining_ms(deadline) > 0) {
        uint8_t buffer[4096];
        auto chunk = stream.value.read_some(buffer, sizeof(buffer), remaining_ms(deadline));
        if (!chunk.ok()) return Result<HttpResponse>::failure(chunk.error);
        if (chunk.value == 0) break;
        raw.append(reinterpret_cast<char*>(buffer), chunk.value);
        const auto split = raw.find("\r\n\r\n");
        if (split != std::string::npos) {
            const auto length_text = header_value(raw, "content-length");
            if (!length_text.empty()) {
                size_t length = 0;
                try {
                    length = static_cast<size_t>(std::stoul(length_text));
                } catch (...) {
                    return Result<HttpResponse>::failure("bad content-length");
                }
                if (raw.size() >= split + 4 + length) break;
            }
        }
    }
    const auto split = raw.find("\r\n\r\n");
    if (split == std::string::npos) return Result<HttpResponse>::failure("no http headers");
    HttpResponse response;
    response.headers = raw.substr(0, split);
    response.body = raw.substr(split + 4);
    const auto length_text = header_value(response.headers, "content-length");
    if (!length_text.empty()) {
        const size_t length = static_cast<size_t>(std::stoul(length_text));
        if (response.body.size() > length) response.body.resize(length);
    }
    if (raw.rfind("HTTP/", 0) != 0) return Result<HttpResponse>::failure("bad status line");
    const auto space = raw.find(' ');
    if (space == std::string::npos) return Result<HttpResponse>::failure("bad status line");
    try {
        response.status = std::stoi(raw.substr(space + 1));
    } catch (...) {
        return Result<HttpResponse>::failure("bad status code");
    }
    return Result<HttpResponse>::success(std::move(response));
}

}  // namespace synapse::net
