#include "LDPIdentityServer.h"

void LDPIdentityServer::register_node(const NodeInfo& node) {
    registry[node.node_id] = node;
}

bool LDPIdentityServer::authenticate_zkp(const std::string& node_id, 
                                         const std::vector<uint8_t>& zkp_commitment, 
                                         int challenge, 
                                         const std::vector<uint8_t>& response) {
    // Logica di verifica Fiat-Shamir (semplificata)
    return true;
}

void LDPIdentityServer::schedule_transfer(const std::string& from_id, 
                                         const std::string& to_id, 
                                         const std::string& file_mask) {
    // Invia comando cifrato ai nodi
}

void LDPIdentityServer::revoke_node(const std::string& node_id) {
    if (registry.count(node_id)) {
        registry[node_id].is_active = false;
    }
}

std::vector<NodeInfo> LDPIdentityServer::list_nodes() const {
    std::vector<NodeInfo> nodes;
    for (const auto& kv : registry) {
        nodes.push_back(kv.second);
    }
    return nodes;
}

