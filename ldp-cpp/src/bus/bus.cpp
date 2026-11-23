#include "synapse/bus.hpp"
#include "synapse/codec.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace synapse::bus {
namespace {

constexpr size_t kHardPayloadCap = 1024 * 1024;
const char kMagic[4] = {'S', 'B', '1', '\n'};

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool prefix_allowed(const std::vector<std::string>& prefixes, const std::string& topic) {
    for (const auto& prefix : prefixes) {
        if (starts_with(topic, prefix)) return true;
    }
    return false;
}

struct Pending {
    EventEnvelope proposal;
};

struct Lease {
    std::string holder;
    int64_t expires_ms = 0;
};

void durable_flush(FILE* file) {
    std::fflush(file);
#ifdef _WIN32
    _commit(_fileno(file));
#else
    fsync(fileno(file));
#endif
}

}  // namespace

struct BusState {
    mutable std::mutex mutex;
    std::filesystem::path directory;
    FILE* log = nullptr;
    std::vector<EventEnvelope> records;
    std::map<std::string, CapabilityCard> cards;
    std::map<std::string, Pending> pending;
    std::map<std::string, Lease> leases;
    std::map<std::string, uint64_t> cursors;
    std::map<std::string, uint64_t> watermarks;
    struct Subscription {
        std::string agent_id;
        std::string prefix;
        Handler handler;
    };
    std::vector<Subscription> subscriptions;
    Executor executor;
};

AgenticEventBus::AgenticEventBus() : state_(new BusState) {}

AgenticEventBus::~AgenticEventBus() {
    close();
    delete state_;
}

void AgenticEventBus::close() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->log) {
        durable_flush(state_->log);
        std::fclose(state_->log);
        state_->log = nullptr;
    }
}

Result<OpenInfo> AgenticEventBus::open(const std::filesystem::path& directory) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->log) return Result<OpenInfo>::failure("bus already open");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return Result<OpenInfo>::failure("cannot create bus directory");
    state_->directory = directory;
    const auto path = directory / "events.log";
    FILE* file = std::fopen(path.string().c_str(), "ab+");
    if (!file) return Result<OpenInfo>::failure("cannot open event log");
    std::fseek(file, 0, SEEK_END);
    const long end = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    OpenInfo info;
    std::vector<uint8_t> bytes;
    if (end > 0) {
        bytes.resize(static_cast<size_t>(end));
        if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
            std::fclose(file);
            return Result<OpenInfo>::failure("cannot read event log");
        }
    }
    size_t index = 0;
    while (index + 8 <= bytes.size()) {
        if (std::memcmp(bytes.data() + index, kMagic, 4) != 0) {
            std::fclose(file);
            return Result<OpenInfo>::failure("event log magic mismatch");
        }
        const uint32_t length = (uint32_t(bytes[index + 4]) << 24) | (uint32_t(bytes[index + 5]) << 16) |
                                (uint32_t(bytes[index + 6]) << 8) | uint32_t(bytes[index + 7]);
        const size_t record = 8u + length + 4u;
        if (index + record > bytes.size()) {
            info.truncated_partial_tail = true;
            break;
        }
        const uint8_t* payload = bytes.data() + index + 8;
        const uint32_t stored = (uint32_t(payload[length]) << 24) | (uint32_t(payload[length + 1]) << 16) |
                                (uint32_t(payload[length + 2]) << 8) | uint32_t(payload[length + 3]);
        const uint32_t actual = codec::crc32_iso(payload, length);
        if (stored != actual) {
            std::fclose(file);
            return Result<OpenInfo>::failure("event log crc mismatch");
        }
        auto event = parse_envelope(std::string_view(reinterpret_cast<const char*>(payload), length));
        if (!event.ok()) {
            std::fclose(file);
            return Result<OpenInfo>::failure(event.error);
        }
        event.value.offset = index;
        state_->records.push_back(std::move(event.value));
        index += record;
    }
    if (index < bytes.size()) info.truncated_partial_tail = true;
    if (info.truncated_partial_tail) {
        std::fclose(file);
        file = std::fopen(path.string().c_str(), "wb+");
        if (!file) return Result<OpenInfo>::failure("cannot rewrite truncated log");
        for (const auto& event : state_->records) {
            const std::string json = canonical_envelope(event);
            uint8_t header[8];
            std::memcpy(header, kMagic, 4);
            const uint32_t length = static_cast<uint32_t>(json.size());
            header[4] = static_cast<uint8_t>(length >> 24);
            header[5] = static_cast<uint8_t>(length >> 16);
            header[6] = static_cast<uint8_t>(length >> 8);
            header[7] = static_cast<uint8_t>(length);
            const uint32_t crc = codec::crc32_iso(reinterpret_cast<const uint8_t*>(json.data()), json.size());
            uint8_t tail[4] = {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
                               static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
            std::fwrite(header, 1, 8, file);
            std::fwrite(json.data(), 1, json.size(), file);
            std::fwrite(tail, 1, 4, file);
        }
        durable_flush(file);
    }
    state_->log = file;
    info.records = state_->records.size();

    auto load_map = [&](const char* name, auto insert) {
        std::ifstream input((directory / name).string());
        std::string left;
        uint64_t right = 0;
        while (input >> left >> right) insert(left, right);
    };
    load_map("cursors.txt", [&](const std::string& id, uint64_t offset) { state_->cursors[id] = offset; });
    load_map("watermarks.txt", [&](const std::string& id, uint64_t offset) { state_->watermarks[id] = offset; });
    {
        std::ifstream input((directory / "leases.txt").string());
        std::string step;
        std::string holder;
        int64_t expiry = 0;
        while (input >> step >> holder >> expiry) {
            if (expiry > now_ms()) state_->leases[step] = Lease{holder, expiry};
        }
    }
    for (const auto& event : state_->records) {
        if (event.verb == Verb::Propose && event.requires_approval) state_->pending[event.id] = Pending{event};
        if (event.verb == Verb::Approve || event.verb == Verb::Veto || event.verb == Verb::Expire) {
            state_->pending.erase(event.causation);
        }
    }
    return Result<OpenInfo>::success(info);
}

