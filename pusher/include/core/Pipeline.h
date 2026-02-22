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
    bool applyRuntimeSettings(bool motionEnabled, bool personEnabled, bool watermarkEnabled);
    void getRuntimeSettings(bool& motionEnabled, bool& personEnabled, bool& watermarkEnabled) const;
    bool applyRuntimeVisualSettings(int imageFlipMode, bool nightVisionEnabled, int nightVisionMode);
    void getRuntimeVisualSettings(int& imageFlipMode, bool& nightVisionEnabled, int& nightVisionMode) const;

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
    std::atomic<bool> runtimeWatermarkEnabled_{true};
    std::atomic<int> runtimeImageFlipMode_{0}; // 0 normal, 1 hflip, 2 vflip, 3 rotate180
    std::atomic<bool> runtimeNightVisionEnabled_{false};
    std::atomic<int> runtimeNightVisionMode_{0}; // 0 auto, 1 on, 2 off
    mutable std::mutex streamerMutex_;

    PusherConfig config_;
};

} // namespace reallive
