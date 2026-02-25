#pragma once

#include "platform/IAudioCapture.h"
#include "platform/ICameraCapture.h"
#include "platform/IEncoder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reallive::stage {

enum class PacketType {
    kUnknown = 0,
    kFrame,
    kDetection,
    kProcessedFrame,
    kEncodedVideo,
    kAudio,
    kCommand,
    kEvent,
};

struct TraceContext {
    uint64_t traceId = 0;
    uint64_t frameId = 0;
    int64_t ingestTsMs = 0;
};

struct FrameBuffer {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;
    std::string pixelFormat = "NV12";
};

struct FramePacket {
    TraceContext trace;
    int64_t ptsUs = 0;
    int64_t tsMs = 0;
    std::shared_ptr<FrameBuffer> frame;
};

struct DetectionBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    double score = 0.0;
};

struct DetectionPacket {
    uint64_t sourceFrameId = 0;
    int64_t tsMs = 0;
    bool valid = false;
    std::vector<DetectionBox> boxes;
};

struct ProcessedFramePacket {
    TraceContext trace;
    int64_t ptsUs = 0;
    int64_t tsMs = 0;
    std::shared_ptr<FrameBuffer> frame;
    DetectionPacket detection;
};

struct EncodedPacketEx {
    TraceContext trace;
    EncodedPacket encoded;
};

struct AudioPacket {
    TraceContext trace;
    AudioFrame audio;
};

} // namespace reallive::stage
