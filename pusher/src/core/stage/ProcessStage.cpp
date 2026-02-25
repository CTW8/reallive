#include "core/stage/ProcessStage.h"
#include "core/TextOverlay.h"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace reallive::stage {

namespace {

void flipNv12Horizontal(std::vector<uint8_t>& data, int width, int height) {
    if (width <= 1 || height <= 1) return;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvSize = ySize / 2;
    if (data.size() < ySize + uvSize) return;
    uint8_t* y = data.data();
    uint8_t* uv = data.data() + ySize;

    for (int row = 0; row < height; ++row) {
        uint8_t* line = y + static_cast<size_t>(row) * static_cast<size_t>(width);
        for (int l = 0, r = width - 1; l < r; ++l, --r) {
            std::swap(line[l], line[r]);
        }
    }

    for (int row = 0; row < height / 2; ++row) {
        uint8_t* line = uv + static_cast<size_t>(row) * static_cast<size_t>(width);
        for (int l = 0, r = width - 2; l < r; l += 2, r -= 2) {
            std::swap(line[l], line[r]);
            std::swap(line[l + 1], line[r + 1]);
        }
    }
}

void flipNv12Vertical(std::vector<uint8_t>& data, int width, int height) {
    if (width <= 1 || height <= 1) return;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvSize = ySize / 2;
    if (data.size() < ySize + uvSize) return;
    uint8_t* y = data.data();
    uint8_t* uv = data.data() + ySize;

    std::vector<uint8_t> tmpRow(static_cast<size_t>(width), 0);
    for (int t = 0, b = height - 1; t < b; ++t, --b) {
        uint8_t* top = y + static_cast<size_t>(t) * static_cast<size_t>(width);
        uint8_t* bottom = y + static_cast<size_t>(b) * static_cast<size_t>(width);
        std::memcpy(tmpRow.data(), top, static_cast<size_t>(width));
        std::memcpy(top, bottom, static_cast<size_t>(width));
        std::memcpy(bottom, tmpRow.data(), static_cast<size_t>(width));
    }

    for (int t = 0, b = (height / 2) - 1; t < b; ++t, --b) {
        uint8_t* top = uv + static_cast<size_t>(t) * static_cast<size_t>(width);
        uint8_t* bottom = uv + static_cast<size_t>(b) * static_cast<size_t>(width);
        std::memcpy(tmpRow.data(), top, static_cast<size_t>(width));
        std::memcpy(top, bottom, static_cast<size_t>(width));
        std::memcpy(bottom, tmpRow.data(), static_cast<size_t>(width));
    }
}

void applyNightVisionNv12(std::vector<uint8_t>& data, int width, int height) {
    if (width <= 1 || height <= 1) return;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvSize = ySize / 2;
    if (data.size() < ySize + uvSize) return;
    uint8_t* y = data.data();
    uint8_t* uv = data.data() + ySize;

    for (size_t i = 0; i < ySize; ++i) {
        int v = static_cast<int>(y[i]);
        v = 12 + (v * 11) / 10;
        if (v > 255) v = 255;
        y[i] = static_cast<uint8_t>(v);
    }

    for (size_t i = 0; i + 1 < uvSize; i += 2) {
        uv[i] = 96;
        uv[i + 1] = 140;
    }
}

} // namespace

ProcessStage::~ProcessStage() {
    requestStop();
    join();
}

std::string ProcessStage::name() const {
    return "ProcessStage";
}

std::vector<PortSpec> ProcessStage::ports() const {
    return {
        PortSpec{"raw_frame", PortDirection::kInput, PacketType::kFrame},
        PortSpec{"detection_meta", PortDirection::kInput, PacketType::kDetection},
        PortSpec{"processed_frame", PortDirection::kOutput, PacketType::kProcessedFrame},
    };
}

bool ProcessStage::init(const StageInitContext& ctx) {
    if (!ctx.config) {
        setState(StageState::kFailed);
        return false;
    }
    config_ = ctx.config;
    setState(StageState::kInited);
    return true;
}

