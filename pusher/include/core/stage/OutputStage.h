#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include "core/LocalRecorder.h"
#include "platform/IStreamer.h"

#include <atomic>
#include <memory>
#include <thread>

namespace reallive::stage {

class OutputStage : public StageBase {
public:
    explicit OutputStage(StreamerPtr streamer);
    ~OutputStage() override;

    std::string name() const override;
    std::vector<PortSpec> ports() const override;

    bool init(const StageInitContext& ctx) override;
    bool start() override;
    void requestStop() override;
    void join() override;

    void setInputQueue(const std::shared_ptr<EdgeQueue<EncodedPacketEx>>& queue);
    void setAudioInputQueue(const std::shared_ptr<EdgeQueue<AudioPacket>>& queue);
    bool setLiveEnabled(bool enabled);
    bool isLiveEnabled() const;
    bool isLiveActive() const;
    uint64_t framesSent() const;
    uint64_t bytesSent() const;
    double currentFps() const;
    double currentBitrateBps() const;
    uint64_t sendFailures() const;
    bool setRecordCleanupPolicy(int minFreePercent, int targetFreePercent);
    bool getRecordCleanupPolicy(int& minFreePercent, int& targetFreePercent) const;

private:
    void runLoop();

    StreamerPtr streamer_;
    std::unique_ptr<LocalRecorder> recorder_;
    const PusherConfig* config_ = nullptr;
    std::shared_ptr<EdgeQueue<EncodedPacketEx>> input_;
    std::shared_ptr<EdgeQueue<AudioPacket>> audioInput_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> liveEnabled_{true};
    std::atomic<bool> liveActive_{false};
    std::atomic<uint64_t> framesSent_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<double> currentFps_{0.0};
    std::atomic<double> currentBitrateBps_{0.0};
    std::atomic<uint64_t> sendFailures_{0};
};

} // namespace reallive::stage
