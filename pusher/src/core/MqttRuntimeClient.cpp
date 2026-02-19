#include "core/MqttRuntimeClient.h"
#include "core/Pipeline.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef REALLIVE_HAS_MQTT
#include <mosquitto.h>
#else
struct mosquitto {};
struct mosquitto_message {
    char* topic = nullptr;
    void* payload = nullptr;
    int payloadlen = 0;
};
#endif

namespace reallive {

namespace {

std::string formatNumber(double value, int precision = 1) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

} // namespace

MqttRuntimeClient::MqttRuntimeClient(const PusherConfig& config, Pipeline* pipeline)
    : config_(config), pipeline_(pipeline) {
}

MqttRuntimeClient::~MqttRuntimeClient() {
    stop();
}

bool MqttRuntimeClient::start() {
    if (running_) return true;
    if (!config_.mqtt.enabled) return true;

#ifndef REALLIVE_HAS_MQTT
    std::cerr << "[MQTT] Enabled in config, but libmosquitto not linked at build time" << std::endl;
    return false;
#else
    if (!pipeline_) {
        std::cerr << "[MQTT] Pipeline unavailable" << std::endl;
        return false;
    }

    const std::string streamKey = sanitizeToken(config_.stream.streamKey);
    if (streamKey.empty()) {
        std::cerr << "[MQTT] stream_key is required for MQTT runtime control" << std::endl;
        return false;
    }

    std::string prefix = trim(config_.mqtt.topicPrefix);
    if (prefix.empty()) prefix = "reallive/device";
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    commandTopic_ = prefix + "/" + streamKey + "/command";
    stateTopic_ = prefix + "/" + streamKey + "/state";

    clientId_ = trim(config_.mqtt.clientId);
    if (clientId_.empty()) {
        clientId_ = "reallive-pusher-" + streamKey;
    }

    mosquitto_lib_init();
    mosq_ = mosquitto_new(clientId_.c_str(), true, this);
    if (!mosq_) {
        std::cerr << "[MQTT] mosquitto_new failed" << std::endl;
        mosquitto_lib_cleanup();
        return false;
    }

    if (!config_.mqtt.username.empty()) {
        int rc = mosquitto_username_pw_set(
            mosq_,
            config_.mqtt.username.c_str(),
            config_.mqtt.password.empty() ? nullptr : config_.mqtt.password.c_str()
        );
        if (rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MQTT] username/password setup failed: " << mosquitto_strerror(rc) << std::endl;
            mosquitto_destroy(mosq_);
            mosq_ = nullptr;
            mosquitto_lib_cleanup();
            return false;
        }
    }

    mosquitto_connect_callback_set(mosq_, &MqttRuntimeClient::handleConnect);
    mosquitto_disconnect_callback_set(mosq_, &MqttRuntimeClient::handleDisconnect);
    mosquitto_message_callback_set(mosq_, &MqttRuntimeClient::handleMessage);

    int rc = mosquitto_connect_async(
        mosq_,
        config_.mqtt.host.c_str(),
        config_.mqtt.port,
        config_.mqtt.keepaliveSec
    );
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] connect_async failed: " << mosquitto_strerror(rc) << std::endl;
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        mosquitto_lib_cleanup();
        return false;
    }

    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] loop_start failed: " << mosquitto_strerror(rc) << std::endl;
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        mosquitto_lib_cleanup();
        return false;
    }

    running_ = true;
    stateThread_ = std::thread(&MqttRuntimeClient::stateLoop, this);
    std::cout << "[MQTT] Runtime control started, command_topic=" << commandTopic_ << std::endl;
    return true;
#endif
}

void MqttRuntimeClient::stop() {
    if (!running_) return;
    running_ = false;

#ifdef REALLIVE_HAS_MQTT
    if (stateThread_.joinable()) {
        stateThread_.join();
    }

    std::lock_guard<std::mutex> lock(mqttMutex_);
    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, false);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    connected_ = false;
    mosquitto_lib_cleanup();
#endif
}

bool MqttRuntimeClient::isRunning() const {
    return running_;
}

int64_t MqttRuntimeClient::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string MqttRuntimeClient::trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

