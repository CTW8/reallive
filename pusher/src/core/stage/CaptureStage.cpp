#include "core/stage/CaptureStage.h"

#include <chrono>
#include <iostream>

namespace reallive::stage {

CaptureStage::CaptureStage(CameraCapturePtr camera)
    : camera_(std::move(camera)) {}

CaptureStage::~CaptureStage() {
    requestStop();
    join();
}

std::string CaptureStage::name() const {
    return "CaptureStage";
}

std::vector<PortSpec> CaptureStage::ports() const {
    return {
        PortSpec{"raw_frame", PortDirection::kOutput, PacketType::kFrame},
    };
}

bool CaptureStage::init(const StageInitContext& ctx) {
    if (!ctx.config || !camera_) {
        setState(StageState::kFailed);
        return false;
    }
    config_ = ctx.config;
    if (!camera_->open(config_->camera)) {
        std::cerr << "[CaptureStage] camera open failed" << std::endl;
        setState(StageState::kFailed);
        return false;
    }
    setState(StageState::kInited);
    return true;
}

bool CaptureStage::start() {
    const StageState st = state();
    if (st == StageState::kRunning) return true;
    if (st != StageState::kInited && st != StageState::kStopped) {
        return false;
    }
    if (!camera_ || !camera_->start()) {
        std::cerr << "[CaptureStage] camera start failed" << std::endl;
        setState(StageState::kFailed);
        return false;
    }
    running_ = true;
    worker_ = std::thread(&CaptureStage::runLoop, this);
    setState(StageState::kRunning);
    return true;
}

void CaptureStage::requestStop() {
    const StageState st = state();
    if (st != StageState::kRunning) return;
    setState(StageState::kStopping);
    running_ = false;
}

void CaptureStage::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
    if (camera_) {
        camera_->stop();
    }
    const StageState st = state();
    if (st == StageState::kStopping || st == StageState::kRunning) {
        setState(StageState::kStopped);
    }
}

void CaptureStage::setOutputQueue(const std::shared_ptr<EdgeQueue<FramePacket>>& queue) {
    output_ = queue;
}

CaptureStageStats CaptureStage::stats() const {
    CaptureStageStats s;
    s.captured = captured_.load();
    s.pushed = pushed_.load();
    s.dropped = dropped_.load();
    return s;
}

int64_t CaptureStage::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

int64_t CaptureStage::normalizeTsMs(int64_t ptsUs) {
    if (ptsUs <= 0) return nowMs();
    const int64_t tsMs = ptsUs / 1000;
    if (tsMs <= 0) return nowMs();
    return tsMs;
}

void CaptureStage::runLoop() {
    while (running_.load()) {
        if (!camera_) break;
        Frame frame = camera_->captureFrame();
        if (frame.empty()) {
            continue;
        }

        captured_++;
        auto buffer = std::make_shared<FrameBuffer>();
        buffer->data = std::move(frame.data);
        buffer->width = frame.width;
        buffer->height = frame.height;
        buffer->stride = frame.stride;
        buffer->pixelFormat = frame.pixelFormat.empty() ? "NV12" : frame.pixelFormat;

        FramePacket pkt;
        pkt.trace.frameId = nextFrameId_.fetch_add(1);
        pkt.trace.traceId = pkt.trace.frameId;
        pkt.trace.ingestTsMs = nowMs();
        pkt.ptsUs = frame.pts;
        pkt.tsMs = normalizeTsMs(frame.pts);
        pkt.frame = std::move(buffer);

        if (!output_) {
            dropped_++;
            continue;
        }

        const bool ok = output_->push(std::move(pkt), std::chrono::milliseconds{1});
        if (ok) {
            pushed_++;
        } else {
            dropped_++;
        }
    }
}

} // namespace reallive::stage
