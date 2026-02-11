#pragma once

#include "platform/IStorage.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace reallive {
namespace puller {

/// MP4 file storage using FFmpeg's libavformat muxer
class Mp4Storage : public IStorage {
public:
    Mp4Storage();
    ~Mp4Storage() override;

    bool open(const std::string& filepath, const StreamInfo& info) override;
    bool writePacket(const EncodedPacket& packet) override;
    bool writeFrame(const Frame& frame) override;
    void close() override;

private:
    /// Map our CodecType to FFmpeg AVCodecID
    static AVCodecID mapCodecType(CodecType type);

    AVFormatContext* formatCtx_ = nullptr;
    int videoStreamIdx_ = -1;
    int audioStreamIdx_ = -1;
    bool headerWritten_ = false;
    int64_t startPts_ = -1;
};

} // namespace puller
} // namespace reallive
