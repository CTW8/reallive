#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include "platform/IEncoder.h"

#include <atomic>
#include <memory>
#include <thread>

namespace reallive::stage {

class EncodeStage : public StageBase {
public:
    explicit EncodeStage(EncoderPtr encoder);
    ~EncodeStage() override;

    std::string name() const override;
    std::vector<PortSpec> ports() const override;

    bool init(const StageInitContext& ctx) override;
    bool start() override;
    void requestStop() override;
    void join() override;

    void setInputQueue(const std::shared_ptr<EdgeQueue<ProcessedFramePacket>>& queue);
    void setOutputQueue(const std::shared_ptr<EdgeQueue<EncodedPacketEx>>& queue);
    void setRuntimeTarget(int fps, int bitrateKbps);
    void getRuntimeTarget(int& fps, int& bitrateKbps) const;

private:
    void runLoop();

    EncoderPtr encoder_;
    const PusherConfig* config_ = nullptr;
    std::shared_ptr<EdgeQueue<ProcessedFramePacket>> input_;
    std::shared_ptr<EdgeQueue<EncodedPacketEx>> output_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<int> runtimeTargetFps_{24};
    std::atomic<int> runtimeTargetBitrateKbps_{2000};
    std::atomic<int64_t> nextEncodeDueMs_{0};
    std::atomic<int64_t> bitrateWindowStartMs_{0};
    std::atomic<uint64_t> bitrateWindowBytes_{0};
};

} // namespace reallive::stage
