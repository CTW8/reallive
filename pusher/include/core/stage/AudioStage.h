#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include "platform/IAudioCapture.h"

#include <atomic>
#include <memory>
#include <thread>

namespace reallive::stage {

class AudioStage : public StageBase {
public:
    explicit AudioStage(AudioCapturePtr audio);
    ~AudioStage() override;

    std::string name() const override;
    std::vector<PortSpec> ports() const override;

    bool init(const StageInitContext& ctx) override;
    bool start() override;
    void requestStop() override;
    void join() override;

    void setOutputQueue(const std::shared_ptr<EdgeQueue<AudioPacket>>& queue);

private:
    void runLoop();
    static int64_t nowMs();

    AudioCapturePtr audio_;
    const PusherConfig* config_ = nullptr;
    std::shared_ptr<EdgeQueue<AudioPacket>> output_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> nextTraceId_{1};
};

} // namespace reallive::stage
