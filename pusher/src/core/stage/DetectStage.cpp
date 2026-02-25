#include "core/stage/DetectStage.h"

#include <chrono>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace reallive::stage {

DetectStage::~DetectStage() {
    requestStop();
    join();
}

std::string DetectStage::name() const {
    return "DetectStage";
}

std::vector<PortSpec> DetectStage::ports() const {
    return {
        PortSpec{"raw_frame", PortDirection::kInput, PacketType::kFrame},
        PortSpec{"detection_meta", PortDirection::kOutput, PacketType::kDetection},
    };
}

bool DetectStage::init(const StageInitContext& ctx) {
    if (!ctx.config) {
        setState(StageState::kFailed);
        return false;
    }
    config_ = ctx.config;
    setState(StageState::kInited);
    return true;
}

bool DetectStage::start() {
    const StageState st = state();
    if (st == StageState::kRunning) return true;
    if (st != StageState::kInited && st != StageState::kStopped) {
        return false;
    }
    running_ = true;
    worker_ = std::thread(&DetectStage::runLoop, this);
    setState(StageState::kRunning);
    return true;
}

void DetectStage::requestStop() {
    const StageState st = state();
    if (st != StageState::kRunning) return;
    setState(StageState::kStopping);
    running_ = false;
    if (input_) input_->close();
}

void DetectStage::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
    const StageState st = state();
    if (st == StageState::kStopping || st == StageState::kRunning) {
        setState(StageState::kStopped);
    }
}

void DetectStage::setInputQueue(const std::shared_ptr<EdgeQueue<FramePacket>>& queue) {
    input_ = queue;
}

void DetectStage::setOutputQueue(const std::shared_ptr<EdgeQueue<DetectionPacket>>& queue) {
    output_ = queue;
}

void DetectStage::setRuntimeDetectionEnabled(bool motionEnabled, bool personEnabled) {
    runtimeMotionEnabled_ = motionEnabled;
    runtimePersonEnabled_ = personEnabled;
}

void DetectStage::runLoop() {
    if (config_ && !config_->detection.enabled) {
        std::cout << "[DetectStage] detection disabled by config" << std::endl;
    }

    while (running_.load()) {
        if (!input_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        FramePacket frame;
        const bool ok = input_->pop(frame, std::chrono::milliseconds{100});
        if (!ok) {
            continue;
        }

        DetectionPacket det;
        det.sourceFrameId = frame.trace.frameId;
        det.tsMs = frame.tsMs;
        det.valid = detectMotion(frame, det);

        if (output_) {
            output_->push(std::move(det), std::chrono::milliseconds{1});
        }
    }
}

bool DetectStage::detectMotion(const FramePacket& frame, DetectionPacket& det) {
    if (!config_ || !config_->detection.enabled || !frame.frame) {
        return false;
    }
    if (!runtimeMotionEnabled_.load() && !runtimePersonEnabled_.load()) {
        return false;
    }
    const auto& fb = *frame.frame;
    if (fb.data.empty() || fb.width <= 0 || fb.height <= 0) {
        return false;
    }
    const int sampleW = std::max(64, std::min(240, fb.width / 6));
    const int sampleH = std::max(36, std::min(160, fb.height / 6));
    if (sampleW <= 0 || sampleH <= 0) return false;

    const size_t sampleSize = static_cast<size_t>(sampleW) * static_cast<size_t>(sampleH);
    if (prevLuma_.size() != sampleSize) {
        prevLuma_.assign(sampleSize, 0);
        hasPrev_ = false;
    }

    const uint8_t* yPlane = fb.data.data();
    int changed = 0;
    int minX = sampleW;
    int minY = sampleH;
    int maxX = -1;
    int maxY = -1;

    const int diffThreshold = std::max(1, config_->detection.diffThreshold);
    for (int sy = 0; sy < sampleH; sy++) {
        const int srcY = std::min(fb.height - 1, (sy * fb.height) / sampleH);
        for (int sx = 0; sx < sampleW; sx++) {
            const int srcX = std::min(fb.width - 1, (sx * fb.width) / sampleW);
            const uint8_t cur = yPlane[srcY * fb.width + srcX];
            const size_t idx = static_cast<size_t>(sy) * static_cast<size_t>(sampleW) + static_cast<size_t>(sx);
            const uint8_t prev = prevLuma_[idx];
            prevLuma_[idx] = cur;
            if (!hasPrev_) continue;
            if (std::abs(static_cast<int>(cur) - static_cast<int>(prev)) < diffThreshold) continue;

            changed++;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }
    }
    hasPrev_ = true;

    if (changed <= 0 || maxX < minX || maxY < minY) return false;

    const double ratio = static_cast<double>(changed) / static_cast<double>(sampleW * sampleH);
    if (ratio < config_->detection.motionRatioThreshold) return false;

    DetectionBox box;
    box.x = (minX * fb.width) / sampleW;
    box.y = (minY * fb.height) / sampleH;
    box.w = std::max(2, ((maxX + 1) * fb.width) / sampleW - box.x);
    box.h = std::max(2, ((maxY + 1) * fb.height) / sampleH - box.y);
    box.score = std::max(0.0, std::min(1.0, ratio * 3.0));

    const double areaRatio = static_cast<double>(box.w) * static_cast<double>(box.h) /
                             static_cast<double>(fb.width * fb.height);
    if (areaRatio < config_->detection.minBoxAreaRatio) return false;

    det.boxes.clear();
    det.boxes.push_back(box);
    return true;
}

} // namespace reallive::stage
