#include "core/Config.h"
#include "core/ControlServer.h"
#include "core/MqttRuntimeClient.h"
#include "core/Pipeline.h"

#include <csignal>
#include <iostream>
#include <thread>

using namespace reallive;

namespace {

Pipeline* g_pipeline = nullptr;
ControlServer* g_controlServer = nullptr;
MqttRuntimeClient* g_mqttClient = nullptr;

void signalHandler(int sig) {
    std::cout << "\n[Main] Signal " << sig << " received, stopping..." << std::endl;
    if (g_pipeline) {
        g_pipeline->stop();
    }
    if (g_controlServer) {
        g_controlServer->stop();
    }
    if (g_mqttClient) {
        g_mqttClient->stop();
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::cout << "=== RealLive Pusher ===" << std::endl;

    // Load configuration
    Config config;
    if (!config.loadFromArgs(argc, argv)) {
        return 1;
    }

    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create and run pipeline
    Pipeline pipeline;
    g_pipeline = &pipeline;
    ControlServer controlServer(config.get(), &pipeline);
    g_controlServer = &controlServer;
    MqttRuntimeClient mqttClient(config.get(), &pipeline);
    g_mqttClient = &mqttClient;

    if (!controlServer.start()) {
        std::cerr << "[Main] Failed to start control server." << std::endl;
        return 1;
    }

    if (!pipeline.init(config.get())) {
        std::cerr << "[Main] Failed to initialize pipeline." << std::endl;
        return 1;
    }

    if (!pipeline.start()) {
        std::cerr << "[Main] Failed to start pipeline." << std::endl;
        return 1;
    }
    if (!mqttClient.start()) {
        std::cerr << "[Main] Failed to start MQTT runtime client." << std::endl;
        return 1;
    }

    std::cout << "[Main] Pipeline running. Press Ctrl+C to stop." << std::endl;

    // Wait for the pipeline to finish (blocks until stop() or stream end)
    while (pipeline.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    pipeline.stop();
    mqttClient.stop();
    controlServer.stop();
    g_pipeline = nullptr;
    g_controlServer = nullptr;
    g_mqttClient = nullptr;

    std::cout << "[Main] Exiting." << std::endl;
    return 0;
}
