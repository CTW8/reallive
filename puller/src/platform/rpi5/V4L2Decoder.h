#pragma once

#include "platform/IDecoder.h"

#include <string>

namespace reallive {
namespace puller {

/// V4L2 Memory-to-Memory hardware decoder for Raspberry Pi 5
/// Uses the V4L2 stateful decoder API for H.264/H.265 hardware decoding
class V4L2Decoder : public IDecoder {
public:
    V4L2Decoder();
    ~V4L2Decoder() override;

    bool init(const StreamInfo& info) override;
    Frame decode(const EncodedPacket& packet) override;
    void flush() override;

private:
    /// Open the V4L2 M2M device
    bool openDevice(const std::string& devicePath);

    /// Set up OUTPUT queue (compressed data in)
    bool setupOutputQueue();

    /// Set up CAPTURE queue (decoded frames out)
    bool setupCaptureQueue();

    /// Submit a compressed buffer to the decoder
    bool enqueueOutput(const EncodedPacket& packet);

    /// Dequeue a decoded frame from the decoder
    Frame dequeueCaptureFrame();

    int fd_ = -1;
    int width_ = 0;
    int height_ = 0;
    CodecType codec_ = CodecType::Unknown;
    bool initialized_ = false;

    // Buffer tracking
    static constexpr int OUTPUT_BUFFER_COUNT = 4;
    static constexpr int CAPTURE_BUFFER_COUNT = 4;

    struct MappedBuffer {
        void* start = nullptr;
        size_t length = 0;
    };

    MappedBuffer outputBuffers_[OUTPUT_BUFFER_COUNT] = {};
    MappedBuffer captureBuffers_[CAPTURE_BUFFER_COUNT] = {};
    int outputBufferIndex_ = 0;
};

} // namespace puller
} // namespace reallive
