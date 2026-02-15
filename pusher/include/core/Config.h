#pragma once

#include "platform/ICameraCapture.h"
#include "platform/IAudioCapture.h"
#include "platform/IEncoder.h"
#include "platform/IStreamer.h"
#include <string>

namespace reallive {

struct RecordConfig {
    bool enabled = false;
    std::string outputDir = "./recordings";
    int segmentDurationSec = 60;
    int minFreePercent = 15;
    int targetFreePercent = 20;
    bool generateThumbnails = true;
};

struct ControlConfig {
    bool enabled = false;
    std::string host = "0.0.0.0";
    int port = 8090;
    std::string replayRtmpBase = "rtmp://localhost:1935/history";
    std::string ffmpegBin = "ffmpeg";
};

struct DetectionConfig {
    bool enabled = true;
    bool drawOverlay = true;
    int intervalFrames = 2;
    double motionRatioThreshold = 0.015;
    int diffThreshold = 22;
    double minBoxAreaRatio = 0.006;
    int holdMs = 1000;
    int eventMinIntervalMs = 1500;
    bool useOpenCvMotion = true;
    bool useTfliteSsd = true;
    bool inferOnMotionOnly = true;
    std::string tfliteModelPath = "./models/detect.tflite";
    std::string tfliteLabelPath = "./models/labels.txt";
    int tfliteInputSize = 320;
    double personScoreThreshold = 0.55;
    int inferMinIntervalMs = 220;
};

struct MqttConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 1883;
    std::string clientId = "";
    std::string username = "";
    std::string password = "";
    std::string topicPrefix = "reallive/device";
    int keepaliveSec = 30;
    int commandQos = 1;
    int stateQos = 0;
    int stateIntervalMs = 1000;
};

struct PusherConfig {
    StreamConfig stream;
    CaptureConfig camera;
    AudioConfig audio;
    EncoderConfig encoder;
    RecordConfig record;
    ControlConfig control;
    DetectionConfig detection;
    MqttConfig mqtt;
    bool enableAudio = false;
};

class Config {
public:
    Config();

    bool loadFromFile(const std::string& path);
    bool loadFromArgs(int argc, char* argv[]);
    const PusherConfig& get() const;

    // Override individual fields from command line
    void setServerUrl(const std::string& url);
    void setStreamKey(const std::string& key);

private:
    PusherConfig config_;
    bool parseJson(const std::string& jsonStr);
};

} // namespace reallive
