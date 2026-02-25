#include "core/stage/StagePipeline.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <limits>
#include <utility>

namespace reallive::stage {

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

struct StreamLevelConfig {
    int fps = 24;
    int bitrateKbps = 2000;
};

int resolveOutputFps(int requestedCameraFps, int fallbackEncoderFps) {
    const int fromCamera = requestedCameraFps > 0 ? requestedCameraFps : 0;
    const int fromEncoder = fallbackEncoderFps > 0 ? fallbackEncoderFps : 0;
    const int base = std::max(fromCamera, fromEncoder);
    return std::max(24, base > 0 ? base : 25);
}

StreamLevelConfig levelConfig(int level, int outputFps) {
    const int safe = std::max(0, std::min(4, level));
    const int fps = std::max(24, outputFps);
    switch (safe) {
    case 0: return {fps, 700};
    case 1: return {fps, 1200};
    case 2: return {fps, 2000};
    case 3: return {fps, 2800};
    default: return {fps, 4000};
    }
}

} // namespace

StagePipeline::~StagePipeline() {
    stop();
}

bool StagePipeline::init(
    const PusherConfig& config,
    CameraCapturePtr camera,
    EncoderPtr encoder,
    StreamerPtr streamer,
    AudioCapturePtr audio
) {
    if (!camera || !encoder || !streamer) {
        return false;
    }

    config_ = config;
    config_.stream.enableAudio = config_.enableAudio;
    runtimeTargetFps_ = std::max(1, config_.encoder.fps);
    runtimeTargetBitrateKbps_ = std::max(100, config_.encoder.bitrate / 1000);
    captureStage_ = std::make_shared<CaptureStage>(std::move(camera));
    if (config_.enableAudio && audio) {
        audioStage_ = std::make_shared<AudioStage>(std::move(audio));
    } else {
        audioStage_.reset();
    }
    detectStage_ = std::make_shared<DetectStage>();
    processStage_ = std::make_shared<ProcessStage>();
    encodeStage_ = std::make_shared<EncodeStage>(std::move(encoder));
    outputStage_ = std::make_shared<OutputStage>(std::move(streamer));

    if (!buildGraph()) {
        return false;
    }

    StageInitContext ctx;
    ctx.config = &config_;
    inited_ = graph_.initAll(ctx);
    if (inited_) {
        if (detectStage_) {
            detectStage_->setRuntimeDetectionEnabled(runtimeMotionEnabled_, runtimePersonEnabled_);
        }
        if (processStage_) {
            processStage_->setRuntimeDetectionOptions(runtimePersonEnabled_, config_.detection.drawOverlay);
            processStage_->setRuntimeVisualOptions(runtimeImageFlipMode_, runtimeNightVisionEnabled_, runtimeNightVisionMode_);
            processStage_->setRuntimeWatermarkEnabled(runtimeWatermarkEnabled_);
        }
        if (encodeStage_) {
            encodeStage_->setRuntimeTarget(runtimeTargetFps_, runtimeTargetBitrateKbps_);
        }
    }
    running_ = false;
    return inited_;
}

bool StagePipeline::start() {
    if (!inited_) return false;
    if (running_) return true;

    if (!graph_.startAll()) {
        graph_.requestStopAll();
        graph_.joinAll();
        running_ = false;
        return false;
    }

    running_ = true;
    startAdaptationLoop();
    return true;
}

void StagePipeline::stop() {
    if (!inited_) return;
    stopAdaptationLoop();
    graph_.requestStopAll();
    graph_.joinAll();
    running_ = false;
}

bool StagePipeline::isRunning() const {
    return running_;
}

bool StagePipeline::setLiveEnabled(bool enabled) {
    if (!outputStage_) return false;
    return outputStage_->setLiveEnabled(enabled);
}

bool StagePipeline::isLiveEnabled() const {
    if (!outputStage_) return false;
    return outputStage_->isLiveEnabled();
}

bool StagePipeline::isLiveActive() const {
    if (!outputStage_) return false;
    return outputStage_->isLiveActive();
}

uint64_t StagePipeline::framesSent() const {
    if (!outputStage_) return 0;
    return outputStage_->framesSent();
}

uint64_t StagePipeline::bytesSent() const {
    if (!outputStage_) return 0;
    return outputStage_->bytesSent();
}

double StagePipeline::currentFps() const {
    if (!outputStage_) return 0.0;
    return outputStage_->currentFps();
}

bool StagePipeline::setRecordCleanupPolicy(int minFreePercent, int targetFreePercent) {
    if (!outputStage_) return false;
    return outputStage_->setRecordCleanupPolicy(minFreePercent, targetFreePercent);
}

