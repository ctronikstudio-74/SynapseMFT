#include "SynapseEventBus.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace SynapseMFT {

SynapseEventBusService::SynapseEventBusService() {
    if (sodium_init() < 0) {
        throw std::runtime_error("Sodium initialization failed");
    }
    randombytes_buf(m_encryptionKey, sizeof(m_encryptionKey));
}

SynapseEventBusService::~SynapseEventBusService() {
    disconnect();
}

bool SynapseEventBusService::connect(const EventBusConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_config = config;
    
    // Initialize persistence if enabled
    if (config.synapseEnablePersistence && !config.synapsePersistencePath.empty()) {
        m_persistenceStream = std::make_unique<std::ofstream>(
            config.synapsePersistencePath, std::ios::app | std::ios::binary);
        if (!m_persistenceStream->is_open()) {
            return false;
        }
    }
    
    // Initialize replication if distributed mode
    if (config.synapseUseDistributedMode) {
        // Connect to replication nodes
        // Implementation would connect to other Synapse nodes
    }
    
    m_isConnected = true;
    return true;
}

void SynapseEventBusService::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_subscribers.clear();
    m_persistenceStream.reset();
    
    // Close replication sockets
    for (int sock : m_replicationSockets) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
    m_replicationSockets.clear();
    
    m_isConnected = false;
}

bool SynapseEventBusService::publish(const std::string& topic, const std::vector<uint8_t>& data,
                                     const std::map<std::string, std::string>& metadata) {
    if (!m_isConnected) return false;
    
    try {
        // Encrypt data if encryption enabled
        std::vector<uint8_t> encryptedData = encryptData(data);
        
        // Create message envelope
        std::ostringstream envelope;
        envelope << "{\"topic\":\"" << topic << "\","
                 << "\"data\":\"" << std::hex;
        for (uint8_t b : encryptedData) {
            envelope << std::setw(2) << std::setfill('0') << (int)b;
        }
        envelope << "\",\"timestamp\":" << std::time(nullptr) << "}";
        
        std::string envelopeStr = envelope.str();
        std::vector<uint8_t> message(envelopeStr.begin(), envelopeStr.end());
        
        // Persist if enabled
        if (m_persistenceStream && m_persistenceStream->is_open()) {
            persistMessage(message);
        }
        
        // Replicate if distributed mode
        if (m_config.synapseUseDistributedMode) {
            replicateMessage(message);
        }
        
        // Deliver to local subscribers
        deliverToSubscribers(topic, encryptedData, metadata);
        
        return true;
    } catch (...) {
        return false;
    }
}

bool SynapseEventBusService::publishBatch(const std::string& topic,
                                         const std::vector<std::vector<uint8_t>>& dataBatch,
                                         const std::map<std::string, std::string>& metadata) {
    for (const auto& data : dataBatch) {
        if (!publish(topic, data, metadata)) {
            return false;
        }
    }
    return true;
}

void SynapseEventBusService::subscribe(const std::string& topic,
                                      std::function<void(const std::vector<uint8_t>&,
                                                       const std::map<std::string, std::string>&)> handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscribers[topic].push_back(handler);
}

void SynapseEventBusService::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscribers.erase(topic);
}

bool SynapseEventBusService::healthCheck() {
    return m_isConnected;
}

std::vector<uint8_t> SynapseEventBusService::encryptData(const std::vector<uint8_t>& data) {
    if (m_config.encryptionLevel == EncryptionLevel::None) {
        return data;
    }
    
    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    
    std::vector<uint8_t> ciphertext(data.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long clen;
    
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext.data(), &clen,
        data.data(), data.size(),
        nullptr, 0,
        nullptr, nonce, m_encryptionKey
    );
    
    ciphertext.resize(clen);
    
    // Prepend nonce
    std::vector<uint8_t> result(nonce, nonce + sizeof(nonce));
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    
    return result;
}

std::vector<uint8_t> SynapseEventBusService::decryptData(const std::vector<uint8_t>& encryptedData) {
    if (m_config.encryptionLevel == EncryptionLevel::None) {
        return encryptedData;
    }
    
    if (encryptedData.size() < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) {
        throw std::runtime_error("Invalid encrypted data");
    }
    
    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    std::memcpy(nonce, encryptedData.data(), sizeof(nonce));
    
    std::vector<uint8_t> ciphertext(encryptedData.begin() + sizeof(nonce), encryptedData.end());
    std::vector<uint8_t> plaintext(ciphertext.size());
    unsigned long long plen;
    
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext.data(), &plen,
        nullptr,
        ciphertext.data(), ciphertext.size(),
        nullptr, 0,
        nonce, m_encryptionKey
    ) != 0) {
        throw std::runtime_error("Decryption failed");
    }
    
    plaintext.resize(plen);
    return plaintext;
}

void SynapseEventBusService::deliverToSubscribers(const std::string& topic,
                                                  const std::vector<uint8_t>& data,
                                                  const std::map<std::string, std::string>& metadata) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_subscribers.find(topic) == m_subscribers.end()) {
        return;
    }
    
    auto handlers = m_subscribers[topic];
    for (const auto& handler : handlers) {
        try {
            handler(data, metadata);
        } catch (...) {
            // Log error but continue
        }
    }
}

void SynapseEventBusService::persistMessage(const std::vector<uint8_t>& message) {
    if (m_persistenceStream && m_persistenceStream->is_open()) {
        m_persistenceStream->write(reinterpret_cast<const char*>(message.data()), message.size());
        m_persistenceStream->write("\n", 1);
        m_persistenceStream->flush();
    }
}

void SynapseEventBusService::replicateMessage(const std::vector<uint8_t>& message) {
    // Replicate to other nodes in distributed mode
    // Implementation would send to replication sockets
    for (int sock : m_replicationSockets) {
        // Send message to replication node
        // send(sock, message.data(), message.size(), 0);
    }
}

std::unique_ptr<IEventBusService> EventBusFactory::create(EventBusType type) {
    switch (type) {
        case EventBusType::SynapseEventBus:
            return std::make_unique<SynapseEventBusService>();
        // Add other implementations
        default:
            throw std::runtime_error("Event bus type not implemented");
    }
}

} // namespace SynapseMFT

