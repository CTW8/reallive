#include "core/Config.h"
#include "core/ControlServer.h"
#include "core/Pipeline.h"

#include <csignal>
#include <iostream>
#include <thread>

using namespace reallive;

namespace {

Pipeline* g_pipeline = nullptr;
ControlServer* g_controlServer = nullptr;

void signalHandler(int sig) {
    std::cout << "\n[Main] Signal " << sig << " received, stopping..." << std::endl;
    if (g_pipeline) {
        g_pipeline->stop();
    }
    if (g_controlServer) {
        g_controlServer->stop();
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
    ControlServer controlServer(config.get());
    g_controlServer = &controlServer;

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

    std::cout << "[Main] Pipeline running. Press Ctrl+C to stop." << std::endl;

    // Wait for the pipeline to finish (blocks until stop() or stream end)
    while (pipeline.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    pipeline.stop();
    controlServer.stop();
    g_pipeline = nullptr;
    g_controlServer = nullptr;

    std::cout << "[Main] Exiting." << std::endl;
    return 0;
}
