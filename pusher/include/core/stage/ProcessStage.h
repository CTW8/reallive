#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include <atomic>
#include <memory>
#include <thread>

namespace reallive::stage {

class ProcessStage : public StageBase {
public:
    ProcessStage() = default;
    ~ProcessStage() override;

    std::string name() const override;
    std::vector<PortSpec> ports() const override;

    bool init(const StageInitContext& ctx) override;
    bool start() override;
    void requestStop() override;
    void join() override;

    void setFrameInputQueue(const std::shared_ptr<EdgeQueue<FramePacket>>& queue);
    void setDetectionInputQueue(const std::shared_ptr<EdgeQueue<DetectionPacket>>& queue);
    void setOutputQueue(const std::shared_ptr<EdgeQueue<ProcessedFramePacket>>& queue);
    void setRuntimeDetectionOptions(bool personEnabled, bool drawOverlay);
    void setRuntimeVisualOptions(int imageFlipMode, bool nightVisionEnabled, int nightVisionMode);
    void setRuntimeWatermarkEnabled(bool enabled);

private:
    void runLoop();

    const PusherConfig* config_ = nullptr;
    std::shared_ptr<EdgeQueue<FramePacket>> frameInput_;
    std::shared_ptr<EdgeQueue<DetectionPacket>> detectionInput_;
    std::shared_ptr<EdgeQueue<ProcessedFramePacket>> output_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> runtimePersonEnabled_{true};
    std::atomic<bool> runtimeDrawOverlay_{true};
    std::atomic<bool> runtimeWatermarkEnabled_{true};
    std::atomic<int> runtimeImageFlipMode_{0};
    std::atomic<bool> runtimeNightVisionEnabled_{false};
    std::atomic<int> runtimeNightVisionMode_{0};
};

} // namespace reallive::stage