std::string MqttRuntimeClient::lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string MqttRuntimeClient::sanitizeToken(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw) {
        const bool ok = (ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') ||
                        ch == '-' || ch == '_' || ch == '.';
        out.push_back(ok ? ch : '_');
    }
    return out;
}

std::string MqttRuntimeClient::jsonValue(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return "";
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) p++;
    if (p >= body.size()) return "";

    if (body[p] == '"') {
        std::string out;
        ++p;
        bool esc = false;
        for (; p < body.size(); ++p) {
            char ch = body[p];
            if (esc) {
                out.push_back(ch);
                esc = false;
            } else if (ch == '\\') {
                esc = true;
            } else if (ch == '"') {
                return out;
            } else {
                out.push_back(ch);
            }
        }
        return "";
    }

    size_t e = body.find_first_of(",}\r\n", p);
    if (e == std::string::npos) e = body.size();
    return trim(body.substr(p, e - p));
}

void MqttRuntimeClient::publishState(const char* reason, int64_t commandSeq) {
#ifdef REALLIVE_HAS_MQTT
    std::lock_guard<std::mutex> lock(mqttMutex_);
    if (!mosq_) return;
    int minFreePercent = 0;
    if (pipeline_) {
        int ignoredTarget = 0;
        pipeline_->getRecordCleanupPolicy(minFreePercent, ignoredTarget);
    }
    double storagePct = 0.0;
    double storageUsedGb = 0.0;
    double storageTotalGb = 0.0;
    try {
        const auto space = std::filesystem::space("/");
        if (space.capacity > 0) {
            const uint64_t used = space.capacity > space.available ? (space.capacity - space.available) : 0;
            storageTotalGb = static_cast<double>(space.capacity) / (1024.0 * 1024.0 * 1024.0);
            storageUsedGb = static_cast<double>(used) / (1024.0 * 1024.0 * 1024.0);
            storagePct = std::max(0.0, std::min(100.0, static_cast<double>(used) * 100.0 / static_cast<double>(space.capacity)));
        }
    } catch (...) {
    }

    std::ostringstream oss;
    oss << "{"
        << "\"v\":1,"
        << "\"ts\":" << nowMs() << ","
        << "\"stream_key\":\"" << config_.stream.streamKey << "\","
        << "\"running\":" << (pipeline_ && pipeline_->isRunning() ? "true" : "false") << ","
        << "\"desired_live\":" << (pipeline_ && pipeline_->isLivePushEnabled() ? "true" : "false") << ","
        << "\"active_live\":" << (pipeline_ && pipeline_->isLivePushActive() ? "true" : "false") << ","
        << "\"record_min_free_percent\":" << minFreePercent << ","
        << "\"storage_pct\":" << formatNumber(storagePct) << ","
        << "\"storage_used_gb\":" << formatNumber(storageUsedGb, 2) << ","
        << "\"storage_total_gb\":" << formatNumber(storageTotalGb, 2);
    if (reason && std::strlen(reason) > 0) {
        oss << ",\"reason\":\"" << reason << "\"";
    }
    if (commandSeq >= 0) {
        oss << ",\"command_seq\":" << commandSeq;
    }
    oss << "}";

    const std::string payload = oss.str();
    mosquitto_publish(
        mosq_,
        nullptr,
        stateTopic_.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        config_.mqtt.stateQos,
        true
    );
#else
    (void)reason;
    (void)commandSeq;
#endif
}

void MqttRuntimeClient::stateLoop() {
    while (running_) {
        publishState("heartbeat");
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.mqtt.stateIntervalMs));
    }
}

void MqttRuntimeClient::onConnect(int rc) {
#ifdef REALLIVE_HAS_MQTT
    if (rc != 0) {
        connected_ = false;
        std::cerr << "[MQTT] Connect failed, rc=" << rc << std::endl;
        return;
    }
    connected_ = true;
    {
        std::lock_guard<std::mutex> lock(mqttMutex_);
        if (mosq_) {
            mosquitto_subscribe(mosq_, nullptr, commandTopic_.c_str(), config_.mqtt.commandQos);
        }
    }
    std::cout << "[MQTT] Connected, subscribed to " << commandTopic_ << std::endl;
    publishState("connected");
#else
    (void)rc;
#endif
}

void MqttRuntimeClient::onDisconnect(int rc) {
    connected_ = false;
    std::cerr << "[MQTT] Disconnected, rc=" << rc << std::endl;
}

