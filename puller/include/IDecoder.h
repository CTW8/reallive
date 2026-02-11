#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct DecoderConfig {
    std::string codec = "h264";
    bool useHardware = true;
};

struct DecodedFrame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t timestamp = 0;
};

class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual bool init(const DecoderConfig& config) = 0;
    virtual DecodedFrame decode(const uint8_t* data, int size, int64_t timestamp) = 0;
    virtual void flush() = 0;
};
