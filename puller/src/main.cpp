#include "core/Config.h"
#include "core/PullPipeline.h"

#include <csignal>
#include <iostream>
#include <string>
#include <thread>

using namespace reallive::puller;

namespace {

PullPipeline* g_pipeline = nullptr;

void signalHandler(int sig) {
    std::cout << "\n[Main] Signal " << sig << " received, stopping..." << std::endl;
    if (g_pipeline) {
        g_pipeline->stop();
    }
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n"
              << "\nOptions:\n"
              << "  -c, --config <path>   Config file path (default: config/puller.json)\n"
              << "  -u, --url <url>       Override stream URL\n"
              << "  -o, --output <dir>    Override output directory\n"
              << "  -h, --help            Show this help message\n"
              << std::endl;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::string configPath = "config/puller.json";
    std::string urlOverride;
    std::string outputOverride;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else if ((arg == "-u" || arg == "--url") && i + 1 < argc) {
            urlOverride = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputOverride = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "=== RealLive Puller ===" << std::endl;

    // Load configuration
    PullerConfig config = Config::load(configPath);

    // Apply overrides
    if (!urlOverride.empty()) {
        config.serverUrl = urlOverride;
    }
    if (!outputOverride.empty()) {
        config.outputDir = outputOverride;
    }

    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create and run pipeline
    PullPipeline pipeline;
    g_pipeline = &pipeline;

    if (!pipeline.init(config)) {
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
    g_pipeline = nullptr;

    std::cout << "[Main] Exiting." << std::endl;
    return 0;
}
