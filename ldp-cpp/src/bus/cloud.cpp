#include "synapse/cloud.hpp"
#include "synapse/codec.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace synapse::cloud {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string hmac_hex(const uint8_t* key, size_t key_len, std::string_view data) {
    const auto mac = codec::hmac_sha256(key, key_len, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return codec::hex_encode(mac.data(), mac.size());
}

std::string iso_now() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
    return buffer;
}

}  // namespace

SigV4Result sigv4_sign(const SigV4Request& request) {
    std::vector<std::pair<std::string, std::string>> headers;
    for (const auto& header : request.headers) headers.emplace_back(lower(trim(header.first)), trim(header.second));
    std::sort(headers.begin(), headers.end());
    std::string canonical_headers;
    std::string signed_headers;
    for (size_t i = 0; i < headers.size(); ++i) {
        canonical_headers += headers[i].first + ":" + headers[i].second + "\n";
        if (i) signed_headers += ";";
        signed_headers += headers[i].first;
    }
    const auto payload_hash = codec::sha256(request.payload);
    const std::string canonical = request.method + "\n" + request.canonical_uri + "\n" + request.canonical_query + "\n" +
                                  canonical_headers + "\n" + signed_headers + "\n" +
                                  codec::hex_encode(payload_hash.data(), payload_hash.size());
    const auto canonical_hash = codec::sha256(canonical);
    const std::string date_stamp = request.amz_date.substr(0, 8);
    const std::string scope = date_stamp + "/" + request.region + "/" + request.service + "/aws4_request";
    const std::string to_sign = "AWS4-HMAC-SHA256\n" + request.amz_date + "\n" + scope + "\n" +
                                codec::hex_encode(canonical_hash.data(), canonical_hash.size());
    const std::string secret = "AWS4" + request.secret_key;
    const auto date_key = codec::hmac_sha256(secret, date_stamp);
    const auto region_key = codec::hmac_sha256(date_key.data(), date_key.size(),
                                               reinterpret_cast<const uint8_t*>(request.region.data()), request.region.size());
    const auto service_key = codec::hmac_sha256(region_key.data(), region_key.size(),
                                                reinterpret_cast<const uint8_t*>(request.service.data()), request.service.size());
    const auto signing_key = codec::hmac_sha256(service_key.data(), service_key.size(),
                                                reinterpret_cast<const uint8_t*>("aws4_request"), 12);
    SigV4Result result;
    result.canonical_hash = codec::hex_encode(canonical_hash.data(), canonical_hash.size());
    result.signature = hmac_hex(signing_key.data(), signing_key.size(), to_sign);
    result.authorization = "AWS4-HMAC-SHA256 Credential=" + request.access_key + "/" + scope +
                           ", SignedHeaders=" + signed_headers + ", Signature=" + result.signature;
    return result;
}

std::string event_grid_sas(const std::string& resource, const std::string& key_base64, int64_t expiry_epoch) {
    auto key = codec::b64_decode(key_base64);
    if (!key.ok()) return {};
    const std::string to_sign = resource + "\n" + std::to_string(expiry_epoch);
    const auto mac = codec::hmac_sha256(key.value.data(), key.value.size(),
                                        reinterpret_cast<const uint8_t*>(to_sign.data()), to_sign.size());
    const std::string signature = codec::b64_encode(mac.data(), mac.size());
    return "r=" + codec::url_encode(resource) + "&e=" + std::to_string(expiry_epoch) + "&s=" + codec::url_encode(signature);
}

AwsEventBridge::AwsEventBridge(AwsConfig config, HttpSender sender)
    : config_(std::move(config)), sender_(std::move(sender)) {}

Status AwsEventBridge::put(const bus::EventEnvelope& event) {
    if (!sender_) return Status::failure("aws sender missing");
    const std::string host = "events." + config_.region + ".amazonaws.com";
    const std::string detail = bus::canonical_envelope(event);
    const std::string body = std::string("{\"Entries\":[{\"Source\":\"synapse.bus\",\"DetailType\":\"") +
                              codec::json_escape(event.topic) + "\",\"Detail\":\"" + codec::json_escape(detail) +
                              "\",\"EventBusName\":\"" + codec::json_escape(config_.event_bus) + "\"}]}";
    const std::string date = config_.amz_date.empty() ? iso_now() : config_.amz_date;
    SigV4Request request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.payload = body;
    request.access_key = config_.access_key;
    request.secret_key = config_.secret_key;
    request.region = config_.region;
    request.service = "events";
    request.amz_date = date;
    request.headers = {{"content-type", "application/x-amz-json-1.1"},
                       {"host", host},
                       {"x-amz-date", date},
                       {"x-amz-target", "AWSEvents.PutEvents"}};
    const auto signed_request = sigv4_sign(request);
    net::HttpRequest http;
    http.method = "POST";
    http.host = host;
    http.port = 443;
    http.tls = true;
    http.target = "/";
    http.body = body;
    http.headers = {{"Content-Type", "application/x-amz-json-1.1"},
                    {"X-Amz-Date", date},
                    {"X-Amz-Target", "AWSEvents.PutEvents"},
                    {"Authorization", signed_request.authorization}};
    auto response = sender_(http);
    if (!response.ok()) return Status::failure(response.error);
    if (response.value.status < 200 || response.value.status >= 300) {
        return Status::failure("aws put events status " + std::to_string(response.value.status));
    }
    return Status::success();
}

AzureEventGrid::AzureEventGrid(AzureConfig config, HttpSender sender)
    : config_(std::move(config)), sender_(std::move(sender)) {}

Status AzureEventGrid::publish(const bus::EventEnvelope& event, int64_t expiry_epoch) {
    if (!sender_) return Status::failure("azure sender missing");
    auto url = net::parse_url("POST", config_.endpoint, "");
    if (!url.ok()) return Status::failure(url.error);
    const std::string token = event_grid_sas(config_.endpoint, config_.key_base64, expiry_epoch);
    if (token.empty()) return Status::failure("azure key rejected");
    const std::string body = std::string("[{\"id\":\"") + codec::json_escape(event.id) +
                             "\",\"eventType\":\"Synapse.Bus.Event\",\"subject\":\"" + codec::json_escape(event.subject) +
                             "\",\"eventTime\":\"1970-01-01T00:00:00Z\",\"data\":" + bus::canonical_envelope(event) +
                             ",\"dataVersion\":\"1\"}]";
    url.value.body = body;
    url.value.headers.push_back({"aeg-sas-token", token});
    url.value.headers.push_back({"Content-Type", "application/json"});
    auto response = sender_(url.value);
    if (!response.ok()) return Status::failure(response.error);
    if (response.value.status < 200 || response.value.status >= 300) {
        return Status::failure("azure event grid status " + std::to_string(response.value.status));
    }
    return Status::success();
}

}  // namespace synapse::cloud
