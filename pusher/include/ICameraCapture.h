#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct CaptureConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string devicePath;
};

struct Frame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t timestamp = 0;
};

class ICameraCapture {
public:
    virtual ~ICameraCapture() = default;
    virtual bool open(const CaptureConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual Frame captureFrame() = 0;
};
