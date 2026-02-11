#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct EncoderConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 4000000;  // 4 Mbps
    std::string codec = "h264";
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    int64_t dts = 0;
    bool isKeyFrame = false;
    bool isVideo = true;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool init(const EncoderConfig& config) = 0;
    virtual EncodedPacket encode(const uint8_t* data, int size, int64_t timestamp) = 0;
    virtual void flush() = 0;
};
