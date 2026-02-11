#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    int64_t dts = 0;
    bool isKeyFrame = false;
    bool isVideo = true;
};

class IStreamReceiver {
public:
    virtual ~IStreamReceiver() = default;
    virtual bool connect(const std::string& url) = 0;
    virtual EncodedPacket receivePacket() = 0;
    virtual void disconnect() = 0;
};