Status AgenticEventBus::register_agent(CapabilityCard card) {
    if (!safe_token(card.id, false)) return Status::failure("unknown agent");
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->cards[card.id] = std::move(card);
    return Status::success();
}

Status AgenticEventBus::subscribe(const std::string& agent_id, const std::string& prefix, Handler handler) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto card = state_->cards.find(agent_id);
    if (card == state_->cards.end()) return Status::failure("unknown agent");
    if (!prefix_allowed(card->second.subscribe_prefixes, prefix)) return Status::failure("capability denied");
    state_->subscriptions.push_back(BusState::Subscription{agent_id, prefix, std::move(handler)});
    return Status::success();
}

void AgenticEventBus::set_executor(Executor executor) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->executor = std::move(executor);
}

std::vector<EventEnvelope> AgenticEventBus::records() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->records;
}

namespace {

Status append_record(BusState& state, EventEnvelope& event) {
    if (!state.log) return Status::failure("bus is closed");
    std::fseek(state.log, 0, SEEK_END);
    const long position = std::ftell(state.log);
    if (position < 0) return Status::failure("event log position failed");
    event.offset = static_cast<uint64_t>(position);
    const std::string json = canonical_envelope(event);
    uint8_t header[8];
    std::memcpy(header, kMagic, 4);
    const auto length = static_cast<uint32_t>(json.size());
    header[4] = static_cast<uint8_t>(length >> 24);
    header[5] = static_cast<uint8_t>(length >> 16);
    header[6] = static_cast<uint8_t>(length >> 8);
    header[7] = static_cast<uint8_t>(length);
    const uint32_t crc = codec::crc32_iso(reinterpret_cast<const uint8_t*>(json.data()), json.size());
    uint8_t tail[4] = {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
                       static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
    if (std::fwrite(header, 1, 8, state.log) != 8 || std::fwrite(json.data(), 1, json.size(), state.log) != json.size() ||
        std::fwrite(tail, 1, 4, state.log) != 4) {
        return Status::failure("event log write failed");
    }
    durable_flush(state.log);
    state.records.push_back(event);
    return Status::success();
}

void save_lines(const std::filesystem::path& path, const std::vector<std::string>& lines) {
    std::ofstream output(path.string(), std::ios::trunc);
    for (const auto& line : lines) output << line << '\n';
}

void save_cursors(BusState& state) {
    std::vector<std::string> lines;
    for (const auto& [id, offset] : state.cursors) lines.push_back(id + " " + std::to_string(offset));
    save_lines(state.directory / "cursors.txt", lines);
}

void save_watermarks(BusState& state) {
    std::vector<std::string> lines;
    for (const auto& [id, offset] : state.watermarks) lines.push_back(id + " " + std::to_string(offset));
    save_lines(state.directory / "watermarks.txt", lines);
}

void save_leases(BusState& state) {
    std::vector<std::string> lines;
    for (const auto& [step, lease] : state.leases) {
        lines.push_back(step + " " + lease.holder + " " + std::to_string(lease.expires_ms));
    }
    save_lines(state.directory / "leases.txt", lines);
}

std::vector<Handler> matching(BusState& state, const EventEnvelope& event) {
    std::vector<Handler> handlers;
    for (const auto& sub : state.subscriptions) {
        if (starts_with(event.topic, sub.prefix)) handlers.push_back(sub.handler);
    }
    return handlers;
}

}  // namespace

Status AgenticEventBus::publish(EventEnvelope event) {
    std::vector<Handler> handlers;
    Executor executor;
    EventEnvelope committed;
    bool run_executor = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->log) return Status::failure("bus is closed");
        const auto card_it = state_->cards.find(event.actor.id);
        if (card_it == state_->cards.end()) return Status::failure("unknown agent");
        const CapabilityCard& card = card_it->second;
        if (event.payload.size() > card.max_payload || event.payload.size() > kHardPayloadCap) {
            return Status::failure("payload too large");
        }
        if (!safe_token(event.topic, false) || !safe_token(event.subject, true) || !safe_token(event.correlation, true) ||
            !safe_token(event.causation, true) || !safe_token(event.actor.model, true)) {
            return Status::failure("topic rejected");
        }
        if (event.id.empty()) event.id = make_id();
        if (!safe_token(event.id, false)) return Status::failure("topic rejected");
        for (const auto& existing : state_->records) {
            if (existing.id == event.id) return Status::failure("duplicate id");
        }