bool StagePipeline::getRecordCleanupPolicy(int& minFreePercent, int& targetFreePercent) const {
    if (!outputStage_) {
        minFreePercent = config_.record.minFreePercent;
        targetFreePercent = config_.record.targetFreePercent;
        return false;
    }
    return outputStage_->getRecordCleanupPolicy(minFreePercent, targetFreePercent);
}

bool StagePipeline::applyRuntimeSettings(
    bool motionEnabled,
    bool personEnabled,
    bool soundEnabled,
    const std::string& motionSensitivity,
    const std::string& soundSensitivity,
    const std::string& detectionZones,
    bool watermarkEnabled
) {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    runtimeMotionEnabled_ = motionEnabled;
    runtimePersonEnabled_ = personEnabled;
    runtimeSoundEnabled_ = soundEnabled;
    runtimeMotionSensitivity_ = motionSensitivity.empty() ? "High" : motionSensitivity;
    runtimeSoundSensitivity_ = soundSensitivity.empty() ? "Loud" : soundSensitivity;
    runtimeDetectionZones_ = detectionZones.empty() ? "2 zones configured" : detectionZones;
    runtimeWatermarkEnabled_ = watermarkEnabled;
    if (detectStage_) {
        detectStage_->setRuntimeDetectionEnabled(runtimeMotionEnabled_, runtimePersonEnabled_);
    }
    if (processStage_) {
        processStage_->setRuntimeDetectionOptions(runtimePersonEnabled_, config_.detection.drawOverlay);
        processStage_->setRuntimeWatermarkEnabled(runtimeWatermarkEnabled_);
    }
    return true;
}

void StagePipeline::getRuntimeSettings(
    bool& motionEnabled,
    bool& personEnabled,
    bool& soundEnabled,
    std::string& motionSensitivity,
    std::string& soundSensitivity,
    std::string& detectionZones,
    bool& watermarkEnabled
) const {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    motionEnabled = runtimeMotionEnabled_;
    personEnabled = runtimePersonEnabled_;
    soundEnabled = runtimeSoundEnabled_;
    motionSensitivity = runtimeMotionSensitivity_;
    soundSensitivity = runtimeSoundSensitivity_;
    detectionZones = runtimeDetectionZones_;
    watermarkEnabled = runtimeWatermarkEnabled_;
}

bool StagePipeline::applyRuntimeVisualSettings(int imageFlipMode, bool nightVisionEnabled, int nightVisionMode) {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    runtimeImageFlipMode_ = std::max(0, std::min(3, imageFlipMode));
    runtimeNightVisionEnabled_ = nightVisionEnabled;
    runtimeNightVisionMode_ = std::max(0, std::min(2, nightVisionMode));
    if (processStage_) {
        processStage_->setRuntimeVisualOptions(
            runtimeImageFlipMode_,
            runtimeNightVisionEnabled_,
            runtimeNightVisionMode_
        );
    }
    return true;
}

void StagePipeline::getRuntimeVisualSettings(int& imageFlipMode, bool& nightVisionEnabled, int& nightVisionMode) const {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    imageFlipMode = runtimeImageFlipMode_;
    nightVisionEnabled = runtimeNightVisionEnabled_;
    nightVisionMode = runtimeNightVisionMode_;
}

