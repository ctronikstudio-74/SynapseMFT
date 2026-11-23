#pragma once
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <cstdint>
#include <chrono>
#include <atomic>

class LDPManager {
    std::mutex mtx;
    std::map<uint32_t, std::vector<uint8_t>> unacked_packets;
    std::atomic<bool> running{false};
public:
    void transmitter_loop();
    void ack_receiver_loop();
    void start();
    void stop();
};
