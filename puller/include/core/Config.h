#pragma once

#include <string>

namespace reallive {
namespace puller {

/// Configuration for the puller application
struct PullerConfig {
    // Server settings
    std::string serverUrl = "rtmp://localhost:1935/live";
    std::string streamKey;

    // Storage settings
    std::string outputDir = "./recordings";
    std::string format = "mp4";
    int segmentDuration = 3600;  // seconds

    // Decode settings
    bool hardwareDecode = true;
};

/// Load configuration from a JSON file
class Config {
public:
    /// Load config from file. Returns default config if file not found.
    static PullerConfig load(const std::string& filepath);

    /// Save config to file
    static bool save(const std::string& filepath, const PullerConfig& config);

    /// Build the full stream URL from server URL and stream key
    static std::string buildStreamUrl(const PullerConfig& config);
};

} // namespace puller
} // namespace reallive