bool StagePipeline::applyRuntimeStreamPolicy(
    const std::string& streamProfile,
    const std::string& streamMode,
    int manualLevel,
    int autoMinLevel,
    int autoMaxLevel,
    const std::string& autoPolicy,
    int autoCooldownSec,
    int autoUpHoldSec,
    int autoDownHoldSec
) {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    auto normalizeProfile = [](const std::string& raw) {
        std::string p = raw;
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (p == "360p" || p == "540p" || p == "720p" || p == "1080p" || p == "auto") return p;
        return std::string("auto");
    };
    auto profileToLevel = [](const std::string& profile) {
        if (profile == "360p") return 0;
        if (profile == "540p") return 1;
        if (profile == "720p") return 2;
        if (profile == "1080p") return 4;
        return 2;
    };

    runtimeStreamProfile_ = normalizeProfile(streamProfile);
    const bool profileManual = runtimeStreamProfile_ != "auto";
    runtimeStreamMode_ = profileManual ? "manual" : (streamMode == "manual" ? "manual" : "auto");
    runtimeManualLevel_ = profileManual ? profileToLevel(runtimeStreamProfile_) : std::max(0, std::min(4, manualLevel));
    runtimeAutoMinLevel_ = std::max(0, std::min(4, std::min(autoMinLevel, autoMaxLevel)));
    runtimeAutoMaxLevel_ = std::max(0, std::min(4, std::max(autoMinLevel, autoMaxLevel)));
    runtimeAutoPolicy_ = autoPolicy.empty() ? "balanced" : autoPolicy;
    runtimeAutoCooldownSec_ = std::max(3, std::min(120, autoCooldownSec));
    runtimeAutoUpHoldSec_ = std::max(5, std::min(180, autoUpHoldSec));
    runtimeAutoDownHoldSec_ = std::max(1, std::min(60, autoDownHoldSec));
    if (runtimeStreamMode_ == "manual") {
        runtimeCurrentLevel_ = runtimeManualLevel_;
    } else {
        if (runtimeCurrentLevel_ < runtimeAutoMinLevel_) runtimeCurrentLevel_ = runtimeAutoMinLevel_;
        if (runtimeCurrentLevel_ > runtimeAutoMaxLevel_) runtimeCurrentLevel_ = runtimeAutoMaxLevel_;
    }

    const int outputFps = resolveOutputFps(config_.camera.fps, config_.encoder.fps);
    const auto cfg = levelConfig(runtimeCurrentLevel_, outputFps);
    runtimeTargetFps_ = cfg.fps;
    runtimeTargetBitrateKbps_ = cfg.bitrateKbps;
    runtimeLastSwitchMs_ = nowMs();
    runtimeAdaptBadSinceMs_ = 0;
    runtimeAdaptGoodSinceMs_ = 0;
    if (encodeStage_) {
        encodeStage_->setRuntimeTarget(runtimeTargetFps_, runtimeTargetBitrateKbps_);
    }
    return true;
}

void StagePipeline::getRuntimeStreamPolicy(
    std::string& streamProfile,
    std::string& streamMode,
    int& manualLevel,
    int& autoMinLevel,
    int& autoMaxLevel,
    std::string& autoPolicy,
    int& autoCooldownSec,
    int& autoUpHoldSec,
    int& autoDownHoldSec,
    int& currentLevel,
    int& targetFps,
    int& targetBitrateKbps
) const {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    streamProfile = runtimeStreamProfile_;
    streamMode = runtimeStreamMode_;
    manualLevel = runtimeManualLevel_;
    autoMinLevel = runtimeAutoMinLevel_;
    autoMaxLevel = runtimeAutoMaxLevel_;
    autoPolicy = runtimeAutoPolicy_;
    autoCooldownSec = runtimeAutoCooldownSec_;
    autoUpHoldSec = runtimeAutoUpHoldSec_;
    autoDownHoldSec = runtimeAutoDownHoldSec_;
    currentLevel = runtimeCurrentLevel_;
    targetFps = runtimeTargetFps_;
    targetBitrateKbps = runtimeTargetBitrateKbps_;
}

bool StagePipeline::applyRuntimePtzCommand(
    const std::string& action,
    int speed,
    int zoomStep,
    int zoomLevel,
    const std::string& preset
) {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    runtimePtzAction_ = action.empty() ? "stop" : action;
    runtimePtzSpeed_ = std::max(1, std::min(10, speed));
    runtimePtzZoomStep_ = std::max(1, std::min(10, zoomStep));
    if (runtimePtzAction_ == "zoom_in") {
        runtimePtzZoomLevel_ = std::max(0, std::min(100, runtimePtzZoomLevel_ + runtimePtzZoomStep_ * 5));
    } else if (runtimePtzAction_ == "zoom_out") {
        runtimePtzZoomLevel_ = std::max(0, std::min(100, runtimePtzZoomLevel_ - runtimePtzZoomStep_ * 5));
    } else if (runtimePtzAction_ == "zoom_set") {
        runtimePtzZoomLevel_ = std::max(0, std::min(100, zoomLevel));
    } else if (zoomLevel >= 0) {
        runtimePtzZoomLevel_ = std::max(0, std::min(100, zoomLevel));
    }
    runtimePtzPreset_ = preset;
    runtimePtzUpdatedAtMs_ = nowMs();
    return true;
}

void StagePipeline::getRuntimePtzState(
    std::string& action,
    int& speed,
    int& zoomStep,
    int& zoomLevel,
    std::string& preset,
    int64_t& updatedAtMs
) const {
    std::lock_guard<std::mutex> lock(runtimeMutex_);
    action = runtimePtzAction_;
    speed = runtimePtzSpeed_;
    zoomStep = runtimePtzZoomStep_;
    zoomLevel = runtimePtzZoomLevel_;
    preset = runtimePtzPreset_;
    updatedAtMs = runtimePtzUpdatedAtMs_;
}

