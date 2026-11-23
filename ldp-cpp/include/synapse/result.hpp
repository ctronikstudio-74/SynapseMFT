#pragma once

#include <string>
#include <utility>

namespace synapse {

struct Status {
    std::string error;

    bool ok() const { return error.empty(); }
    static Status success() { return {}; }
    static Status failure(std::string message) { return Status{std::move(message)}; }
};

template <typename T>
struct Result {
    T value{};
    std::string error;

    bool ok() const { return error.empty(); }
    static Result success(T v) {
        Result out;
        out.value = std::move(v);
        return out;
    }
    static Result failure(std::string message) {
        Result out;
        out.error = std::move(message);
        return out;
    }
};

}  // namespace synapse
