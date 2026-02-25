#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace reallive::stage {

class DetectStage : public StageBase {
public:
    DetectStage() = default;
    ~DetectStage() override;

    std::string name() const override;
    std::vector<PortSpec> ports() const override;

    bool init(const StageInitContext& ctx) override;
    bool start() override;
    void requestStop() override;
    void join() override;

    void setInputQueue(const std::shared_ptr<EdgeQueue<FramePacket>>& queue);
    void setOutputQueue(const std::shared_ptr<EdgeQueue<DetectionPacket>>& queue);
    void setRuntimeDetectionEnabled(bool motionEnabled, bool personEnabled);

private:
    bool detectMotion(const FramePacket& frame, DetectionPacket& det);
    void runLoop();

    const PusherConfig* config_ = nullptr;
    std::shared_ptr<EdgeQueue<FramePacket>> input_;
    std::shared_ptr<EdgeQueue<DetectionPacket>> output_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> runtimeMotionEnabled_{true};
    std::atomic<bool> runtimePersonEnabled_{true};
    std::vector<uint8_t> prevLuma_;
    bool hasPrev_ = false;
};

} // namespace reallive::stage