bool ProcessStage::start() {
    const StageState st = state();
    if (st == StageState::kRunning) return true;
    if (st != StageState::kInited && st != StageState::kStopped) {
        return false;
    }
    running_ = true;
    worker_ = std::thread(&ProcessStage::runLoop, this);
    setState(StageState::kRunning);
    return true;
}

void ProcessStage::requestStop() {
    const StageState st = state();
    if (st != StageState::kRunning) return;
    setState(StageState::kStopping);
    running_ = false;
    if (frameInput_) frameInput_->close();
}

void ProcessStage::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
    const StageState st = state();
    if (st == StageState::kStopping || st == StageState::kRunning) {
        setState(StageState::kStopped);
    }
}

void ProcessStage::setFrameInputQueue(const std::shared_ptr<EdgeQueue<FramePacket>>& queue) {
    frameInput_ = queue;
}

void ProcessStage::setDetectionInputQueue(const std::shared_ptr<EdgeQueue<DetectionPacket>>& queue) {
    detectionInput_ = queue;
}

void ProcessStage::setOutputQueue(const std::shared_ptr<EdgeQueue<ProcessedFramePacket>>& queue) {
    output_ = queue;
}

void ProcessStage::setRuntimeDetectionOptions(bool personEnabled, bool drawOverlay) {
    runtimePersonEnabled_ = personEnabled;
    runtimeDrawOverlay_ = drawOverlay;
}

void ProcessStage::setRuntimeVisualOptions(int imageFlipMode, bool nightVisionEnabled, int nightVisionMode) {
    runtimeImageFlipMode_ = std::max(0, std::min(3, imageFlipMode));
    runtimeNightVisionEnabled_ = nightVisionEnabled;
    runtimeNightVisionMode_ = std::max(0, std::min(2, nightVisionMode));
}

void ProcessStage::setRuntimeWatermarkEnabled(bool enabled) {
    runtimeWatermarkEnabled_ = enabled;
}

void ProcessStage::runLoop() {
    DetectionPacket latestDet;
    bool hasDet = false;
    constexpr int64_t kFreshMs = 160;

    while (running_.load()) {
        if (!frameInput_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (detectionInput_) {
            DetectionPacket det;
            while (detectionInput_->pop(det, std::chrono::milliseconds{0})) {
                latestDet = std::move(det);
                hasDet = true;
            }
        }

        FramePacket frame;
        const bool ok = frameInput_->pop(frame, std::chrono::milliseconds{100});
        if (!ok) {
            continue;
        }

        ProcessedFramePacket out;
        out.trace = frame.trace;
        out.ptsUs = frame.ptsUs;
        out.tsMs = frame.tsMs;
        out.frame = frame.frame;
        if (out.frame) {
            const int flipMode = runtimeImageFlipMode_.load();
            if (flipMode == 1 || flipMode == 3) {
                flipNv12Horizontal(out.frame->data, out.frame->width, out.frame->height);
            }
            if (flipMode == 2 || flipMode == 3) {
                flipNv12Vertical(out.frame->data, out.frame->width, out.frame->height);
            }
            const bool nightEnabled = runtimeNightVisionEnabled_.load();
            const int nightMode = runtimeNightVisionMode_.load();
            if (nightEnabled && nightMode != 2) {
                applyNightVisionNv12(out.frame->data, out.frame->width, out.frame->height);
            }
        }
        if (hasDet && std::llabs(out.tsMs - latestDet.tsMs) <= kFreshMs) {
            out.detection = latestDet;
            if (runtimePersonEnabled_.load() && runtimeDrawOverlay_.load() && out.frame && !out.detection.boxes.empty()) {
                const auto& box = out.detection.boxes.front();
                TextOverlay::drawBoundingBox(
                    out.frame->data.data(),
                    out.frame->width,
                    out.frame->height,
                    box.x,
                    box.y,
                    box.w,
                    box.h
                );
            }
        }
        if (out.frame && runtimeWatermarkEnabled_.load()) {
            TextOverlay::drawTimestamp(
                out.frame->data.data(),
                out.frame->width,
                out.frame->height
            );
        }

        if (output_) {
            output_->push(std::move(out), std::chrono::milliseconds{1});
        }
    }
}

} // namespace reallive::stage
