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

struct PusherConfig {
    StreamConfig stream;
    CaptureConfig camera;
    AudioConfig audio;
    EncoderConfig encoder;
    RecordConfig record;
    ControlConfig control;
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
