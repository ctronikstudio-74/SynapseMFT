#include "LDPAgentServer.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>
// Hash per generare ID file - usa std::hash (OpenSSL opzionale)
#include <functional>
#include <sstream>
#include <iomanip>
#ifdef USE_OPENSSL
#include <openssl/sha.h>
#endif

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

// Parser JSON semplice (alternativa a jsoncpp)
class SimpleJson {
public:
    static std::string createResponse(const std::map<std::string, std::string>& data) {
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& pair : data) {
            if (!first) oss << ",";
            oss << "\"" << pair.first << "\":\"" << pair.second << "\"";
            first = false;
        }
        oss << "}";
        return oss.str();
    }
    
    static std::map<std::string, std::string> parse(const std::string& json) {
        std::map<std::string, std::string> result;
        // Parser semplificato - in produzione usare jsoncpp
        size_t pos = json.find("\"type\"");
        if (pos != std::string::npos) {
            size_t start = json.find('"', pos + 6) + 1;
            size_t end = json.find('"', start);
            if (end != std::string::npos) {
                result["type"] = json.substr(start, end - start);
            }
        }
        return result;
    }
};

LDPAgentServer::LDPAgentServer(const std::string& id, const std::string& ip, int basePort)
    : nodeId(id), nodeIp(ip), port(basePort + std::stoi(id)) {
    
    basePath = "nodes/node-" + nodeId;
    try {
        fs::create_directories(basePath);
        std::cout << "[Agent " << nodeId << "] Inizializzato su " << nodeIp << ":" << port 
                  << ", basePath: " << basePath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Agent " << nodeId << "] Errore creazione directory: " << e.what() << std::endl;
    }
}

LDPAgentServer::~LDPAgentServer() {
    stop();
}

bool LDPAgentServer::start() {
    if (running) {
        std::cout << "[Agent " << nodeId << "] Già in esecuzione" << std::endl;
        return true;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[Agent " << nodeId << "] WSAStartup failed" << std::endl;
        return false;
    }
#endif

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "[Agent " << nodeId << "] Errore creazione socket" << std::endl;
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
    serverAddr.sin_port = htons(port);
    
    if (nodeIp == "0.0.0.0" || nodeIp.empty()) {
        serverAddr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, nodeIp.c_str(), &serverAddr.sin_addr);
    }

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[Agent " << nodeId << "] Errore bind su " << nodeIp << ":" << port << std::endl;
#ifdef _WIN32
        closesocket(serverSocket);
        WSACleanup();
#else
        close(serverSocket);
#endif
        return false;
    }

    if (listen(serverSocket, 5) < 0) {
        std::cerr << "[Agent " << nodeId << "] Errore listen" << std::endl;
#ifdef _WIN32
        closesocket(serverSocket);
        WSACleanup();
#else
        close(serverSocket);
#endif
        return false;
    }

    running = true;
    serverThread = std::thread(&LDPAgentServer::serverLoop, this);
    
    std::cout << "[Agent " << nodeId << "] Server in ascolto su " << nodeIp << ":" << port << std::endl;
    return true;
}

void LDPAgentServer::stop() {
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
    
    std::cout << "[Agent " << nodeId << "] Server fermato" << std::endl;
}

void LDPAgentServer::serverLoop() {
    while (running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (running) {
                std::cerr << "[Agent " << nodeId << "] Errore accept" << std::endl;
            }
            continue;
        }

        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
        std::cout << "[Agent " << nodeId << "] Connessione ricevuta da " << clientIp << ":" 
                  << ntohs(clientAddr.sin_port) << std::endl;

        std::thread clientThread(&LDPAgentServer::handleClient, this, clientSocket);
        clientThread.detach();
    }
}

void LDPAgentServer::handleClient(int clientSocket) {
    char buffer[4096];
    std::string messageBuffer;
    
    while (running) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) break;
        
        buffer[bytesReceived] = '\0';
        messageBuffer += buffer;
        
        size_t pos;
        while ((pos = messageBuffer.find('\n')) != std::string::npos) {
            std::string line = messageBuffer.substr(0, pos);
            messageBuffer = messageBuffer.substr(pos + 1);
            
            if (!line.empty()) {
                std::string response = handleCommand(line);
                send(clientSocket, response.c_str(), response.length(), 0);
                send(clientSocket, "\n", 1, 0);
            }
        }
    }
    
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
    
    std::cout << "[Agent " << nodeId << "] Connessione chiusa" << std::endl;
}

