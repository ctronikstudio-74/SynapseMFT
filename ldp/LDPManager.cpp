#include "LDPManager.h"
#include <iostream>

void LDPManager::transmitter_loop() {
    while (running) {
        // 1. Controlla se ci sono pacchetti da rispedire (Timeout)
        // 2. Invia nuovi pacchetti dal file
        // 3. Rispetta il send_interval_ms per non saturare la banda
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void LDPManager::ack_receiver_loop() {
    while (running) {
        // Riceve LDPAckPacket dal socket
        // Rimuove i pacchetti confermati da unacked_packets
        std::lock_guard<std::mutex> lock(mtx);
        // logic to clean up map...
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void LDPManager::start() {
    running = true;
    std::thread t1(&LDPManager::transmitter_loop, this);
    std::thread t2(&LDPManager::ack_receiver_loop, this);
    t1.detach();
    t2.detach();
}

void LDPManager::stop() {
    running = false;
}

