#pragma once

#include "IEncoder.h"
#include "IAudioCapture.h"
#include <string>
#include <memory>

namespace reallive {

struct StreamConfig {
    std::string url;        // e.g. "rtmp://localhost:1935/live"
    std::string streamKey;
    int connectTimeoutMs = 5000;
    int writeTimeoutMs = 3000;
};

class IStreamer {
public:
    virtual ~IStreamer() = default;

    virtual bool connect(const StreamConfig& config) = 0;
    virtual bool sendVideoPacket(const EncodedPacket& packet) = 0;
    virtual bool sendAudioPacket(const AudioFrame& frame) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string getName() const = 0;
};

using StreamerPtr = std::unique_ptr<IStreamer>;

} // namespace reallive
