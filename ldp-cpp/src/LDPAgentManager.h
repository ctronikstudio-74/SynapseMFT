#pragma once
#include "LDPAgentServer.h"
#include <map>
#include <mutex>
#include <memory>
#include <string>

class LDPAgentManager {
private:
    std::map<std::string, std::unique_ptr<LDPAgentServer>> agents;
    mutable std::mutex agentsMutex;
    int basePort;

public:
    LDPAgentManager(int basePort = 8888) : basePort(basePort) {}
    
    bool startAgent(const std::string& nodeId, const std::string& nodeIp);
    bool stopAgent(const std::string& nodeId);
    void stopAllAgents();
    
    std::string sendCommand(const std::string& nodeId, const std::string& commandJson, const std::string& nodeIp = "");
    
    bool isAgentRunning(const std::string& nodeId) const;
    std::vector<std::pair<std::string, std::string>> getAllAgents() const;
};

