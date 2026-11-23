#include "LDPFileTransmitter.h"
#include "LDPReceiver.h"
#include <iostream>

int main() {
    // Test di trasmissione file
    LDPFileTransmitter transmitter;
    transmitter.send_file("testfile.txt", "127.0.0.1", 9000);
    // Test di ricezione file
    LDPReceiver receiver("received_testfile.txt");
    // Simulazione ricezione pacchetti
    // receiver.on_packet_received(seq_id, data);
    std::cout << "Test LDP completato." << std::endl;
    return 0;
}