bool StagePipeline::buildGraph() {
    if (!captureStage_ || !detectStage_ || !processStage_ || !encodeStage_ || !outputStage_) {
        return false;
    }

    graph_ = StageGraph{};
    const NodeId capture = graph_.addStage(captureStage_);
    NodeId audio = 0;
    if (audioStage_) {
        audio = graph_.addStage(audioStage_);
    }
    const NodeId detect = graph_.addStage(detectStage_);
    const NodeId process = graph_.addStage(processStage_);
    const NodeId encode = graph_.addStage(encodeStage_);
    const NodeId output = graph_.addStage(outputStage_);
    if (!capture || !detect || !process || !encode || !output) {
        return false;
    }

    if (!graph_.connect(capture, "raw_frame", detect, "raw_frame", EdgeOptions{1, QueuePolicy::kDropOldest})) {
        std::cerr << "[StagePipeline] connect capture->detect failed" << std::endl;
        return false;
    }
    if (!graph_.connect(capture, "raw_frame", process, "raw_frame", EdgeOptions{2, QueuePolicy::kDropOldest})) {
        std::cerr << "[StagePipeline] connect capture->process failed" << std::endl;
        return false;
    }
    if (!graph_.connect(detect, "detection_meta", process, "detection_meta", EdgeOptions{1, QueuePolicy::kLatestOnly})) {
        std::cerr << "[StagePipeline] connect detect->process failed" << std::endl;
        return false;
    }
    if (!graph_.connect(process, "processed_frame", encode, "processed_frame", EdgeOptions{2, QueuePolicy::kBlock})) {
        std::cerr << "[StagePipeline] connect process->encode failed" << std::endl;
        return false;
    }
    if (!graph_.connect(encode, "encoded_video", output, "encoded_video", EdgeOptions{4, QueuePolicy::kDropOldest})) {
        std::cerr << "[StagePipeline] connect encode->output failed" << std::endl;
        return false;
    }
    if (audio) {
        if (!graph_.connect(audio, "audio_frame", output, "audio_frame", EdgeOptions{8, QueuePolicy::kDropOldest})) {
            std::cerr << "[StagePipeline] connect audio->output failed" << std::endl;
            return false;
        }
    }

    return graph_.validate();
}

void StagePipeline::startAdaptationLoop() {
    if (adaptationRunning_.load()) return;
    adaptationRunning_ = true;
    adaptationThread_ = std::thread(&StagePipeline::adaptationLoop, this);
}

void StagePipeline::stopAdaptationLoop() {
    if (!adaptationRunning_.load()) return;
    adaptationRunning_ = false;
    if (adaptationThread_.joinable()) {
        adaptationThread_.join();
    }
}

