#pragma once

#include "envelope.hpp"
#include "net.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace synapse::cloud {

struct SigV4Request {
    std::string method;
    std::string canonical_uri;
    std::string canonical_query;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string payload;
    std::string access_key;
    std::string secret_key;
    std::string region;
    std::string service;
    std::string amz_date;
};

struct SigV4Result {
    std::string canonical_hash;
    std::string signature;
    std::string authorization;
};

SigV4Result sigv4_sign(const SigV4Request& request);
std::string event_grid_sas(const std::string& resource, const std::string& key_base64, int64_t expiry_epoch);

using HttpSender = std::function<Result<net::HttpResponse>(const net::HttpRequest&)>;

struct AwsConfig {
    std::string access_key;
    std::string secret_key;
    std::string region = "eu-west-1";
    std::string event_bus = "default";
    std::string amz_date;
};

class AwsEventBridge {
public:
    AwsEventBridge(AwsConfig config, HttpSender sender);
    Status put(const bus::EventEnvelope& event);

private:
    AwsConfig config_;
    HttpSender sender_;
};

struct AzureConfig {
    std::string endpoint;
    std::string key_base64;
};

class AzureEventGrid {
public:
    AzureEventGrid(AzureConfig config, HttpSender sender);
    Status publish(const bus::EventEnvelope& event, int64_t expiry_epoch);

private:
    AzureConfig config_;
    HttpSender sender_;
};

}  // namespace synapse::cloud
