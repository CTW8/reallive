#pragma once

#include "platform/IStreamer.h"
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace reallive {

class RtmpStreamer : public IStreamer {
public:
    RtmpStreamer();
    ~RtmpStreamer() override;

    bool connect(const StreamConfig& config) override;
    bool sendVideoPacket(const EncodedPacket& packet) override;
    bool sendAudioPacket(const AudioFrame& frame) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string getName() const override;

private:
    AVFormatContext* formatCtx_ = nullptr;
    int videoStreamIdx_ = -1;
    int audioStreamIdx_ = -1;
    bool connected_ = false;
    bool headerWritten_ = false;
    bool audioEnabled_ = false;

    int64_t videoStartPts_ = -1;
    int64_t audioStartPts_ = -1;
    std::mutex writeMutex_;
};

} // namespace reallive
