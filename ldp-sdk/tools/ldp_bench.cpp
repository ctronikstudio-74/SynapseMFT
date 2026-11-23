#include "ldp/sdk.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto rows = ldp::sdk::run_public_bench();
        const auto text = ldp::sdk::format_public_bench(rows);
        std::cout << text;
        std::ofstream out("ldp-bench-result.txt", std::ios::binary);
        if (!out) {
            throw std::runtime_error("cannot write ldp-bench-result.txt");
        }
        out << text;
        for (const auto& row : rows) {
            if (!row.match) {
                std::cerr << "benchmark payload mismatch\n";
                return 1;
            }
        }
        std::cout << "wrote ldp-bench-result.txt\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ldp-bench failed: " << ex.what() << "\n";
        return 1;
    }
}