void MqttRuntimeClient::onMessage(const std::string& topic, const std::string& payload) {
    if (topic != commandTopic_ || !pipeline_) return;

    const std::string streamKey = jsonValue(payload, "stream_key");
    if (!streamKey.empty() && streamKey != config_.stream.streamKey) {
        return;
    }

    const std::string type = lower(trim(jsonValue(payload, "type")));

    int64_t seq = -1;
    {
        const std::string seqRaw = jsonValue(payload, "seq");
        if (!seqRaw.empty()) {
            char* end = nullptr;
            long long parsed = std::strtoll(seqRaw.c_str(), &end, 10);
            if (end && *end == '\0') {
                seq = static_cast<int64_t>(parsed);
            }
        }
    }
    if (seq >= 0) {
        const int64_t prev = commandSeq_.load();
        if (seq <= prev) {
            publishState("ignored-old-seq", seq);
            return;
        }
        commandSeq_ = seq;
    }

    if (type == "record_policy" || type == "record") {
        std::string minRaw = jsonValue(payload, "min_free_percent");
        if (minRaw.empty()) minRaw = jsonValue(payload, "record_min_free_percent");
        if (minRaw.empty()) {
            publishState("invalid-record-policy", seq);
            return;
        }
        const int minFreePercent = std::max(1, std::atoi(minRaw.c_str()));
        int currentMin = 15;
        int currentTarget = 20;
        pipeline_->getRecordCleanupPolicy(currentMin, currentTarget);
        const int targetFreePercent = std::max(minFreePercent + 1, std::max(currentTarget, minFreePercent + 5));
        const bool ok = pipeline_->setRecordCleanupPolicy(minFreePercent, targetFreePercent);
        if (ok) {
            std::cout << "[MQTT] Record policy applied: min_free=" << minFreePercent
                      << "% target_free=" << targetFreePercent
                      << (seq >= 0 ? (", seq=" + std::to_string(seq)) : "") << std::endl;
            publishState("record-policy-applied", seq);
            return;
        }
        std::cerr << "[MQTT] Failed to apply record policy: min_free=" << minFreePercent
                  << " target_free=" << targetFreePercent << std::endl;
        publishState("record-policy-failed", seq);
        return;
    }

    if (type == "storage_query" || type == "state_query" || type == "report_state") {
        publishState("storage-query", seq);
        return;
    }

    std::string enableRaw = jsonValue(payload, "enable");
    if (enableRaw.empty()) enableRaw = jsonValue(payload, "live");
    if (enableRaw.empty()) return;
    const std::string enableNorm = lower(trim(enableRaw));
    const bool enable = (enableNorm == "1" || enableNorm == "true" ||
                         enableNorm == "on" || enableNorm == "yes");
    const bool ok = pipeline_->setLivePushEnabled(enable);
    if (ok) {
        std::cout << "[MQTT] Live push command applied: " << (enable ? "on" : "off")
                  << (seq >= 0 ? (", seq=" + std::to_string(seq)) : "") << std::endl;
        publishState("applied", seq);
        return;
    }

    std::cerr << "[MQTT] Failed to apply live command: " << (enable ? "on" : "off") << std::endl;
    publishState("apply-failed", seq);
}

void MqttRuntimeClient::handleConnect(::mosquitto* mosq, void* obj, int rc) {
    (void)mosq;
    auto* self = static_cast<MqttRuntimeClient*>(obj);
    if (!self) return;
    self->onConnect(rc);
}

void MqttRuntimeClient::handleDisconnect(::mosquitto* mosq, void* obj, int rc) {
    (void)mosq;
    auto* self = static_cast<MqttRuntimeClient*>(obj);
    if (!self) return;
    self->onDisconnect(rc);
}

void MqttRuntimeClient::handleMessage(::mosquitto* mosq, void* obj, const ::mosquitto_message* msg) {
    (void)mosq;
    auto* self = static_cast<MqttRuntimeClient*>(obj);
    if (!self || !msg || !msg->topic) return;
    std::string topic = msg->topic;
    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
        payload.assign(static_cast<const char*>(msg->payload), static_cast<size_t>(msg->payloadlen));
    }
    self->onMessage(topic, payload);
}

} // namespace reallive
