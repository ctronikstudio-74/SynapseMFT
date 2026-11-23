#include "synapse/model.hpp"

#include <cctype>

namespace synapse::bus {
namespace {

std::string lower_token(std::string_view text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::vector<std::string> tokens(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

}  // namespace

int LinearScorer::score(std::string_view text) const {
    if (weights.empty()) return threshold;
    int total = 0;
    for (const auto& token : tokens(text)) {
        const auto it = weights.find(token);
        if (it != weights.end()) total += it->second;
    }
    return total;
}

EmbeddedPolicyModel::EmbeddedPolicyModel(std::vector<PolicyRule> rules, LinearScorer scorer)
    : rules_(std::move(rules)), scorer_(std::move(scorer)) {}

std::vector<Proposal> EmbeddedPolicyModel::decide(const std::vector<EventEnvelope>& recent) const {
    std::vector<Proposal> proposals;
    for (const auto& event : recent) {
        if (event.verb != Verb::Observe && event.verb != Verb::Memory) continue;
        for (const auto& rule : rules_) {
            if (event.topic.rfind(rule.observe_prefix, 0) != 0) continue;
            if (!rule.payload_substring.empty() && event.payload.find(rule.payload_substring) == std::string::npos &&
                event.subject.find(rule.payload_substring) == std::string::npos) {
                continue;
            }
            const int value = scorer_.score(event.payload + " " + event.subject);
            if (value < scorer_.threshold) continue;
            Proposal proposal;
            proposal.topic = rule.propose_topic;
            proposal.subject = event.subject.empty() ? lower_token(event.payload) : event.subject;
            proposal.payload = rule.payload;
            proposal.correlation = event.correlation.empty() ? event.id : event.correlation;
            proposal.causation = event.id;
            proposal.requires_approval = rule.requires_approval;
            proposal.lease_ttl_ms = rule.lease_ttl_ms;
            proposal.deadline_ms = rule.requires_approval ? now_ms() + rule.approval_window_ms : 0;
            proposals.push_back(std::move(proposal));
        }
    }
    return proposals;
}

EmbeddedAgent::EmbeddedAgent(AgenticEventBus& bus, CapabilityCard card, EmbeddedPolicyModel model)
    : bus_(bus), card_(std::move(card)), model_(std::move(model)) {}

Status EmbeddedAgent::tick() {
    const auto recent = bus_.replay(card_.id);
    uint64_t last = 0;
    for (const auto& event : recent) last = event.offset;
    for (const auto& proposal : model_.decide(recent)) {
        if (proposal.subject.empty() || !safe_token(proposal.subject, false)) continue;
        auto claimed = bus_.claim(proposal.subject, card_.id, proposal.lease_ttl_ms);
        if (!claimed.ok()) continue;
        EventEnvelope event;
        event.actor.kind = ActorKind::Agent;
        event.actor.id = card_.id;
        event.actor.model = "embedded-linear-v1";
        event.verb = Verb::Propose;
        event.topic = proposal.topic;
        event.subject = proposal.subject;
        event.payload = proposal.payload;
        event.correlation = proposal.correlation;
        event.causation = proposal.causation;
        event.requires_approval = proposal.requires_approval;
        event.deadline_ms = proposal.deadline_ms;
        auto published = bus_.publish(event);
        if (!published.ok()) {
            bus_.release(proposal.subject, card_.id);
            return published;
        }
    }
    if (last != 0 || !recent.empty()) {
        if (!recent.empty()) return bus_.commit_cursor(card_.id, recent.back().offset);
    }
    return Status::success();
}

}  // namespace synapse::bus
