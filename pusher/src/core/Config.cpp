#include "core/Config.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>

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
    // Set defaults - 720p @ 15fps for software encoding on Pi 5
    config_.stream.url = "rtmp://localhost:1935/live";
    config_.stream.streamKey = "";
    config_.camera.width = 1280;
    config_.camera.height = 720;
    config_.camera.fps = 15;
    config_.camera.pixelFormat = "NV12";
    config_.encoder.width = 1280;
    config_.encoder.height = 720;
    config_.encoder.fps = 15;
    config_.encoder.codec = "h264";
    config_.encoder.bitrate = 2000000;  // 2Mbps for 720p
    config_.encoder.profile = "main";
    config_.encoder.gopSize = 30;
    config_.encoder.inputFormat = "NV12";
    config_.audio.sampleRate = 44100;
    config_.audio.channels = 1;
    config_.audio.bitsPerSample = 16;
    config_.audio.device = "default";
    config_.record.enabled = false;
    config_.record.outputDir = "./recordings";
    config_.record.segmentDurationSec = 60;
    config_.record.minFreePercent = 15;
    config_.record.targetFreePercent = 20;
    config_.record.generateThumbnails = true;
    config_.control.enabled = false;
    config_.control.host = "0.0.0.0";
    config_.control.port = 8090;
    config_.control.replayRtmpBase = "rtmp://localhost:1935/history";
    config_.control.ffmpegBin = "ffmpeg";
    config_.detection.enabled = true;
    config_.detection.drawOverlay = true;
    config_.detection.intervalFrames = 2;
    config_.detection.motionRatioThreshold = 0.015;
    config_.detection.diffThreshold = 22;
    config_.detection.minBoxAreaRatio = 0.006;
    config_.detection.holdMs = 1000;
    config_.detection.eventMinIntervalMs = 1500;
    config_.detection.useOpenCvMotion = true;
    config_.detection.useTfliteSsd = true;
    config_.detection.inferOnMotionOnly = true;
    config_.detection.tfliteModelPath = "/home/lz/reallive/model/yolov8n_float16.tflite";
    config_.detection.tfliteLabelPath = "./models/labels.txt";
    config_.detection.tfliteInputSize = 320;
    config_.detection.personScoreThreshold = 0.55;
    config_.detection.inferMinIntervalMs = 220;
    config_.mqtt.enabled = false;
    config_.mqtt.host = "127.0.0.1";
    config_.mqtt.port = 1883;
    config_.mqtt.clientId = "";
    config_.mqtt.username = "";
    config_.mqtt.password = "";
    config_.mqtt.topicPrefix = "reallive/device";
    config_.mqtt.keepaliveSec = 30;
    config_.mqtt.commandQos = 1;
    config_.mqtt.stateQos = 0;
    config_.mqtt.stateIntervalMs = 1000;
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

    int gop = jsonInt(jsonStr, "gop", 0);
    if (gop > 0) config_.encoder.gopSize = gop;

    // Audio section
    config_.enableAudio = jsonBool(jsonStr, "enable_audio", false);

    int sampleRate = jsonInt(jsonStr, "sample_rate", 0);
    if (sampleRate > 0) config_.audio.sampleRate = sampleRate;

    int channels = jsonInt(jsonStr, "channels", 0);
    if (channels > 0) config_.audio.channels = channels;

    std::string audioDevice = jsonValue(jsonStr, "audio_device");
    if (!audioDevice.empty()) config_.audio.device = audioDevice;

    // Local recording section
    config_.record.enabled = jsonBool(jsonStr, "enable_record", config_.record.enabled);
    std::string outputDir = jsonValue(jsonStr, "record_output_dir");
    if (!outputDir.empty()) config_.record.outputDir = outputDir;

    int segmentSec = jsonInt(jsonStr, "record_segment_seconds", 0);
    if (segmentSec > 0) config_.record.segmentDurationSec = segmentSec;

    int minFreePct = jsonInt(jsonStr, "record_min_free_percent", 0);
    if (minFreePct > 0) config_.record.minFreePercent = minFreePct;

    int targetFreePct = jsonInt(jsonStr, "record_target_free_percent", 0);
    if (targetFreePct > 0) config_.record.targetFreePercent = targetFreePct;

    config_.record.generateThumbnails = jsonBool(
        jsonStr, "record_thumbnail", config_.record.generateThumbnails);

    config_.control.enabled = jsonBool(
        jsonStr, "control_enable", config_.control.enabled);
    std::string controlHost = jsonValue(jsonStr, "control_host");
    if (!controlHost.empty()) config_.control.host = controlHost;
    int controlPort = jsonInt(jsonStr, "control_port", 0);
    if (controlPort > 0) config_.control.port = controlPort;
    std::string replayRtmpBase = jsonValue(jsonStr, "replay_rtmp_base");
    if (!replayRtmpBase.empty()) config_.control.replayRtmpBase = replayRtmpBase;
    std::string ffmpegBin = jsonValue(jsonStr, "ffmpeg_bin");
    if (!ffmpegBin.empty()) config_.control.ffmpegBin = ffmpegBin;

    config_.detection.enabled = jsonBool(
        jsonStr, "detect_enable", config_.detection.enabled);
    config_.detection.drawOverlay = jsonBool(
        jsonStr, "detect_draw_overlay", config_.detection.drawOverlay);
    config_.detection.intervalFrames = std::max(
        1, jsonInt(jsonStr, "detect_interval_frames", config_.detection.intervalFrames));
    config_.detection.diffThreshold = std::max(
        4, jsonInt(jsonStr, "detect_diff_threshold", config_.detection.diffThreshold));

    {
        const std::string motionRatio = jsonValue(jsonStr, "detect_motion_ratio");
        if (!motionRatio.empty()) {
            const double value = std::atof(motionRatio.c_str());
            if (value > 0.0 && value < 1.0) {
                config_.detection.motionRatioThreshold = value;
            }
        }
    }
    {
        const std::string areaRatio = jsonValue(jsonStr, "detect_min_box_area_ratio");
        if (!areaRatio.empty()) {
            const double value = std::atof(areaRatio.c_str());
            if (value > 0.0 && value < 1.0) {
                config_.detection.minBoxAreaRatio = value;
            }
        }
    }
    config_.detection.holdMs = std::max(
        200, jsonInt(jsonStr, "detect_hold_ms", config_.detection.holdMs));
    config_.detection.eventMinIntervalMs = std::max(
        200, jsonInt(jsonStr, "detect_event_min_interval_ms", config_.detection.eventMinIntervalMs));
    config_.detection.useOpenCvMotion = jsonBool(
        jsonStr, "detect_opencv_motion_enable", config_.detection.useOpenCvMotion);
    config_.detection.useTfliteSsd = jsonBool(
        jsonStr, "detect_tflite_enable", config_.detection.useTfliteSsd);
    config_.detection.inferOnMotionOnly = jsonBool(
        jsonStr, "detect_infer_on_motion_only", config_.detection.inferOnMotionOnly);
    config_.detection.tfliteInputSize = std::max(
        128, jsonInt(jsonStr, "detect_tflite_input_size", config_.detection.tfliteInputSize));
    config_.detection.inferMinIntervalMs = std::max(
        10, jsonInt(jsonStr, "detect_infer_interval_ms", config_.detection.inferMinIntervalMs));
    {
        const std::string score = jsonValue(jsonStr, "detect_person_score_threshold");
        if (!score.empty()) {
            const double value = std::atof(score.c_str());
            if (value > 0.0 && value <= 1.0) {
                config_.detection.personScoreThreshold = value;
            }
        }
    }
    {
        const std::string modelPath = jsonValue(jsonStr, "detect_tflite_model");
        if (!modelPath.empty()) config_.detection.tfliteModelPath = modelPath;
    }
    {
        const std::string labelPath = jsonValue(jsonStr, "detect_tflite_labels");
        if (!labelPath.empty()) config_.detection.tfliteLabelPath = labelPath;
    }

    config_.mqtt.enabled = jsonBool(jsonStr, "mqtt_enable", config_.mqtt.enabled);
    {
        const std::string mqttHost = jsonValue(jsonStr, "mqtt_host");
        if (!mqttHost.empty()) config_.mqtt.host = mqttHost;
    }
    {
        const int mqttPort = jsonInt(jsonStr, "mqtt_port", 0);
        if (mqttPort > 0) config_.mqtt.port = mqttPort;
    }
    {
        const std::string mqttClientId = jsonValue(jsonStr, "mqtt_client_id");
        if (!mqttClientId.empty()) config_.mqtt.clientId = mqttClientId;
    }
    {
        const std::string mqttUsername = jsonValue(jsonStr, "mqtt_username");
        if (!mqttUsername.empty()) config_.mqtt.username = mqttUsername;
    }
    {
        const std::string mqttPassword = jsonValue(jsonStr, "mqtt_password");
        if (!mqttPassword.empty()) config_.mqtt.password = mqttPassword;
    }
    {
        const std::string mqttTopicPrefix = jsonValue(jsonStr, "mqtt_topic_prefix");
        if (!mqttTopicPrefix.empty()) config_.mqtt.topicPrefix = mqttTopicPrefix;
    }
    config_.mqtt.keepaliveSec = std::max(
        5, jsonInt(jsonStr, "mqtt_keepalive_sec", config_.mqtt.keepaliveSec));
    config_.mqtt.commandQos = std::max(
        0, std::min(2, jsonInt(jsonStr, "mqtt_command_qos", config_.mqtt.commandQos)));
    config_.mqtt.stateQos = std::max(
        0, std::min(2, jsonInt(jsonStr, "mqtt_state_qos", config_.mqtt.stateQos)));
    config_.mqtt.stateIntervalMs = std::max(
        200, jsonInt(jsonStr, "mqtt_state_interval_ms", config_.mqtt.stateIntervalMs));

    std::cout << "[Config] Loaded: " << config_.stream.url
              << " " << config_.camera.width << "x" << config_.camera.height
              << "@" << config_.camera.fps << "fps"
              << " bitrate=" << config_.encoder.bitrate
              << " record=" << (config_.record.enabled ? "on" : "off")
              << " control=" << (config_.control.enabled ? "on" : "off")
              << " mqtt=" << (config_.mqtt.enabled ? "on" : "off")
              << " detect=" << (config_.detection.enabled ? "on" : "off")
              << std::endl;

    return true;
}

