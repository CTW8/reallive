#pragma once

#include "core/Config.h"
#include "platform/IEncoder.h"

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

namespace reallive {

class LocalRecorder {
public:
    LocalRecorder();
    ~LocalRecorder();

    bool init(
        const RecordConfig& config,
        const std::string& streamKey,
        const uint8_t* videoExtraData,
        int videoExtraDataSize,
        int width,
        int height
    );

    bool writeVideoPacket(const EncodedPacket& packet);
    void close();
    bool isEnabled() const;

private:
    bool openSegment(int64_t startMs);
    bool rotateIfNeeded(const EncodedPacket& packet, int64_t nowMs);
    bool finalizeCurrentSegment(int64_t endMs);
    void maybeCleanupOldSegments();
    void generateThumbnail(const std::string& mp4Path) const;
    std::vector<std::string> listSegmentsOldestFirst() const;

    std::string makeTempPath(int64_t startMs) const;
    std::string makeFinalPath(int64_t startMs, int64_t endMs) const;
    std::string sanitizeStreamKey(const std::string& raw) const;

    RecordConfig config_;
    std::string streamKey_;
    std::string streamDir_;

    std::vector<uint8_t> videoExtraData_;
    int width_ = 0;
    int height_ = 0;

    AVFormatContext* formatCtx_ = nullptr;
    int videoStreamIdx_ = -1;
    bool headerWritten_ = false;
    bool initialized_ = false;

    int64_t segmentStartWallMs_ = 0;
    int64_t segmentStartPtsUs_ = -1;
    std::string currentTempPath_;
};

} // namespace reallive
