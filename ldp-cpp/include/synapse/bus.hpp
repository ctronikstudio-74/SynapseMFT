#pragma once

#include "envelope.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace synapse::bus {

struct CapabilityCard {
    std::string id;
    std::vector<std::string> publish_prefixes;
    std::vector<std::string> subscribe_prefixes;
    bool may_propose = false;
    bool may_approve = false;
    bool may_commit_direct = false;
    size_t max_payload = 65536;
};

struct OpenInfo {
    size_t records = 0;
    bool truncated_partial_tail = false;
};

using Executor = std::function<Status(const EventEnvelope&)>;
using Handler = std::function<void(const EventEnvelope&)>;

class AgenticEventBus;

struct BusState;

class AgenticEventBus {
public:
    AgenticEventBus();
    ~AgenticEventBus();
    AgenticEventBus(const AgenticEventBus&) = delete;
    AgenticEventBus& operator=(const AgenticEventBus&) = delete;

    Result<OpenInfo> open(const std::filesystem::path& directory);
    void close();

    Status register_agent(CapabilityCard card);
    Status subscribe(const std::string& agent_id, const std::string& prefix, Handler handler);
    Status publish(EventEnvelope event);
    Status claim(const std::string& step, const std::string& holder, int64_t ttl_ms);
    Status release(const std::string& step, const std::string& holder);
    Status sweep(int64_t now);
    Result<EventEnvelope> compact(const std::string& correlation, size_t keep_subjects);
    std::vector<EventEnvelope> replay(const std::string& agent_id) const;
    Status commit_cursor(const std::string& agent_id, uint64_t offset);
    void set_executor(Executor executor);
    std::vector<EventEnvelope> records() const;

private:
    BusState* state_;
};

}  // namespace synapse::bus
