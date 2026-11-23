#pragma once
#include "LDPAgentManager.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>

class LDPApiServer {
private:
    LDPAgentManager agentManager;
    int apiPort;
    std::atomic<bool> running{false};
    std::thread serverThread;
    int serverSocket;
    
    void serverLoop();
    void handleRequest(int clientSocket);
    std::string handleHttpRequest(const std::string& request);
    std::string parseHttpRequest(const std::string& request, std::string& method, std::string& path, std::string& body);
    std::string createHttpResponse(int statusCode, const std::string& contentType, const std::string& body);
    std::string jsonResponse(const std::string& json);
    std::string errorResponse(const std::string& error);
    
public:
    LDPApiServer(int port = 3002);
    ~LDPApiServer();
    
    bool start();
    void stop();
    bool isRunning() const { return running; }
};