void StagePipeline::adaptationLoop() {
    uint64_t lastSendFailures = 0;
    auto lastLogTs = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    while (adaptationRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (!running_ || !outputStage_ || !encodeStage_) {
            continue;
        }

        std::lock_guard<std::mutex> lock(runtimeMutex_);
        if (runtimeStreamMode_ != "auto") {
            runtimeAdaptBadSinceMs_ = 0;
            runtimeAdaptGoodSinceMs_ = 0;
            continue;
        }

        const double fpsNow = outputStage_->currentFps();
        const double bitrateNow = outputStage_->currentBitrateBps();
        const double fpsTarget = std::max(1, runtimeTargetFps_);
        const double bitrateTarget = static_cast<double>(std::max(100, runtimeTargetBitrateKbps_)) * 1000.0;
        const uint64_t sendFailures = outputStage_->sendFailures();
        const uint64_t sendFailDelta = sendFailures >= lastSendFailures ? (sendFailures - lastSendFailures) : 0;
        lastSendFailures = sendFailures;
        if (fpsNow <= 0.0) {
            continue;
        }
        const int64_t now = nowMs();
        const bool fpsBad = fpsNow < fpsTarget * 0.90;
        const bool fpsGood = fpsNow >= fpsTarget * 0.98;
        const bool bitrateBad = bitrateNow > bitrateTarget * 1.20;
        const bool bitrateGood = bitrateNow > 0.0 && bitrateNow >= bitrateTarget * 0.75 && bitrateNow <= bitrateTarget * 1.10;
        const bool transportBad = sendFailDelta > 0;
        const int64_t sinceSwitch = runtimeLastSwitchMs_ > 0 ? (now - runtimeLastSwitchMs_) : std::numeric_limits<int64_t>::max();
        const bool canSwitch = sinceSwitch >= static_cast<int64_t>(runtimeAutoCooldownSec_) * 1000;
        const bool bad = fpsBad || bitrateBad || transportBad;
        const bool good = fpsGood && bitrateGood && !transportBad;

        const auto logNow = std::chrono::steady_clock::now();
        if (logNow - lastLogTs >= std::chrono::seconds(10)) {
            std::cout << "[StageAdapt] mode=auto"
                      << " level=L" << runtimeCurrentLevel_
                      << " range=L" << runtimeAutoMinLevel_ << "-L" << runtimeAutoMaxLevel_
                      << " fps=" << fpsNow << "/" << fpsTarget
                      << " bitrate_kbps=" << (bitrateNow / 1000.0) << "/" << (bitrateTarget / 1000.0)
                      << " send_fail_delta=" << sendFailDelta
                      << " bad=" << (bad ? "1" : "0")
                      << " good=" << (good ? "1" : "0")
                      << " can_switch=" << (canSwitch ? "1" : "0")
                      << std::endl;
            lastLogTs = logNow;
        }

        if (bad) {
            if (runtimeAdaptBadSinceMs_ <= 0) runtimeAdaptBadSinceMs_ = now;
            runtimeAdaptGoodSinceMs_ = 0;
        } else if (good) {
            if (runtimeAdaptGoodSinceMs_ <= 0) runtimeAdaptGoodSinceMs_ = now;
            runtimeAdaptBadSinceMs_ = 0;
        } else {
            runtimeAdaptBadSinceMs_ = 0;
            runtimeAdaptGoodSinceMs_ = 0;
        }

        if (canSwitch && runtimeAdaptBadSinceMs_ > 0 &&
            now - runtimeAdaptBadSinceMs_ >= static_cast<int64_t>(runtimeAutoDownHoldSec_) * 1000 &&
            runtimeCurrentLevel_ > runtimeAutoMinLevel_) {
            const int prevLevel = runtimeCurrentLevel_;
            runtimeCurrentLevel_ -= 1;
            const auto cfg = levelConfig(runtimeCurrentLevel_, resolveOutputFps(config_.camera.fps, config_.encoder.fps));
            runtimeTargetFps_ = cfg.fps;
            runtimeTargetBitrateKbps_ = cfg.bitrateKbps;
            encodeStage_->setRuntimeTarget(runtimeTargetFps_, runtimeTargetBitrateKbps_);
            runtimeLastSwitchMs_ = now;
            runtimeAdaptBadSinceMs_ = 0;
            runtimeAdaptGoodSinceMs_ = 0;
            std::cout << "[StageAdapt] downshift L" << prevLevel << "->L" << runtimeCurrentLevel_
                      << " reason="
                      << (transportBad ? "send_fail" : (bitrateBad ? "bitrate_high" : "fps_low"))
                      << " fps=" << fpsNow << "/" << fpsTarget
                      << " bitrate_kbps=" << (bitrateNow / 1000.0) << "/" << (bitrateTarget / 1000.0)
                      << " send_fail_delta=" << sendFailDelta
                      << std::endl;
            continue;
        }

        if (canSwitch && runtimeAdaptGoodSinceMs_ > 0 &&
            now - runtimeAdaptGoodSinceMs_ >= static_cast<int64_t>(runtimeAutoUpHoldSec_) * 1000 &&
            runtimeCurrentLevel_ < runtimeAutoMaxLevel_) {
            const int prevLevel = runtimeCurrentLevel_;
            runtimeCurrentLevel_ += 1;
            const auto cfg = levelConfig(runtimeCurrentLevel_, resolveOutputFps(config_.camera.fps, config_.encoder.fps));
            runtimeTargetFps_ = cfg.fps;
            runtimeTargetBitrateKbps_ = cfg.bitrateKbps;
            encodeStage_->setRuntimeTarget(runtimeTargetFps_, runtimeTargetBitrateKbps_);
            runtimeLastSwitchMs_ = now;
            runtimeAdaptBadSinceMs_ = 0;
            runtimeAdaptGoodSinceMs_ = 0;
            std::cout << "[StageAdapt] upshift L" << prevLevel << "->L" << runtimeCurrentLevel_
                      << " reason=stable_good"
                      << " fps=" << fpsNow << "/" << fpsTarget
                      << " bitrate_kbps=" << (bitrateNow / 1000.0) << "/" << (bitrateTarget / 1000.0)
                      << std::endl;
        }
    }
}

} // namespace reallive::stage
