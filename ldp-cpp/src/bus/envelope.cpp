#include "synapse/envelope.hpp"
#include "synapse/codec.hpp"

#include <chrono>
#include <cctype>
#include <random>

namespace synapse::bus {
namespace {

bool is_token_char(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':';
}

}  // namespace

const char* actor_kind_name(ActorKind kind) {
    switch (kind) {
        case ActorKind::Human:
            return "human";
        case ActorKind::Agent:
            return "agent";
        case ActorKind::Node:
            return "node";
    }
    return "agent";
}

const char* verb_name(Verb verb) {
    switch (verb) {
        case Verb::Observe:
            return "observe";
        case Verb::Propose:
            return "propose";
        case Verb::Act:
            return "act";
        case Verb::Approve:
            return "approve";
        case Verb::Veto:
            return "veto";
        case Verb::Expire:
            return "expire";
        case Verb::Memory:
            return "memory";
    }
    return "observe";
}

Result<ActorKind> parse_actor_kind(std::string_view text) {
    if (text == "human") return Result<ActorKind>::success(ActorKind::Human);
    if (text == "agent") return Result<ActorKind>::success(ActorKind::Agent);
    if (text == "node") return Result<ActorKind>::success(ActorKind::Node);
    return Result<ActorKind>::failure("unknown actor kind");
}

Result<Verb> parse_verb(std::string_view text) {
    if (text == "observe") return Result<Verb>::success(Verb::Observe);
    if (text == "propose") return Result<Verb>::success(Verb::Propose);
    if (text == "act") return Result<Verb>::success(Verb::Act);
    if (text == "approve") return Result<Verb>::success(Verb::Approve);
    if (text == "veto") return Result<Verb>::success(Verb::Veto);
    if (text == "expire") return Result<Verb>::success(Verb::Expire);
    if (text == "memory") return Result<Verb>::success(Verb::Memory);
    return Result<Verb>::failure("unknown verb");
}

bool safe_token(std::string_view text, bool allow_empty) {
    if (text.empty()) return allow_empty;
    if (text.size() > 128) return false;
    for (unsigned char c : text) {
        if (!is_token_char(c)) return false;
    }
    return true;
}

std::string canonical_envelope(const EventEnvelope& event) {
    std::string out = "{";
    out += "\"id\":\"" + codec::json_escape(event.id) + "\",";
    out += "\"causation\":\"" + codec::json_escape(event.causation) + "\",";
    out += "\"correlation\":\"" + codec::json_escape(event.correlation) + "\",";
    out += "\"actor_kind\":\"" + std::string(actor_kind_name(event.actor.kind)) + "\",";
    out += "\"actor_id\":\"" + codec::json_escape(event.actor.id) + "\",";
    out += "\"actor_model\":\"" + codec::json_escape(event.actor.model) + "\",";
    out += "\"verb\":\"" + std::string(verb_name(event.verb)) + "\",";
    out += "\"topic\":\"" + codec::json_escape(event.topic) + "\",";
    out += "\"subject\":\"" + codec::json_escape(event.subject) + "\",";
    out += "\"payload\":\"" + codec::json_escape(event.payload) + "\",";
    out += "\"deadline_ms\":" + std::to_string(event.deadline_ms) + ",";
    out += "\"requires_approval\":";
    out += event.requires_approval ? "true" : "false";
    out += ",\"offset\":" + std::to_string(event.offset);
    out += "}";
    return out;
}

Result<EventEnvelope> parse_envelope(std::string_view json) {
    auto parsed = codec::parse_json(json);
    if (!parsed.ok()) return Result<EventEnvelope>::failure(parsed.error);
    EventEnvelope event;
    auto id = parsed.value.str("id");
    auto causation = parsed.value.str("causation");
    auto correlation = parsed.value.str("correlation");
    auto kind = parsed.value.str("actor_kind");
    auto actor_id = parsed.value.str("actor_id");
    auto model = parsed.value.str("actor_model");
    auto verb = parsed.value.str("verb");
    auto topic = parsed.value.str("topic");
    auto subject = parsed.value.str("subject");
    auto payload = parsed.value.str("payload");
    auto deadline = parsed.value.i64("deadline_ms");
    auto approval = parsed.value.boolean_at("requires_approval");
    auto offset = parsed.value.i64("offset");
    if (!id.ok() || !causation.ok() || !correlation.ok() || !kind.ok() || !actor_id.ok() || !model.ok() ||
        !verb.ok() || !topic.ok() || !subject.ok() || !payload.ok() || !deadline.ok() || !approval.ok() ||
        !offset.ok()) {
        return Result<EventEnvelope>::failure("envelope fields missing");
    }
    auto actor_kind = parse_actor_kind(kind.value);
    auto verb_value = parse_verb(verb.value);
    if (!actor_kind.ok()) return Result<EventEnvelope>::failure(actor_kind.error);
    if (!verb_value.ok()) return Result<EventEnvelope>::failure(verb_value.error);
    if (offset.value < 0) return Result<EventEnvelope>::failure("negative offset");
    event.id = std::move(id.value);
    event.causation = std::move(causation.value);
    event.correlation = std::move(correlation.value);
    event.actor.kind = actor_kind.value;
    event.actor.id = std::move(actor_id.value);
    event.actor.model = std::move(model.value);
    event.verb = verb_value.value;
    event.topic = std::move(topic.value);
    event.subject = std::move(subject.value);
    event.payload = std::move(payload.value);
    event.deadline_ms = deadline.value;
    event.requires_approval = approval.value;
    event.offset = static_cast<uint64_t>(offset.value);
    return Result<EventEnvelope>::success(std::move(event));
}

std::string make_id() {
    std::random_device device;
    std::uniform_int_distribution<int> dist(0, 15);
    static const char* digits = "0123456789abcdef";
    std::string id(32, '0');
    for (char& c : id) c = digits[dist(device)];
    return id;
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace synapse::bus
