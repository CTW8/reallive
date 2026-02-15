#pragma once

#include "core/Config.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct mosquitto;
struct mosquitto_message;

namespace reallive {

class Pipeline;

class MqttRuntimeClient {
public:
    MqttRuntimeClient(const PusherConfig& config, Pipeline* pipeline);
    ~MqttRuntimeClient();

    bool start();
    void stop();
    bool isRunning() const;

private:
    static int64_t nowMs();
    static std::string trim(const std::string& s);
    static std::string lower(const std::string& s);
    static std::string sanitizeToken(const std::string& raw);
    static std::string jsonValue(const std::string& body, const std::string& key);

    void publishState(const char* reason = nullptr, int64_t commandSeq = -1);
    void stateLoop();
    void onConnect(int rc);
    void onDisconnect(int rc);
    void onMessage(const std::string& topic, const std::string& payload);

    static void handleConnect(::mosquitto* mosq, void* obj, int rc);
    static void handleDisconnect(::mosquitto* mosq, void* obj, int rc);
    static void handleMessage(::mosquitto* mosq, void* obj, const ::mosquitto_message* msg);

    PusherConfig config_;
    Pipeline* pipeline_ = nullptr;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int64_t> commandSeq_{0};

    std::thread stateThread_;
    std::mutex mqttMutex_;
    ::mosquitto* mosq_ = nullptr;

    std::string clientId_;
    std::string commandTopic_;
    std::string stateTopic_;
};

} // namespace reallive
