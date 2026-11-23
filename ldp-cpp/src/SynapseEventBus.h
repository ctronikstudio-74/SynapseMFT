#pragma once
// Legacy in-process bus. The supported agentic bus is synapse::bus::AgenticEventBus
// in include/synapse/bus.hpp. This type keeps the old config enums and is not the
// control plane described in .claude/specs/agentic-event-bus.
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <fstream>
#include <sodium.h>

namespace SynapseMFT {

enum class EventBusType {
    Socket,
    SynapseEventBus,
    ApacheKafka,
    IBM_MQSeries,
    AWS_EventBridge,
    Azure_EventGrid,
    RabbitMQ,
    Redis_Streams
};

enum class EncryptionLevel {
    None,
    Transport,
    EndToEnd,
    PostQuantum
};

struct EventBusConfig {
    std::string name;
    EventBusType type = EventBusType::Socket;
    bool isEnabled = true;
    EncryptionLevel encryptionLevel = EncryptionLevel::EndToEnd;
    bool usePostQuantumCrypto = false;
    std::string encryptionKeyId;
    
    // Performance
    int maxConcurrentTransfers = 100;
    int batchSize = 1000;
    int retryAttempts = 3;
    int retryDelayMs = 1000;
    
    // Connection
    std::string endpoint;
    int port = 0;
    std::string topic;
    std::string connectionString;
    
    // Synapse specific
    bool synapseUseDistributedMode = false;
    int synapseReplicationFactor = 3;
    bool synapseEnablePersistence = false;
    std::string synapsePersistencePath;
    
    // Kafka
    std::string kafkaBootstrapServers;
    std::string kafkaConsumerGroup;
    bool kafkaEnableAutoCommit = true;
    
    // MQSeries
    std::string mqSeriesQueueManager;
    std::string mqSeriesChannel;
    
    // AWS
    std::string awsEventBusName;
    std::string awsAccountId;
    std::string awsRegion;
    
    // Azure
    std::string azureTopicEndpoint;
    std::string azureAccessKey;
};

class IEventBusService {
public:
    virtual ~IEventBusService() = default;
    virtual bool connect(const EventBusConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool publish(const std::string& topic, const std::vector<uint8_t>& data, 
                        const std::map<std::string, std::string>& metadata = {}) = 0;
    virtual bool publishBatch(const std::string& topic, 
                             const std::vector<std::vector<uint8_t>>& dataBatch,
                             const std::map<std::string, std::string>& metadata = {}) = 0;
    virtual void subscribe(const std::string& topic, 
                          std::function<void(const std::vector<uint8_t>&, 
                                           const std::map<std::string, std::string>&)> handler) = 0;
    virtual void unsubscribe(const std::string& topic) = 0;
    virtual bool healthCheck() = 0;
    virtual EventBusType getType() const = 0;
};

class SynapseEventBusService : public IEventBusService {
private:
    EventBusConfig m_config;
    std::atomic<bool> m_isConnected{false};
    std::mutex m_mutex;
    
    // Subscribers
    std::map<std::string, std::vector<std::function<void(const std::vector<uint8_t>&, 
                                                         const std::map<std::string, std::string>&)>>> m_subscribers;
    
    // Persistence
    std::unique_ptr<std::ofstream> m_persistenceStream;
    
    // Replication
    std::vector<int> m_replicationSockets;
    
    // Encryption
    uint8_t m_encryptionKey[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    
    std::vector<uint8_t> encryptData(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decryptData(const std::vector<uint8_t>& encryptedData);
    void deliverToSubscribers(const std::string& topic, const std::vector<uint8_t>& data,
                             const std::map<std::string, std::string>& metadata);
    void persistMessage(const std::vector<uint8_t>& message);
    void replicateMessage(const std::vector<uint8_t>& message);
    
public:
    SynapseEventBusService();
    ~SynapseEventBusService();
    
    bool connect(const EventBusConfig& config) override;
    void disconnect() override;
    bool isConnected() const override { return m_isConnected; }
    bool publish(const std::string& topic, const std::vector<uint8_t>& data,
                const std::map<std::string, std::string>& metadata = {}) override;
    bool publishBatch(const std::string& topic,
                     const std::vector<std::vector<uint8_t>>& dataBatch,
                     const std::map<std::string, std::string>& metadata = {}) override;
    void subscribe(const std::string& topic,
                  std::function<void(const std::vector<uint8_t>&,
                                   const std::map<std::string, std::string>&)> handler) override;
    void unsubscribe(const std::string& topic) override;
    bool healthCheck() override;
    EventBusType getType() const override { return EventBusType::SynapseEventBus; }
};

class EventBusFactory {
public:
    static std::unique_ptr<IEventBusService> create(EventBusType type);
};

} // namespace SynapseMFT