bool Config::loadFromArgs(int argc, char* argv[]) {
    bool configProvided = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configProvided = true;
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
        } else if (arg == "--record") {
            config_.record.enabled = true;
        } else if (arg == "--record-dir" && i + 1 < argc) {
            config_.record.outputDir = argv[++i];
        } else if (arg == "--segment-seconds" && i + 1 < argc) {
            config_.record.segmentDurationSec = std::atoi(argv[++i]);
        } else if (arg == "--min-free" && i + 1 < argc) {
            config_.record.minFreePercent = std::atoi(argv[++i]);
        } else if (arg == "--target-free" && i + 1 < argc) {
            config_.record.targetFreePercent = std::atoi(argv[++i]);
        } else if (arg == "--no-thumbnail") {
            config_.record.generateThumbnails = false;
        } else if (arg == "--control") {
            config_.control.enabled = true;
        } else if (arg == "--control-port" && i + 1 < argc) {
            config_.control.port = std::atoi(argv[++i]);
        } else if (arg == "--replay-rtmp-base" && i + 1 < argc) {
            config_.control.replayRtmpBase = argv[++i];
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
                      << "  --record              Enable local segmented recording\n"
                      << "  --record-dir <dir>    Recording output directory\n"
                      << "  --segment-seconds <n> Segment duration in seconds\n"
                      << "  --min-free <pct>      Delete old files when free < pct\n"
                      << "  --target-free <pct>   Stop deleting when free >= pct\n"
                      << "  --no-thumbnail        Disable thumbnail generation\n"
                      << "  --control             Enable local control HTTP API\n"
                      << "  --control-port <n>    Control HTTP listen port\n"
                      << "  --replay-rtmp-base    RTMP base for history replay output\n"
                      << "  --audio               Enable audio capture\n"
                      << "  --help                Show this help\n";
            return false;
        }
    }

    if (!configProvided) {
        std::vector<std::string> candidates;
        const char* envCfg = std::getenv("PUSHER_CONFIG_PATH");
        if (envCfg && std::strlen(envCfg) > 0) {
            candidates.emplace_back(envCfg);
        }
        candidates.emplace_back("./pusher.json");
        candidates.emplace_back("./config/pusher.json");
        candidates.emplace_back("../config/pusher.json");

        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec) || ec) continue;
            if (loadFromFile(candidate)) {
                std::cout << "[Config] Auto-loaded config: " << candidate << std::endl;
                break;
            }
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
