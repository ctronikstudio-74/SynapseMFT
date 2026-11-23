#include "LDPApiServer.h"
#include <iostream>
#include <signal.h>
#include <csignal>

LDPApiServer* g_apiServer = nullptr;

void signalHandler(int signal) {
    if (g_apiServer) {
        std::cout << "\n[API Server] Arresto in corso..." << std::endl;
        g_apiServer->stop();
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    int port = 3002;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    LDPApiServer apiServer(port);
    g_apiServer = &apiServer;
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    if (!apiServer.start()) {
        std::cerr << "Errore avvio API Server" << std::endl;
        return 1;
    }
    
    std::cout << "API Server LDP in esecuzione su porta " << port << std::endl;
    std::cout << "Premi CTRL+C per fermare" << std::endl;
    
    while (apiServer.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}

