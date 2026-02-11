#include "core/Config.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>

// Minimal JSON parser -- avoids external dependency for a small config file.
// Handles the flat/nested structure of pusher.json without a full JSON library.

namespace reallive {

namespace {

// Trim whitespace
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract a string value for a given key from a JSON-like string
// This is intentionally simple -- supports "key": "value" and "key": number
std::string jsonValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    // skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // string value
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    } else {
        // numeric or boolean value
        size_t end = json.find_first_of(",}\n\r", pos);
        if (end == std::string::npos) end = json.size();
        return trim(json.substr(pos, end - pos));
    }
}

int jsonInt(const std::string& json, const std::string& key, int defaultVal) {
    std::string val = jsonValue(json, key);
    if (val.empty()) return defaultVal;
    return std::atoi(val.c_str());
}

bool jsonBool(const std::string& json, const std::string& key, bool defaultVal) {
    std::string val = jsonValue(json, key);
    if (val.empty()) return defaultVal;
    return val == "true";
}

} // anonymous namespace

Config::Config() {
    // Set defaults
    config_.stream.url = "rtmp://localhost:1935/live";
    config_.stream.streamKey = "";
    config_.camera.width = 1920;
    config_.camera.height = 1080;
    config_.camera.fps = 30;
    config_.camera.pixelFormat = "NV12";
    config_.encoder.codec = "h264";
    config_.encoder.bitrate = 4000000;
    config_.encoder.profile = "main";
    config_.encoder.gopSize = 60;
    config_.encoder.inputFormat = "NV12";
    config_.audio.sampleRate = 44100;
    config_.audio.channels = 1;
    config_.audio.bitsPerSample = 16;
    config_.audio.device = "default";
    config_.enableAudio = false;
}

bool Config::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Config] Failed to open config file: " << path << std::endl;
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    return parseJson(content);
}

bool Config::parseJson(const std::string& jsonStr) {
    // Server section
    std::string url = jsonValue(jsonStr, "url");
    if (!url.empty()) config_.stream.url = url;

    std::string streamKey = jsonValue(jsonStr, "stream_key");
    if (!streamKey.empty()) config_.stream.streamKey = streamKey;

    // Camera section
    int w = jsonInt(jsonStr, "width", 0);
    if (w > 0) {
        config_.camera.width = w;
        config_.encoder.width = w;
    }
    int h = jsonInt(jsonStr, "height", 0);
    if (h > 0) {
        config_.camera.height = h;
        config_.encoder.height = h;
    }
    int fps = jsonInt(jsonStr, "fps", 0);
    if (fps > 0) {
        config_.camera.fps = fps;
        config_.encoder.fps = fps;
    }

    // Encoder section
    std::string codec = jsonValue(jsonStr, "codec");
    if (!codec.empty()) config_.encoder.codec = codec;

    int bitrate = jsonInt(jsonStr, "bitrate", 0);
    if (bitrate > 0) config_.encoder.bitrate = bitrate;

    std::string profile = jsonValue(jsonStr, "profile");
    if (!profile.empty()) config_.encoder.profile = profile;

    // Audio section
    config_.enableAudio = jsonBool(jsonStr, "enable_audio", false);

    int sampleRate = jsonInt(jsonStr, "sample_rate", 0);
    if (sampleRate > 0) config_.audio.sampleRate = sampleRate;

    int channels = jsonInt(jsonStr, "channels", 0);
    if (channels > 0) config_.audio.channels = channels;

    std::string audioDevice = jsonValue(jsonStr, "audio_device");
    if (!audioDevice.empty()) config_.audio.device = audioDevice;

    std::cout << "[Config] Loaded: " << config_.stream.url
              << " " << config_.camera.width << "x" << config_.camera.height
              << "@" << config_.camera.fps << "fps"
              << " bitrate=" << config_.encoder.bitrate
              << std::endl;

    return true;
}

bool Config::loadFromArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            if (!loadFromFile(argv[++i])) return false;
        } else if ((arg == "-u" || arg == "--url") && i + 1 < argc) {
            config_.stream.url = argv[++i];
        } else if ((arg == "-k" || arg == "--key") && i + 1 < argc) {
            config_.stream.streamKey = argv[++i];
        } else if ((arg == "-w" || arg == "--width") && i + 1 < argc) {
            config_.camera.width = std::atoi(argv[++i]);
            config_.encoder.width = config_.camera.width;
        } else if ((arg == "-h" || arg == "--height") && i + 1 < argc) {
            config_.camera.height = std::atoi(argv[++i]);
            config_.encoder.height = config_.camera.height;
        } else if ((arg == "-f" || arg == "--fps") && i + 1 < argc) {
            config_.camera.fps = std::atoi(argv[++i]);
            config_.encoder.fps = config_.camera.fps;
        } else if ((arg == "-b" || arg == "--bitrate") && i + 1 < argc) {
            config_.encoder.bitrate = std::atoi(argv[++i]);
        } else if (arg == "--audio") {
            config_.enableAudio = true;
        } else if (arg == "--help") {
            std::cout << "Usage: reallive-pusher [options]\n"
                      << "  -c, --config <file>   Config file path (JSON)\n"
                      << "  -u, --url <url>       RTMP server URL\n"
                      << "  -k, --key <key>       Stream key\n"
                      << "  -w, --width <px>      Video width (default: 1920)\n"
                      << "  -h, --height <px>     Video height (default: 1080)\n"
                      << "  -f, --fps <fps>       Frame rate (default: 30)\n"
                      << "  -b, --bitrate <bps>   Encoder bitrate (default: 4000000)\n"
                      << "  --audio               Enable audio capture\n"
                      << "  --help                Show this help\n";
            return false;
        }
    }
    return true;
}

const PusherConfig& Config::get() const {
    return config_;
}

void Config::setServerUrl(const std::string& url) {
    config_.stream.url = url;
}

void Config::setStreamKey(const std::string& key) {
    config_.stream.streamKey = key;
}

} // namespace reallive