        if (event.verb == Verb::Observe) {
            if (!starts_with(event.topic, "obs.")) return Status::failure("topic rejected");
            if (!prefix_allowed(card.publish_prefixes, event.topic)) return Status::failure("capability denied");
        } else if (event.verb == Verb::Propose) {
            if (!starts_with(event.topic, "act.")) return Status::failure("topic rejected");
            if (!card.may_propose || !prefix_allowed(card.publish_prefixes, event.topic)) {
                return Status::failure("capability denied");
            }
            if (!event.subject.empty()) {
                const auto lease = state_->leases.find(event.subject);
                if (lease == state_->leases.end() || lease->second.expires_ms <= now_ms() ||
                    lease->second.holder != event.actor.id) {
                    return Status::failure("lease held");
                }
            }
            if (!event.requires_approval && !card.may_commit_direct) return Status::failure("direct act denied");
        } else if (event.verb == Verb::Approve || event.verb == Verb::Veto) {
            if (!card.may_approve) return Status::failure("capability denied");
            const auto pending = state_->pending.find(event.causation);
            if (pending == state_->pending.end()) return Status::failure("no pending proposal");
            if (pending->second.proposal.actor.id == event.actor.id) return Status::failure("four-eyes");
            if (pending->second.proposal.deadline_ms > 0 && now_ms() > pending->second.proposal.deadline_ms) {
                return Status::failure("deadline passed");
            }
        } else if (event.verb == Verb::Act || event.verb == Verb::Expire || event.verb == Verb::Memory) {
            return Status::failure("direct act denied");
        } else {
            return Status::failure("topic rejected");
        }

        if (auto status = append_record(*state_, event); !status.ok()) return status;
        handlers = matching(*state_, event);

        if (event.verb == Verb::Propose && event.requires_approval) {
            state_->pending[event.id] = Pending{event};
        }
        if (event.verb == Verb::Propose && !event.requires_approval) {
            EventEnvelope act = event;
            act.id = make_id();
            act.verb = Verb::Act;
            act.causation = event.id;
            act.actor.kind = ActorKind::Node;
            act.actor.id = "synapse-bus";
            act.actor.model.clear();
            if (auto status = append_record(*state_, act); !status.ok()) return status;
            committed = act;
            run_executor = true;
            executor = state_->executor;
        }
        if (event.verb == Verb::Approve) {
            auto pending = state_->pending.find(event.causation);
            EventEnvelope act = pending->second.proposal;
            state_->pending.erase(pending);
            act.id = make_id();
            act.verb = Verb::Act;
            act.causation = event.causation;
            act.actor = event.actor;
            if (auto status = append_record(*state_, act); !status.ok()) return status;
            committed = act;
            run_executor = true;
            executor = state_->executor;
        }
        if (event.verb == Verb::Veto) state_->pending.erase(event.causation);
    }
    for (const auto& handler : handlers) handler(event);
    if (run_executor) {
        std::vector<Handler> act_handlers;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            act_handlers = matching(*state_, committed);
        }
        for (const auto& handler : act_handlers) handler(committed);
    }
    if (run_executor && executor) {
        auto status = executor(committed);
        if (!status.ok()) return status;
    }
    return Status::success();
}

Status AgenticEventBus::claim(const std::string& step, const std::string& holder, int64_t ttl_ms) {
    if (!safe_token(step, false) || !safe_token(holder, false) || ttl_ms <= 0) return Status::failure("lease held");
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->cards.count(holder)) return Status::failure("unknown agent");
    const int64_t now = now_ms();
    const auto it = state_->leases.find(step);
    if (it != state_->leases.end() && it->second.expires_ms > now && it->second.holder != holder) {
        return Status::failure("lease held");
    }
    state_->leases[step] = Lease{holder, now + ttl_ms};
    save_leases(*state_);
    return Status::success();
}

