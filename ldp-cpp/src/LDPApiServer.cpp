#include "LDPApiServer.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

LDPApiServer::LDPApiServer(int port) : apiPort(port) {
}

LDPApiServer::~LDPApiServer() {
    stop();
}

bool LDPApiServer::start() {
    if (running) {
        return true;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[API Server] WSAStartup failed" << std::endl;
        return false;
    }
#endif

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "[API Server] Errore creazione socket" << std::endl;
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(apiPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[API Server] Errore bind su porta " << apiPort << std::endl;
#ifdef _WIN32
        closesocket(serverSocket);
        WSACleanup();
#else
        close(serverSocket);
#endif
        return false;
    }

    if (listen(serverSocket, 10) < 0) {
        std::cerr << "[API Server] Errore listen" << std::endl;
#ifdef _WIN32
        closesocket(serverSocket);
        WSACleanup();
#else
        close(serverSocket);
#endif
        return false;
    }

    running = true;
    serverThread = std::thread(&LDPApiServer::serverLoop, this);
    
    std::cout << "[API Server] Server in ascolto su porta " << apiPort << std::endl;
    return true;
}

void LDPApiServer::stop() {
    if (!running) return;
    
    running = false;
    
#ifdef _WIN32
    closesocket(serverSocket);
    WSACleanup();
#else
    close(serverSocket);
#endif
    
    if (serverThread.joinable()) {
        serverThread.join();
    }
    
    agentManager.stopAllAgents();
    std::cout << "[API Server] Server fermato" << std::endl;
}

void LDPApiServer::serverLoop() {
    while (running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (running) {
                std::cerr << "[API Server] Errore accept" << std::endl;
            }
            continue;
        }

        std::thread clientThread(&LDPApiServer::handleRequest, this, clientSocket);
        clientThread.detach();
    }
}

void LDPApiServer::handleRequest(int clientSocket) {
    char buffer[8192];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesReceived <= 0) {
#ifdef _WIN32
        closesocket(clientSocket);
#else
        close(clientSocket);
#endif
        return;
    }
    
    buffer[bytesReceived] = '\0';
    std::string request(buffer);
    
    std::string method, path, body;
    parseHttpRequest(request, method, path, body);
    
    std::string response = handleHttpRequest(method + " " + path + " " + body);
    
    send(clientSocket, response.c_str(), response.length(), 0);
    
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}

std::string LDPApiServer::parseHttpRequest(const std::string& request, std::string& method, std::string& path, std::string& body) {
    std::istringstream iss(request);
    std::string line;
    
    // Prima riga: METHOD PATH HTTP/VERSION
    if (std::getline(iss, line)) {
        std::istringstream firstLine(line);
        firstLine >> method >> path;
    }
    
    // Cerca body (dopo doppio CRLF)
    size_t bodyPos = request.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        body = request.substr(bodyPos + 4);
    }
    
    return "";
}

std::string LDPApiServer::handleHttpRequest(const std::string& requestLine) {
    std::istringstream iss(requestLine);
    std::string method, path, body;
    iss >> method >> path;
    std::getline(iss, body);
    
    // CORS headers
    std::string corsHeaders = "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type\r\n";
    
    if (method == "OPTIONS") {
        return createHttpResponse(200, "application/json", "") + corsHeaders;
    }
    
    // POST /api/agents/start
    if (method == "POST" && path.find("/api/agents/start") != std::string::npos) {
        // Parse JSON body per nodeId e nodeIp
        std::string nodeId, nodeIp;
        size_t idPos = body.find("\"nodeId\":\"");
        if (idPos != std::string::npos) {
            size_t start = idPos + 10;
            size_t end = body.find('"', start);
            if (end != std::string::npos) {
                nodeId = body.substr(start, end - start);
            }
        }
        size_t ipPos = body.find("\"nodeIp\":\"");
        if (ipPos != std::string::npos) {
            size_t start = ipPos + 10;
            size_t end = body.find('"', start);
            if (end != std::string::npos) {
                nodeIp = body.substr(start, end - start);
            }
        }
        
        if (agentManager.startAgent(nodeId, nodeIp)) {
            return jsonResponse("{\"success\":true,\"message\":\"Agent " + nodeId + " started\"}") + corsHeaders;
        } else {
            return errorResponse("Failed to start agent") + corsHeaders;
        }
    }
    
    // POST /api/agents/stop
    if (method == "POST" && path.find("/api/agents/stop") != std::string::npos) {
        std::string nodeId;
        size_t idPos = body.find("\"nodeId\":\"");
        if (idPos != std::string::npos) {
            size_t start = idPos + 10;
            size_t end = body.find('"', start);
            if (end != std::string::npos) {
                nodeId = body.substr(start, end - start);
            }
        }
        
        if (agentManager.stopAgent(nodeId)) {
            return jsonResponse("{\"success\":true,\"message\":\"Agent " + nodeId + " stopped\"}") + corsHeaders;
        } else {
            return errorResponse("Failed to stop agent") + corsHeaders;
        }
    }
    
    // GET /api/agents
    if (method == "GET" && path.find("/api/agents") != std::string::npos) {
        auto agents = agentManager.getAllAgents();
        std::ostringstream json;
        json << "{\"agents\":[";
        bool first = true;
        for (const auto& pair : agents) {
            if (!first) json << ",";
            json << "{\"nodeId\":\"" << pair.first << "\",\"ip\":\"" << pair.second << "\"}";
            first = false;
        }
        json << "]}";
        return jsonResponse(json.str()) + corsHeaders;
    }
    
    // POST /api/agent/:nodeId
    std::regex agentRegex("/api/agent/([^/]+)");
    std::smatch match;
    if (method == "POST" && std::regex_search(path, match, agentRegex)) {
        std::string nodeId = match[1].str();
        
        // Estrai comando dal body
        std::string commandJson;
        size_t cmdPos = body.find("\"command\":");
        if (cmdPos != std::string::npos) {
            // Estrai JSON del comando
            commandJson = body.substr(cmdPos + 10);
            // Rimuovi eventuali caratteri finali
            size_t endPos = commandJson.find_last_of('}');
            if (endPos != std::string::npos) {
                commandJson = commandJson.substr(0, endPos + 1);
            }
        }
        
        std::string nodeIp;
        size_t ipPos = body.find("\"ip\":\"");
        if (ipPos != std::string::npos) {
            size_t start = ipPos + 6;
            size_t end = body.find('"', start);
            if (end != std::string::npos) {
                nodeIp = body.substr(start, end - start);
            }
        }
        
        std::string response = agentManager.sendCommand(nodeId, commandJson, nodeIp);
        return jsonResponse(response) + corsHeaders;
    }
    
    return errorResponse("Not found") + corsHeaders;
}

std::string LDPApiServer::createHttpResponse(int statusCode, const std::string& contentType, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << (statusCode == 200 ? "OK" : "Error") << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "\r\n";
    response << body;
    return response.str();
}

std::string LDPApiServer::jsonResponse(const std::string& json) {
    return createHttpResponse(200, "application/json", json);
}

std::string LDPApiServer::errorResponse(const std::string& error) {
    return createHttpResponse(500, "application/json", "{\"error\":\"" + error + "\"}");
}

