#include "ldp/sdk.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage() {
    std::cerr << "ldp-try\n"
              << "  ldp-try                         classic loopback, 1 MiB\n"
              << "  ldp-try --hybrid                hybrid loopback, 256 KiB\n"
              << "  ldp-try listen --port 4590 --output received.bin\n"
              << "  ldp-try send --host 127.0.0.1 --port 4590 --bytes 1048576\n"
              << "  ldp-try send --host 127.0.0.1 --port 4590 --input file.bin\n";
}

std::string arg_value(int argc, char** argv, int& i) {
    if (i + 1 >= argc) {
        throw std::runtime_error(std::string("missing value after ") + argv[i]);
    }
    return argv[++i];
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> pattern(size_t bytes) {
    std::vector<uint8_t> payload(bytes);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xff);
    }
    return payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc <= 1) {
            ldp::sdk::TransferOptions options;
            const auto payload = pattern(1 << 20);
            const auto row = ldp::sdk::measure_loopback(payload, options);
            std::cout << ldp::sdk::format_public_bench({row});
            if (!row.match) {
                return 1;
            }
            std::cout << "ldp-try ok\n";
            return 0;
        }
        const std::string command = argv[1];
        if (command == "--hybrid") {
            ldp::sdk::TransferOptions options;
            options.profile = ldp::sdk::Profile::HybridPq1;
            const auto row = ldp::sdk::measure_loopback(pattern(256 * 1024), options);
            std::cout << ldp::sdk::format_public_bench({row});
            if (!row.match) {
                return 1;
            }
            std::cout << "ldp-try hybrid ok\n";
            return 0;
        }
        if (command == "listen") {
            uint16_t port = 4590;
            std::string output;
            for (int i = 2; i < argc; ++i) {
                const std::string flag = argv[i];
                if (flag == "--port") {
                    port = static_cast<uint16_t>(std::stoi(arg_value(argc, argv, i)));
                } else if (flag == "--output") {
                    output = arg_value(argc, argv, i);
                } else {
                    usage();
                    return 2;
                }
            }
            std::cout << "listening on 127.0.0.1:" << port << "\n";
            ldp::sdk::udp_listen(port, output, {});
            std::cout << "ldp-try listen ok\n";
            return 0;
        }
        if (command == "send") {
            std::string host = "127.0.0.1";
            uint16_t port = 4590;
            std::string input;
            size_t bytes = 1 << 20;
            bool have_bytes = false;
            for (int i = 2; i < argc; ++i) {
                const std::string flag = argv[i];
                if (flag == "--host") {
                    host = arg_value(argc, argv, i);
                } else if (flag == "--port") {
                    port = static_cast<uint16_t>(std::stoi(arg_value(argc, argv, i)));
                } else if (flag == "--input") {
                    input = arg_value(argc, argv, i);
                } else if (flag == "--bytes") {
                    bytes = static_cast<size_t>(std::stoull(arg_value(argc, argv, i)));
                    have_bytes = true;
                } else {
                    usage();
                    return 2;
                }
            }
            const auto payload = input.empty() ? pattern(bytes) : read_file(input);
            if (!input.empty() && have_bytes) {
                throw std::runtime_error("use --input or --bytes, not both");
            }
            ldp::sdk::udp_send(host, port, payload, {});
            std::cout << "ldp-try send ok " << payload.size() << " bytes\n";
            return 0;
        }
        usage();
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "ldp-try failed: " << ex.what() << "\n";
        return 1;
    }
}
