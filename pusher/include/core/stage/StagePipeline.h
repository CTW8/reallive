#pragma once

#include "core/stage/AudioStage.h"
#include "core/stage/CaptureStage.h"
#include "core/stage/DetectStage.h"
#include "core/stage/EncodeStage.h"
#include "core/stage/OutputStage.h"
#include "core/stage/ProcessStage.h"
#include "core/stage/StageGraph.h"

#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace reallive::stage {

class StagePipeline {
public:
    StagePipeline() = default;
    ~StagePipeline();

    bool init(
        const PusherConfig& config,
        CameraCapturePtr camera,
        EncoderPtr encoder,
        StreamerPtr streamer,
        AudioCapturePtr audio = nullptr
    );

    bool start();
    void stop();
    bool isRunning() const;
    bool setLiveEnabled(bool enabled);
    bool isLiveEnabled() const;
    bool isLiveActive() const;
    uint64_t framesSent() const;
    uint64_t bytesSent() const;
    double currentFps() const;
    bool setRecordCleanupPolicy(int minFreePercent, int targetFreePercent);
    bool getRecordCleanupPolicy(int& minFreePercent, int& targetFreePercent) const;
    bool applyRuntimeSettings(
        bool motionEnabled,
        bool personEnabled,
        bool soundEnabled,
        const std::string& motionSensitivity,
        const std::string& soundSensitivity,
        const std::string& detectionZones,
        bool watermarkEnabled
    );
    void getRuntimeSettings(
        bool& motionEnabled,
        bool& personEnabled,
        bool& soundEnabled,
        std::string& motionSensitivity,
        std::string& soundSensitivity,
        std::string& detectionZones,
        bool& watermarkEnabled
    ) const;
    bool applyRuntimeVisualSettings(int imageFlipMode, bool nightVisionEnabled, int nightVisionMode);
    void getRuntimeVisualSettings(int& imageFlipMode, bool& nightVisionEnabled, int& nightVisionMode) const;
    bool applyRuntimeStreamPolicy(
        const std::string& streamProfile,
        const std::string& streamMode,
        int manualLevel,
        int autoMinLevel,
        int autoMaxLevel,
        const std::string& autoPolicy,
        int autoCooldownSec,
        int autoUpHoldSec,
        int autoDownHoldSec
    );
    void getRuntimeStreamPolicy(
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
    ) const;
    bool applyRuntimePtzCommand(
        const std::string& action,
        int speed,
        int zoomStep,
        int zoomLevel,
        const std::string& preset
    );
    void getRuntimePtzState(
        std::string& action,
        int& speed,
        int& zoomStep,
        int& zoomLevel,
        std::string& preset,
        int64_t& updatedAtMs
    ) const;

private:
    bool buildGraph();
    void startAdaptationLoop();
    void stopAdaptationLoop();
    void adaptationLoop();

    PusherConfig config_{};
    StageGraph graph_;
    std::shared_ptr<CaptureStage> captureStage_;
    std::shared_ptr<AudioStage> audioStage_;
    std::shared_ptr<DetectStage> detectStage_;
    std::shared_ptr<ProcessStage> processStage_;
    std::shared_ptr<EncodeStage> encodeStage_;
    std::shared_ptr<OutputStage> outputStage_;
    bool inited_ = false;
    bool running_ = false;
    std::thread adaptationThread_;
    std::atomic<bool> adaptationRunning_{false};

    mutable std::mutex runtimeMutex_;
    bool runtimeMotionEnabled_ = true;
    bool runtimePersonEnabled_ = true;
    bool runtimeSoundEnabled_ = false;
    std::string runtimeMotionSensitivity_{"High"};
    std::string runtimeSoundSensitivity_{"Loud"};
    std::string runtimeDetectionZones_{"2 zones configured"};
    bool runtimeWatermarkEnabled_ = true;
    int runtimeImageFlipMode_ = 0;
    bool runtimeNightVisionEnabled_ = false;
    int runtimeNightVisionMode_ = 0;
    std::string runtimeStreamProfile_{"auto"};
    std::string runtimeStreamMode_{"auto"};
    int runtimeManualLevel_ = 2;
    int runtimeAutoMinLevel_ = 0;
    int runtimeAutoMaxLevel_ = 4;
    std::string runtimeAutoPolicy_{"balanced"};
    int runtimeAutoCooldownSec_ = 10;
    int runtimeAutoUpHoldSec_ = 25;
    int runtimeAutoDownHoldSec_ = 3;
    int runtimeCurrentLevel_ = 2;
    int runtimeTargetFps_ = 24;
    int runtimeTargetBitrateKbps_ = 2000;
    int64_t runtimeLastSwitchMs_ = 0;
    int64_t runtimeAdaptBadSinceMs_ = 0;
    int64_t runtimeAdaptGoodSinceMs_ = 0;
    std::string runtimePtzAction_{"stop"};
    int runtimePtzSpeed_ = 5;
    int runtimePtzZoomStep_ = 1;
    int runtimePtzZoomLevel_ = 50;
    std::string runtimePtzPreset_;
    int64_t runtimePtzUpdatedAtMs_ = 0;
};

} // namespace reallive::stage
