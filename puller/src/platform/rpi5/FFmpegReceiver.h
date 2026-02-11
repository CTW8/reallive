#pragma once

#include "platform/IStreamReceiver.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace reallive {
namespace puller {

/// FFmpeg-based RTMP stream receiver
class FFmpegReceiver : public IStreamReceiver {
public:
    FFmpegReceiver();
    ~FFmpegReceiver() override;

    bool connect(const std::string& url) override;
    bool start() override;
    bool stop() override;
    EncodedPacket receivePacket() override;
    StreamInfo getStreamInfo() override;

private:
    /// Map FFmpeg codec ID to our CodecType
    static CodecType mapCodecId(AVCodecID id);

    AVFormatContext* formatCtx_ = nullptr;
    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;
    bool connected_ = false;
    std::string url_;
};

} // namespace puller
} // namespace reallive