std::string LDPAgentServer::handleCommand(const std::string& commandJson) {
    // Parser JSON semplificato - in produzione usare jsoncpp
    std::ostringstream response;
    
    if (commandJson.find("\"type\":\"PING\"") != std::string::npos) {
        response << "{\"type\":\"PONG\",\"nodeId\":\"" << nodeId << "\",\"status\":\"ONLINE\"}";
    }
    else if (commandJson.find("\"type\":\"LIST_FILES\"") != std::string::npos) {
        std::string path = "/";
        size_t pathPos = commandJson.find("\"path\":\"");
        if (pathPos != std::string::npos) {
            size_t start = pathPos + 8;
            size_t end = commandJson.find('"', start);
            if (end != std::string::npos) {
                path = commandJson.substr(start, end - start);
            }
        }
        response << listFiles(path);
    }
    else if (commandJson.find("\"type\":\"CREATE_FOLDER\"") != std::string::npos) {
        std::string name, parentPath = "/";
        // Estrai name e path dal JSON
        response << createFolder(name, parentPath);
    }
    else if (commandJson.find("\"type\":\"GET_STATUS\"") != std::string::npos) {
        response << "{\"type\":\"STATUS\",\"nodeId\":\"" << nodeId 
                 << "\",\"status\":\"ONLINE\",\"ip\":\"" << nodeIp << "\",\"port\":" << port << "}";
    }
    else {
        response << "{\"error\":\"Unknown command\"}";
    }
    
    return response.str();
}

std::string LDPAgentServer::listFiles(const std::string& folderPath) {
    std::ostringstream response;
    response << "{\"type\":\"FILES_LIST\",\"files\":[";
    
    try {
        std::string fullPath = resolvePath(folderPath);
        bool first = true;
        
        for (const auto& entry : fs::directory_iterator(fullPath)) {
            if (!first) response << ",";
            
            std::string entryPath = entry.path().string();
            std::string relativePath = fs::relative(entryPath, basePath).string();
            std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
            
            auto status = entry.status();
            bool isDir = fs::is_directory(status);
            
            response << "{\"id\":\"" << generateFileId(relativePath) << "\","
                     << "\"name\":\"" << entry.path().filename().string() << "\","
                     << "\"type\":\"" << (isDir ? "folder" : "file") << "\","
                     << "\"path\":\"/" << relativePath << "\"";
            
            if (!isDir) {
                response << ",\"size\":" << fs::file_size(entryPath);
            }
            
            response << "}";
            first = false;
        }
    } catch (const std::exception& e) {
        return "{\"error\":\"Cannot list files: " + std::string(e.what()) + "\"}";
    }
    
    response << "]}";
    return response.str();
}

std::string LDPAgentServer::createFolder(const std::string& name, const std::string& parentPath) {
    try {
        std::string fullPath = resolvePath(parentPath == "/" ? name : parentPath + "/" + name);
        fs::create_directories(fullPath);
        std::string relativePath = fs::relative(fullPath, basePath).string();
        std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
        return "{\"type\":\"FOLDER_CREATED\",\"path\":\"/" + relativePath + "\"}";
    } catch (const std::exception& e) {
        return "{\"error\":\"Cannot create folder: " + std::string(e.what()) + "\"}";
    }
}

std::string LDPAgentServer::resolvePath(const std::string& relativePath) {
    if (relativePath == "/") {
        return basePath;
    }
    std::string cleanPath = relativePath;
    if (cleanPath.front() == '/') {
        cleanPath = cleanPath.substr(1);
    }
    return (fs::path(basePath) / cleanPath).string();
}

std::string LDPAgentServer::generateFileId(const std::string& path) {
    // Usa hash semplice se OpenSSL non disponibile
    std::hash<std::string> hasher;
    size_t hashValue = hasher(path);
    std::ostringstream oss;
    oss << std::hex << hashValue;
    return oss.str();
}

std::string LDPAgentServer::uploadFile(const std::string& name, const std::string& folderPath, const std::string& data) {
    // Implementazione upload file
    return "{\"type\":\"FILE_UPLOADED\"}";
}

std::string LDPAgentServer::downloadFile(const std::string& filePath) {
    // Implementazione download file
    return "{\"type\":\"FILE_DATA\"}";
}

std::string LDPAgentServer::transferFile(const std::string& fromPath, const std::string& toNodeId, const std::string& toPath, const std::string& decryptionKey) {
    // Implementazione trasferimento file con LDP
    return "{\"type\":\"TRANSFER_STARTED\"}";
}

std::string LDPAgentServer::decryptFile(const std::string& filePath, const std::string& key) {
    // Implementazione decriptazione file
    return "{\"type\":\"FILE_DECRYPTED\",\"success\":true}";
}

