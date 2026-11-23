#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
// Simulazione di proxy UDP trasparente
class LDPGatewayDMZ {
    std::set<std::string> allowed_ips;
    std::map<std::string, std::pair<std::string, int>> upstream_nodes;
    std::atomic<bool> running{true};
public:
    void load_config(const std::string& config_path) {
        // Carica YAML, popola allowed_ips e upstream_nodes
    }
    void start_proxy(int listen_port) {
        std::cout << "Gateway DMZ listening on port " << listen_port << std::endl;
        while (running) {
            // Ricevi pacchetto UDP
            // Verifica IP mittente
            // Inoltra a nodo interno se IP in whitelist
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void stop() { running = false; }
};
