#pragma once

#include "IStreamReceiver.h"

namespace reallive {
namespace puller {

/// Pixel format enumeration
enum class PixelFormat {
    YUV420P,
    NV12,
    NV21,
    RGB24,
    BGR24,
    Unknown
};

/// Decoded video/audio frame
struct Frame {
    MediaType type = MediaType::Unknown;
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    int64_t pts = 0;

    // Audio-specific
    int sampleRate = 0;
    int channels = 0;
    int samples = 0;

    bool empty() const { return data.empty(); }
};

/// Abstract interface for decoding encoded packets into raw frames
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /// Initialize the decoder with stream information
    virtual bool init(const StreamInfo& info) = 0;

    /// Decode a single encoded packet into a frame
    virtual Frame decode(const EncodedPacket& packet) = 0;

    /// Flush any buffered frames
    virtual void flush() = 0;
};

} // namespace puller
} // namespace reallive
