#pragma once

#include "IEncoder.h"
#include <string>

class IStreamer {
public:
    virtual ~IStreamer() = default;
    virtual bool connect(const std::string& url, const std::string& streamKey) = 0;
    virtual bool sendPacket(const EncodedPacket& packet) = 0;
    virtual void disconnect() = 0;
};
