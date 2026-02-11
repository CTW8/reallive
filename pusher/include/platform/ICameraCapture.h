#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace reallive {

struct CaptureConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string device;   // e.g. "/dev/video0" or camera index
    std::string pixelFormat; // e.g. "NV12", "YUV420"
};

struct Frame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;
    int64_t pts = 0;       // presentation timestamp in microseconds
    std::string pixelFormat;

    bool empty() const { return data.empty(); }
};

class ICameraCapture {
public:
    virtual ~ICameraCapture() = default;

    virtual bool open(const CaptureConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual Frame captureFrame() = 0;
    virtual bool isOpen() const = 0;
    virtual std::string getName() const = 0;
};

using CameraCapturePtr = std::unique_ptr<ICameraCapture>;

} // namespace reallive