Status AgenticEventBus::release(const std::string& step, const std::string& holder) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto it = state_->leases.find(step);
    if (it == state_->leases.end()) return Status::success();
    if (it->second.holder != holder) return Status::failure("lease held");
    state_->leases.erase(it);
    save_leases(*state_);
    return Status::success();
}

Status AgenticEventBus::sweep(int64_t now) {
    std::vector<std::pair<EventEnvelope, std::vector<Handler>>> expired;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->log) return Status::failure("bus is closed");
        std::vector<std::string> ids;
        for (const auto& [id, pending] : state_->pending) {
            if (pending.proposal.deadline_ms > 0 && now > pending.proposal.deadline_ms) ids.push_back(id);
        }
        for (const auto& id : ids) {
            EventEnvelope event;
            event.id = make_id();
            event.causation = id;
            event.correlation = state_->pending[id].proposal.correlation;
            event.actor.kind = ActorKind::Node;
            event.actor.id = "synapse-bus";
            event.verb = Verb::Expire;
            event.topic = "act.expire";
            event.subject = state_->pending[id].proposal.subject;
            event.requires_approval = false;
            if (auto status = append_record(*state_, event); !status.ok()) return status;
            state_->pending.erase(id);
            expired.push_back({event, matching(*state_, event)});
        }
    }
    for (const auto& item : expired) {
        for (const auto& handler : item.second) handler(item.first);
    }
    return Status::success();
}

Result<EventEnvelope> AgenticEventBus::compact(const std::string& correlation, size_t keep_subjects) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->log) return Result<EventEnvelope>::failure("bus is closed");
    if (!safe_token(correlation, false)) return Result<EventEnvelope>::failure("topic rejected");
    std::vector<std::string> subjects;
    const auto existing = state_->watermarks.find(correlation);
    const bool has_watermark = existing != state_->watermarks.end();
    const uint64_t watermark = has_watermark ? existing->second : 0;
    uint64_t last = watermark;
    for (const auto& event : state_->records) {
        if (event.verb != Verb::Observe || event.correlation != correlation) continue;
        if (has_watermark && event.offset <= watermark) continue;
        subjects.push_back(event.subject);
        last = event.offset;
    }
    if (subjects.empty()) return Result<EventEnvelope>::failure("nothing to compact");
    const size_t begin = subjects.size() > keep_subjects ? subjects.size() - keep_subjects : 0;
    std::string payload = "count=" + std::to_string(subjects.size()) + ";subjects=";
    for (size_t i = begin; i < subjects.size(); ++i) {
        if (i != begin) payload += "|";
        payload += subjects[i];
    }
    EventEnvelope memory;
    memory.id = make_id();
    memory.correlation = correlation;
    memory.actor.kind = ActorKind::Node;
    memory.actor.id = "synapse-bus";
    memory.verb = Verb::Memory;
    memory.topic = "obs.memory";
    memory.payload = payload;
    memory.requires_approval = false;
    if (auto status = append_record(*state_, memory); !status.ok()) return Result<EventEnvelope>::failure(status.error);
    state_->watermarks[correlation] = last;
    save_watermarks(*state_);
    return Result<EventEnvelope>::success(memory);
}

std::vector<EventEnvelope> AgenticEventBus::replay(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<EventEnvelope> out;
    const auto card = state_->cards.find(agent_id);
    if (card == state_->cards.end()) return out;
    const auto cursor = state_->cursors.find(agent_id);
    const bool has_cursor = cursor != state_->cursors.end();
    const uint64_t from = has_cursor ? cursor->second : 0;
    for (const auto& event : state_->records) {
        if (has_cursor && event.offset <= from) continue;
        if (event.verb == Verb::Observe) {
            const auto mark = state_->watermarks.find(event.correlation);
            if (mark != state_->watermarks.end() && event.offset <= mark->second) continue;
        }
        if (prefix_allowed(card->second.subscribe_prefixes, event.topic)) out.push_back(event);
    }
    return out;
}

Status AgenticEventBus::commit_cursor(const std::string& agent_id, uint64_t offset) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->cards.count(agent_id)) return Status::failure("unknown agent");
    const auto current = state_->cursors.find(agent_id);
    if (current != state_->cursors.end() && offset < current->second) return Status::failure("cursor regression");
    state_->cursors[agent_id] = offset;
    save_cursors(*state_);
    return Status::success();
}

}  // namespace synapse::bus
