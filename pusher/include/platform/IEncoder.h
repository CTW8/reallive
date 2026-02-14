#pragma once

#include "ICameraCapture.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace reallive {

struct EncoderConfig {
    std::string codec = "h264";
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 4000000;        // bits per second
    std::string profile = "main"; // baseline, main, high
    int gopSize = 60;             // keyframe interval in frames
    std::string inputFormat = "NV12";
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;   // presentation timestamp in microseconds
    int64_t dts = 0;   // decode timestamp in microseconds
    bool isKeyframe = false;
    int64_t captureTime = 0;  // capture timestamp (steady_clock microseconds since start)
    int64_t encodeTime = 0;  // encoding duration in microseconds

    bool empty() const { return data.empty(); }
};

class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual bool init(const EncoderConfig& config) = 0;
    virtual EncodedPacket encode(const Frame& frame) = 0;
    virtual void flush() = 0;
    virtual std::string getName() const = 0;
};

using EncoderPtr = std::unique_ptr<IEncoder>;

} // namespace reallive
