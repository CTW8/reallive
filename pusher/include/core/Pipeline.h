#pragma once

#include "Config.h"
#include "platform/ICameraCapture.h"
#include "platform/IAudioCapture.h"
#include "platform/IEncoder.h"
#include "platform/IStreamer.h"
#include "core/LocalRecorder.h"
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <string>

namespace reallive {

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    bool init(const PusherConfig& config);
    bool start();
    void stop();
    bool isRunning() const;

    // Stats
    uint64_t getFramesSent() const;
    uint64_t getBytesSent() const;
    double getCurrentFps() const;
    bool setLivePushEnabled(bool enabled);
    bool isLivePushEnabled() const;
    bool isLivePushActive() const;
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

private:
    void videoLoop();
    void audioLoop();

    bool createComponents(const PusherConfig& config);

    CameraCapturePtr camera_;
    AudioCapturePtr audio_;
    EncoderPtr encoder_;
    StreamerPtr streamer_;
    std::unique_ptr<LocalRecorder> recorder_;

    std::thread videoThread_;
    std::thread audioThread_;
    std::atomic<bool> running_{false};

    // Stats
    std::atomic<uint64_t> framesSent_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<double> currentFps_{0.0};
    std::atomic<bool> livePushDesired_{true};
    std::atomic<bool> livePushActive_{false};
    std::atomic<bool> runtimeMotionEnabled_{true};
    std::atomic<bool> runtimePersonEnabled_{true};
    std::atomic<bool> runtimeSoundEnabled_{false};
    std::atomic<bool> runtimeWatermarkEnabled_{true};
    mutable std::mutex runtimeSettingsMutex_;
    std::string runtimeMotionSensitivity_{"High"};
    std::string runtimeSoundSensitivity_{"Loud"};
    std::string runtimeDetectionZones_{"2 zones configured"};
    std::atomic<int> runtimeImageFlipMode_{0}; // 0 normal, 1 hflip, 2 vflip, 3 rotate180
    std::atomic<bool> runtimeNightVisionEnabled_{false};
    std::atomic<int> runtimeNightVisionMode_{0}; // 0 auto, 1 on, 2 off
    std::atomic<int> runtimeProfileLevel_{2};
    std::atomic<int> runtimeProfileTargetWidth_{1280};
    std::atomic<int> runtimeProfileTargetHeight_{720};
    std::atomic<int> runtimeProfileTargetFps_{20};
    std::atomic<int> runtimeProfileTargetBitrateKbps_{1200};
    std::atomic<int64_t> runtimeLastSwitchMs_{0};
    std::atomic<uint64_t> runtimeAdaptLastSendDrop_{0};
    std::atomic<int64_t> runtimeAdaptBadSinceMs_{0};
    std::atomic<int64_t> runtimeAdaptGoodSinceMs_{0};
    mutable std::mutex streamerMutex_;

    PusherConfig config_;
    std::string runtimeStreamMode_{"auto"};
    std::string runtimeStreamProfile_{"auto"};
    std::string runtimeAutoPolicy_{"balanced"};
    int runtimeManualLevel_ = 2;
    int runtimeAutoMinLevel_ = 0;
    int runtimeAutoMaxLevel_ = 4;
    int runtimeAutoCooldownSec_ = 10;
    int runtimeAutoUpHoldSec_ = 25;
    int runtimeAutoDownHoldSec_ = 3;
    mutable std::mutex runtimeStreamMutex_;
};

} // namespace reallive
