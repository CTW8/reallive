#pragma once

#include "platform/IEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <vector>

namespace reallive {

class AvcodecEncoder : public IEncoder {
public:
    AvcodecEncoder();
    ~AvcodecEncoder() override;

    bool init(const EncoderConfig& config) override;
    EncodedPacket encode(const Frame& frame) override;
    void flush() override;
    std::string getName() const override;

    // Extra data (SPS/PPS) needed by muxer before writing header
    const uint8_t* getExtraData() const;
    int getExtraDataSize() const;

private:
    const AVCodec* codec_ = nullptr;
    AVCodecContext* ctx_ = nullptr;
    AVFrame* avFrame_ = nullptr;
    AVPacket* avPacket_ = nullptr;

    EncoderConfig config_;
    bool initialized_ = false;
    int64_t frameCount_ = 0;
    std::string encoderName_;
};

} // namespace reallive
