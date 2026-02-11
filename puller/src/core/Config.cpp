#include "core/Config.h"

#include <fstream>
#include <iostream>
#include <sstream>

// Minimal JSON parsing without external dependency.
// For production use, consider nlohmann/json or rapidjson.

namespace reallive {
namespace puller {

namespace {

// Trim whitespace and quotes from a string value
std::string trimValue(const std::string& s) {
    std::string result = s;
    // Remove leading/trailing whitespace
    size_t start = result.find_first_not_of(" \t\n\r");
    size_t end = result.find_last_not_of(" \t\n\r,");
    if (start == std::string::npos) return "";
    result = result.substr(start, end - start + 1);
    // Remove quotes
    if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}

// Simple key-value extractor from JSON-like text
std::string extractValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return "";
    pos++; // skip ':'
    // Find the value - skip whitespace
    size_t valStart = json.find_first_not_of(" \t\n\r", pos);
    if (valStart == std::string::npos) return "";

    if (json[valStart] == '"') {
        // String value
        size_t valEnd = json.find('"', valStart + 1);
        if (valEnd == std::string::npos) return "";
        return json.substr(valStart + 1, valEnd - valStart - 1);
    } else {
        // Number or boolean value
        size_t valEnd = json.find_first_of(",}\n\r", valStart);
        if (valEnd == std::string::npos) valEnd = json.size();
        return trimValue(json.substr(valStart, valEnd - valStart));
    }
}

} // anonymous namespace

PullerConfig Config::load(const std::string& filepath) {
    PullerConfig config;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Config] Cannot open config file: " << filepath
                  << ", using defaults." << std::endl;
        return config;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Parse known fields
    std::string val;

    val = extractValue(json, "url");
    if (!val.empty()) config.serverUrl = val;

    val = extractValue(json, "stream_key");
    if (!val.empty()) config.streamKey = val;

    val = extractValue(json, "output_dir");
    if (!val.empty()) config.outputDir = val;

    val = extractValue(json, "format");
    if (!val.empty()) config.format = val;

    val = extractValue(json, "segment_duration");
    if (!val.empty()) {
        try { config.segmentDuration = std::stoi(val); }
        catch (...) {}
    }

    val = extractValue(json, "hardware");
    if (!val.empty()) {
        config.hardwareDecode = (val == "true" || val == "1");
    }

    std::cout << "[Config] Loaded config from: " << filepath << std::endl;
    std::cout << "[Config]   Server URL: " << config.serverUrl << std::endl;
    std::cout << "[Config]   Output dir: " << config.outputDir << std::endl;
    std::cout << "[Config]   Format: " << config.format << std::endl;
    std::cout << "[Config]   Segment duration: " << config.segmentDuration << "s" << std::endl;
    std::cout << "[Config]   Hardware decode: " << (config.hardwareDecode ? "yes" : "no") << std::endl;

    return config;
}

bool Config::save(const std::string& filepath, const PullerConfig& config) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Config] Cannot write config file: " << filepath << std::endl;
        return false;
    }

    file << "{\n";
    file << "    \"server\": {\n";
    file << "        \"url\": \"" << config.serverUrl << "\",\n";
    file << "        \"stream_key\": \"" << config.streamKey << "\"\n";
    file << "    },\n";
    file << "    \"storage\": {\n";
    file << "        \"output_dir\": \"" << config.outputDir << "\",\n";
    file << "        \"format\": \"" << config.format << "\",\n";
    file << "        \"segment_duration\": " << config.segmentDuration << "\n";
    file << "    },\n";
    file << "    \"decode\": {\n";
    file << "        \"hardware\": " << (config.hardwareDecode ? "true" : "false") << "\n";
    file << "    }\n";
    file << "}\n";

    return true;
}

std::string Config::buildStreamUrl(const PullerConfig& config) {
    std::string url = config.serverUrl;
    if (!config.streamKey.empty()) {
        if (url.back() != '/') url += '/';
        url += config.streamKey;
    }
    return url;
}

} // namespace puller
} // namespace reallive
