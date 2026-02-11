#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reallive {
namespace puller {

/// Codec type enumeration
enum class CodecType {
    H264,
    H265,
    AAC,
    OPUS,
    Unknown
};

/// Media type enumeration
enum class MediaType {
    Video,
    Audio,
    Unknown
};

/// Stream information returned after connecting
struct StreamInfo {
    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrate = 0;
    int sampleRate = 0;
    int channels = 0;
    CodecType videoCodec = CodecType::Unknown;
    CodecType audioCodec = CodecType::Unknown;

    // Codec extradata (SPS/PPS for H.264, etc.)
    std::vector<uint8_t> videoExtradata;
    std::vector<uint8_t> audioExtradata;
};

/// Encoded packet (not yet decoded)
struct EncodedPacket {
    MediaType type = MediaType::Unknown;
    std::vector<uint8_t> data;
    int64_t pts = 0;       // presentation timestamp (microseconds)
    int64_t dts = 0;       // decode timestamp (microseconds)
    bool isKeyFrame = false;

    // Stream-level timing info
    int streamIndex = -1;
    int timebaseNum = 1;
    int timebaseDen = 1000;

    bool empty() const { return data.empty(); }
};

/// Abstract interface for pulling (receiving) a media stream
class IStreamReceiver {
public:
    virtual ~IStreamReceiver() = default;

    /// Connect to the given stream URL (e.g. rtmp://...)
    virtual bool connect(const std::string& url) = 0;

    /// Start receiving packets
    virtual bool start() = 0;

    /// Stop receiving packets
    virtual bool stop() = 0;

    /// Receive the next encoded packet (blocking)
    virtual EncodedPacket receivePacket() = 0;

    /// Get stream information (valid after connect)
    virtual StreamInfo getStreamInfo() = 0;
};

} // namespace puller
} // namespace reallive
