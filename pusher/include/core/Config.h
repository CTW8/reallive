#pragma once

#include "platform/ICameraCapture.h"
#include "platform/IAudioCapture.h"
#include "platform/IEncoder.h"
#include "platform/IStreamer.h"
#include <string>

namespace reallive {

struct PusherConfig {
    StreamConfig stream;
    CaptureConfig camera;
    AudioConfig audio;
    EncoderConfig encoder;
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
