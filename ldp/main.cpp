#include "LDPFileTransmitter.h"
#include "LDPReceiver.h"
#include "LDPCrypto.h"
#include "LDPSocket.h"
#include "LDPAck.h"
#include "LDPCongestionControl.h"
#include "LDPManager.h"
#include "LDPControl.h"
#include "LDPRemoteCommands.h"
#include <iostream>
#include <sodium.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: ldp <send|receive> [file] [ip] [port]" << std::endl;
        return 1;
    }
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed" << std::endl;
        return 1;
    }
    std::string mode = argv[1];
    if (mode == "send" && argc == 5) {
        std::string file = argv[2];
        std::string ip = argv[3];
        int port = std::stoi(argv[4]);
        LDPFileTransmitter transmitter;
        transmitter.send_file(file, ip, port);
        std::cout << "File sent." << std::endl;
    } else if (mode == "receive" && argc == 4) {
        std::string out_file = argv[2];
        int port = std::stoi(argv[3]);
        LDPReceiver receiver(out_file);
        // Qui andrebbe la logica di ricezione pacchetti via socket
        std::cout << "Ready to receive on port " << port << std::endl;
    } else if (mode == "ls" && argc == 4) {
        std::string ip = argv[2];
        int port = std::stoi(argv[3]);
        LDPRemoteCommands remote;
        remote.send_ls_request(ip, port);
        std::cout << "LS command sent." << std::endl;
    } else if (mode == "mkdir" && argc == 5) {
        std::string ip = argv[2];
        int port = std::stoi(argv[3]);
        std::string dirname = argv[4];
        LDPRemoteCommands remote;
        remote.send_mkdir_request(ip, port, dirname);
        std::cout << "MKDIR command sent." << std::endl;
    } else if (mode == "rm" && argc == 5) {
        std::string ip = argv[2];
        int port = std::stoi(argv[3]);
        std::string filename = argv[4];
        LDPRemoteCommands remote;
        remote.send_rm_request(ip, port, filename);
        std::cout << "RM command sent." << std::endl;
    } else if (mode == "server" && argc == 3) {
        int port = std::stoi(argv[2]);
        LDPSocket socket;
        std::cout << "LDP server listening on port " << port << std::endl;
        while (true) {
            // Ricezione pacchetto (semplificato, da completare con logica reale)
            std::vector<uint8_t> buffer(4096);
            // Simulazione: ricevi da socket
            // int received = socket.receive_packet(buffer, port);
            // if (received > 0) {
            //     uint8_t nonce[24];
            //     // Estrai nonce dal pacchetto
            //     LDPRemoteCommands remote;
            //     remote.handle_command_packet(buffer, nonce);
            // }
        }
    } else {
        std::cout << "Invalid arguments." << std::endl;
        return 1;
    }
    return 0;
}
