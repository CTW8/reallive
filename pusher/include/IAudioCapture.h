#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct AudioConfig {
    int sampleRate = 48000;
    int channels = 1;
    int bitsPerSample = 16;
    std::string deviceName;
};

struct AudioFrame {
    std::vector<uint8_t> data;
    int samples = 0;
    int64_t timestamp = 0;
};

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual bool open(const AudioConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual AudioFrame captureFrame() = 0;
};
