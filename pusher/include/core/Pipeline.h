#pragma once

#include "Config.h"
#include "platform/ICameraCapture.h"
#include "platform/IAudioCapture.h"
#include "platform/IEncoder.h"
#include "platform/IStreamer.h"
#include <atomic>
#include <thread>
#include <memory>

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

private:
    void videoLoop();
    void audioLoop();

    bool createComponents(const PusherConfig& config);

    CameraCapturePtr camera_;
    AudioCapturePtr audio_;
    EncoderPtr encoder_;
    StreamerPtr streamer_;

    std::thread videoThread_;
    std::thread audioThread_;
    std::atomic<bool> running_{false};

    // Stats
    std::atomic<uint64_t> framesSent_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<double> currentFps_{0.0};

    PusherConfig config_;
};

} // namespace reallive
