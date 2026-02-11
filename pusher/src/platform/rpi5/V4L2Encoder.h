#pragma once

#include "platform/IEncoder.h"

namespace reallive {

class V4L2Encoder : public IEncoder {
public:
    V4L2Encoder();
    ~V4L2Encoder() override;

    bool init(const EncoderConfig& config) override;
    EncodedPacket encode(const Frame& frame) override;
    void flush() override;
    std::string getName() const override;

private:
    bool setupOutputFormat();   // V4L2 OUTPUT = raw frames input
    bool setupCaptureFormat();  // V4L2 CAPTURE = encoded output
    bool allocateBuffers();
    bool startStreaming();

    int fd_ = -1;
    EncoderConfig config_;
    bool initialized_ = false;

    // V4L2 OUTPUT (raw frame input) buffers
    struct V4L2Buffer {
        void* ptr = nullptr;
        size_t length = 0;
    };
    static constexpr int kOutputBufferCount = 2;
    static constexpr int kCaptureBufferCount = 2;
    V4L2Buffer outputBuffers_[kOutputBufferCount];
    V4L2Buffer captureBuffers_[kCaptureBufferCount];

    int64_t frameCount_ = 0;
};

} // namespace reallive
