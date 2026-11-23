#pragma once

#include "result.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace synapse::bus {

enum class ActorKind { Human, Agent, Node };
enum class Verb { Observe, Propose, Act, Approve, Veto, Expire, Memory };

struct Actor {
    ActorKind kind = ActorKind::Agent;
    std::string id;
    std::string model;
};

struct EventEnvelope {
    std::string id;
    std::string causation;
    std::string correlation;
    Actor actor;
    Verb verb = Verb::Observe;
    std::string topic;
    std::string subject;
    std::string payload;
    int64_t deadline_ms = 0;
    bool requires_approval = true;
    uint64_t offset = 0;
};

const char* actor_kind_name(ActorKind kind);
const char* verb_name(Verb verb);
Result<ActorKind> parse_actor_kind(std::string_view text);
Result<Verb> parse_verb(std::string_view text);

bool safe_token(std::string_view text, bool allow_empty);
std::string canonical_envelope(const EventEnvelope& event);
Result<EventEnvelope> parse_envelope(std::string_view json);
std::string make_id();
int64_t now_ms();

}  // namespace synapse::bus
