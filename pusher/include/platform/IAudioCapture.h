#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace reallive {

struct AudioConfig {
    int sampleRate = 44100;
    int channels = 1;
    int bitsPerSample = 16;
    std::string device;  // e.g. "default" or "hw:0,0"
};

struct AudioFrame {
    std::vector<uint8_t> data;
    int samples = 0;
    int sampleRate = 0;
    int channels = 0;
    int64_t pts = 0;     // presentation timestamp in microseconds

    bool empty() const { return data.empty(); }
};

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;

    virtual bool open(const AudioConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual AudioFrame captureFrame() = 0;
    virtual bool isOpen() const = 0;
    virtual std::string getName() const = 0;
};

using AudioCapturePtr = std::unique_ptr<IAudioCapture>;

} // namespace reallive
