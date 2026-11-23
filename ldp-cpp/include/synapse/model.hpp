#pragma once

#include "bus.hpp"

#include <map>
#include <string>
#include <vector>

namespace synapse::bus {

struct LinearScorer {
    std::map<std::string, int> weights;
    int threshold = 0;
    int score(std::string_view text) const;
};

struct PolicyRule {
    std::string observe_prefix = "obs.";
    std::string payload_substring;
    std::string propose_topic = "act.transfer";
    std::string payload = "propose";
    bool requires_approval = true;
    int64_t lease_ttl_ms = 30000;
    int64_t approval_window_ms = 15 * 60 * 1000;
};

struct Proposal {
    std::string topic;
    std::string subject;
    std::string payload;
    std::string correlation;
    std::string causation;
    bool requires_approval = true;
    int64_t lease_ttl_ms = 30000;
    int64_t deadline_ms = 0;
};

class IAgentModel {
public:
    virtual ~IAgentModel() = default;
    virtual std::vector<Proposal> decide(const std::vector<EventEnvelope>& recent) const = 0;
};

class EmbeddedPolicyModel final : public IAgentModel {
public:
    EmbeddedPolicyModel(std::vector<PolicyRule> rules, LinearScorer scorer);
    std::vector<Proposal> decide(const std::vector<EventEnvelope>& recent) const override;

private:
    std::vector<PolicyRule> rules_;
    LinearScorer scorer_;
};

class EmbeddedAgent {
public:
    EmbeddedAgent(AgenticEventBus& bus, CapabilityCard card, EmbeddedPolicyModel model);
    Status tick();

private:
    AgenticEventBus& bus_;
    CapabilityCard card_;
    EmbeddedPolicyModel model_;
};

}  // namespace synapse::bus
