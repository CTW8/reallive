#include "core/stage/EncodeStage.h"

#include <chrono>
#include <utility>
#include <algorithm>

namespace reallive::stage {

EncodeStage::EncodeStage(EncoderPtr encoder)
    : encoder_(std::move(encoder)) {}

EncodeStage::~EncodeStage() {
    requestStop();
    join();
}

std::string EncodeStage::name() const {
    return "EncodeStage";
}

std::vector<PortSpec> EncodeStage::ports() const {
    return {
        PortSpec{"processed_frame", PortDirection::kInput, PacketType::kProcessedFrame},
        PortSpec{"encoded_video", PortDirection::kOutput, PacketType::kEncodedVideo},
    };
}

bool EncodeStage::init(const StageInitContext& ctx) {
    if (!ctx.config || !encoder_) {
        setState(StageState::kFailed);
        return false;
    }
    config_ = ctx.config;
    if (!encoder_->init(config_->encoder)) {
        setState(StageState::kFailed);
        return false;
    }
    runtimeTargetFps_ = std::max(1, config_->encoder.fps);
    runtimeTargetBitrateKbps_ = std::max(100, config_->encoder.bitrate / 1000);
    nextEncodeDueMs_ = 0;
    bitrateWindowStartMs_ = 0;
    bitrateWindowBytes_ = 0;
    setState(StageState::kInited);
    return true;
}

bool EncodeStage::start() {
    const StageState st = state();
    if (st == StageState::kRunning) return true;
    if (st != StageState::kInited && st != StageState::kStopped) {
        return false;
    }
    running_ = true;
    worker_ = std::thread(&EncodeStage::runLoop, this);
    setState(StageState::kRunning);
    return true;
}

void EncodeStage::requestStop() {
    const StageState st = state();
    if (st != StageState::kRunning) return;
    setState(StageState::kStopping);
    running_ = false;
    if (input_) input_->close();
}

void EncodeStage::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
    if (encoder_) {
        encoder_->flush();
    }
    const StageState st = state();
    if (st == StageState::kStopping || st == StageState::kRunning) {
        setState(StageState::kStopped);
    }
}

void EncodeStage::setInputQueue(const std::shared_ptr<EdgeQueue<ProcessedFramePacket>>& queue) {
    input_ = queue;
}

void EncodeStage::setOutputQueue(const std::shared_ptr<EdgeQueue<EncodedPacketEx>>& queue) {
    output_ = queue;
}

void EncodeStage::setRuntimeTarget(int fps, int bitrateKbps) {
    runtimeTargetFps_ = std::max(1, fps);
    runtimeTargetBitrateKbps_ = std::max(100, bitrateKbps);
}

void EncodeStage::getRuntimeTarget(int& fps, int& bitrateKbps) const {
    fps = runtimeTargetFps_.load();
    bitrateKbps = runtimeTargetBitrateKbps_.load();
}

void EncodeStage::runLoop() {
    while (running_.load()) {
        if (!input_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        ProcessedFramePacket in;
        const bool ok = input_->pop(in, std::chrono::milliseconds{100});
        if (!ok) {
            continue;
        }
        if (!in.frame) {
            continue;
        }

        const int targetFps = std::max(1, runtimeTargetFps_.load());
        const int64_t frameTsMs = in.tsMs > 0 ? in.tsMs : (in.ptsUs > 0 ? in.ptsUs / 1000 : 0);
        if (frameTsMs > 0) {
            const int64_t minIntervalMs = std::max<int64_t>(1, 1000 / targetFps);
            int64_t due = nextEncodeDueMs_.load();
            if (due <= 0) {
                nextEncodeDueMs_ = frameTsMs;
                due = frameTsMs;
            }
            if (frameTsMs < due) {
                continue;
            }
            nextEncodeDueMs_ = due + minIntervalMs;
            if (frameTsMs - nextEncodeDueMs_.load() > minIntervalMs * 3) {
                nextEncodeDueMs_ = frameTsMs + minIntervalMs;
            }
        }

        Frame raw;
        raw.data = in.frame->data;
        raw.width = in.frame->width;
        raw.height = in.frame->height;
        raw.stride = in.frame->stride;
        raw.pixelFormat = in.frame->pixelFormat;
        raw.pts = in.ptsUs;

        EncodedPacket encoded = encoder_->encode(raw);
        if (encoded.empty()) {
            continue;
        }

        const int bitrateKbps = std::max(100, runtimeTargetBitrateKbps_.load());
        const uint64_t budgetBytesPerSec = static_cast<uint64_t>(bitrateKbps) * 1000ull / 8ull;
        const int64_t nowMs = frameTsMs > 0 ? frameTsMs : (in.trace.ingestTsMs > 0 ? in.trace.ingestTsMs : 0);
        int64_t windowStart = bitrateWindowStartMs_.load();
        if (windowStart <= 0 || nowMs <= 0 || nowMs - windowStart >= 1000) {
            bitrateWindowStartMs_ = nowMs > 0 ? nowMs : windowStart;
            bitrateWindowBytes_ = 0;
            windowStart = bitrateWindowStartMs_.load();
        }
        (void)windowStart;

        uint64_t usedBytes = bitrateWindowBytes_.load();
        const uint64_t pktBytes = static_cast<uint64_t>(encoded.data.size());
        if (usedBytes + pktBytes > budgetBytesPerSec && !encoded.isKeyframe) {
            continue;
        }
        bitrateWindowBytes_ = usedBytes + pktBytes;

        EncodedPacketEx out;
        out.trace = in.trace;
        out.encoded = std::move(encoded);
        if (output_) {
            output_->push(std::move(out), std::chrono::milliseconds{1});
        }
    }
}

} // namespace reallive::stage
