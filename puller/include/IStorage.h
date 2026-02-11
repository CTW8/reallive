#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct StorageConfig {
    std::string format = "mp4";
    std::string videoCodec = "h264";
    std::string audioCodec = "aac";
    int width = 1920;
    int height = 1080;
    int fps = 30;
};

class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool open(const std::string& path, const StorageConfig& config) = 0;
    virtual bool writePacket(const uint8_t* data, int size, int64_t pts, int64_t dts, bool isVideo, bool isKeyFrame) = 0;
    virtual void close() = 0;
};
