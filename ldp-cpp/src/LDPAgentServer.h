#pragma once
#include "LDPFileTransmitter.h"
#include "LDPFileOps.h"
#include "LDPCrypto.h"
#include "LDPTransportTCP.h"
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
// Parser JSON semplice implementato inline

namespace fs = std::filesystem;

struct VirtualFile {
    std::string id;
    std::string name;
    std::string type; // "file" or "folder"
    std::string path;
    size_t size;
    std::string modified;
    bool encrypted;
};

class LDPAgentServer {
private:
    std::string nodeId;
    std::string nodeIp;
    int port;
    std::string basePath;
    std::atomic<bool> running{false};
    std::thread serverThread;
    std::mutex filesMutex;
    std::map<std::string, VirtualFile> virtualFileSystem;
    
    int serverSocket;
    
    void serverLoop();
    void handleClient(int clientSocket);
    std::string handleCommand(const std::string& commandJson);
    std::string listFiles(const std::string& folderPath);
    std::string createFolder(const std::string& name, const std::string& parentPath);
    std::string uploadFile(const std::string& name, const std::string& folderPath, const std::string& data);
    std::string downloadFile(const std::string& filePath);
    std::string transferFile(const std::string& fromPath, const std::string& toNodeId, const std::string& toPath, const std::string& decryptionKey);
    std::string decryptFile(const std::string& filePath, const std::string& key);
    std::string resolvePath(const std::string& relativePath);
    std::string generateFileId(const std::string& path);
    
public:
    LDPAgentServer(const std::string& id, const std::string& ip, int basePort = 8888);
    ~LDPAgentServer();
    
    bool start();
    void stop();
    bool isRunning() const { return running; }
    int getPort() const { return port; }
    std::string getNodeId() const { return nodeId; }
    std::string getNodeIp() const { return nodeIp; }
};

