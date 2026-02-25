#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include "platform/ICameraCapture.h"

#include <atomic>
#include <memory>
#include <thread>

namespace reallive::stage {

struct CaptureStageStats {
    uint64_t captured = 0;
    uint64_t pushed = 0;
    uint64_t dropped = 0;
};

class CaptureStage : public StageBase {
public:
    explicit CaptureStage(CameraCapturePtr camera);
    ~CaptureStage() override;

    std::string name() const override;
    std::vector<PortSpec> ports() const override;

    bool init(const StageInitContext& ctx) override;
    bool start() override;
    void requestStop() override;
    void join() override;

    void setOutputQueue(const std::shared_ptr<EdgeQueue<FramePacket>>& queue);
    CaptureStageStats stats() const;

private:
    static int64_t nowMs();
    static int64_t normalizeTsMs(int64_t ptsUs);
    void runLoop();

    CameraCapturePtr camera_;
    const PusherConfig* config_ = nullptr;
    std::shared_ptr<EdgeQueue<FramePacket>> output_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> nextFrameId_{1};
    std::atomic<uint64_t> captured_{0};
    std::atomic<uint64_t> pushed_{0};
    std::atomic<uint64_t> dropped_{0};
};

} // namespace reallive::stage
