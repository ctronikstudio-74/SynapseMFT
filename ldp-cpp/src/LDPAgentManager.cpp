#include "LDPAgentManager.h"
#include "LDPTransportTCP.h"
#include <iostream>
#include <thread>
#include <chrono>

bool LDPAgentManager::startAgent(const std::string& nodeId, const std::string& nodeIp) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    if (agents.find(nodeId) != agents.end()) {
        std::cout << "[Manager] Agente " << nodeId << " già attivo" << std::endl;
        return true;
    }
    
    auto agent = std::make_unique<LDPAgentServer>(nodeId, nodeIp, basePort);
    if (agent->start()) {
        agents[nodeId] = std::move(agent);
        std::cout << "[Manager] Agente " << nodeId << " avviato su " << nodeIp << std::endl;
        return true;
    }
    
    return false;
}

bool LDPAgentManager::stopAgent(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    auto it = agents.find(nodeId);
    if (it != agents.end()) {
        it->second->stop();
        agents.erase(it);
        std::cout << "[Manager] Agente " << nodeId << " fermato" << std::endl;
        return true;
    }
    
    return false;
}

void LDPAgentManager::stopAllAgents() {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    for (auto& pair : agents) {
        pair.second->stop();
    }
    agents.clear();
    
    std::cout << "[Manager] Tutti gli agenti fermati" << std::endl;
}

std::string LDPAgentManager::sendCommand(const std::string& nodeId, const std::string& commandJson, const std::string& nodeIp) {
    std::lock_guard<std::mutex> lock(agentsMutex);
    
    auto it = agents.find(nodeId);
    if (it == agents.end()) {
        // Prova ad avviare l'agente se non esiste
        if (!nodeIp.empty()) {
            if (startAgent(nodeId, nodeIp)) {
                it = agents.find(nodeId);
            }
        }
        if (it == agents.end()) {
            return "{\"error\":\"Agent not found\"}";
        }
    }
    
    LDPAgentServer* agent = it->second.get();
    std::string ip = agent->getNodeIp();
    int port = agent->getPort();
    
    // Connetti all'agente e invia comando
    LDPTransportTCP transport;
    if (!transport.connect_to(ip, port)) {
        return "{\"error\":\"Cannot connect to agent\"}";
    }
    
    std::string command = commandJson + "\n";
    std::vector<uint8_t> cmdData(command.begin(), command.end());
    transport.send_data(cmdData);
    
    // Ricevi risposta
    std::vector<uint8_t> buffer(4096);
    int bytesReceived = transport.receive_data(buffer);
    if (bytesReceived > 0) {
        return std::string(buffer.begin(), buffer.begin() + bytesReceived);
    }
    
    return "{\"error\":\"No response from agent\"}";
}

bool LDPAgentManager::isAgentRunning(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    return agents.find(nodeId) != agents.end() && agents.at(nodeId)->isRunning();
}

std::vector<std::pair<std::string, std::string>> LDPAgentManager::getAllAgents() const {
    std::lock_guard<std::mutex> lock(agentsMutex);
    std::vector<std::pair<std::string, std::string>> result;
    
    for (const auto& pair : agents) {
        result.push_back({pair.first, pair.second->getNodeIp()});
    }
    
    return result;
}

