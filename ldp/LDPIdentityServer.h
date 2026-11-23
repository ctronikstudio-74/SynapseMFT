#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstdint>

struct NodeInfo {
    std::string node_id;
    std::string ip_address;
    std::vector<uint8_t> public_key;
    bool is_active;
};

class LDPIdentityServer {
    std::map<std::string, NodeInfo> registry;
public:
    void register_node(const NodeInfo& node);
    bool authenticate_zkp(const std::string& node_id, const std::vector<uint8_t>& zkp_commitment, 
                          int challenge, const std::vector<uint8_t>& response);
    void schedule_transfer(const std::string& from_id, const std::string& to_id, const std::string& file_mask);
    void revoke_node(const std::string& node_id);
    std::vector<NodeInfo> list_nodes() const;
};
