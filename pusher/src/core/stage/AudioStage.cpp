#include "core/stage/AudioStage.h"

#include <chrono>

namespace reallive::stage {

AudioStage::AudioStage(AudioCapturePtr audio)
    : audio_(std::move(audio)) {}

AudioStage::~AudioStage() {
    requestStop();
    join();
}

std::string AudioStage::name() const {
    return "AudioStage";
}

std::vector<PortSpec> AudioStage::ports() const {
    return {
        PortSpec{"audio_frame", PortDirection::kOutput, PacketType::kAudio},
    };
}

bool AudioStage::init(const StageInitContext& ctx) {
    if (!ctx.config || !audio_) {
        setState(StageState::kFailed);
        return false;
    }
    config_ = ctx.config;
    if (!config_->enableAudio) {
        setState(StageState::kInited);
        return true;
    }
    if (!audio_->open(config_->audio)) {
        setState(StageState::kFailed);
        return false;
    }
    setState(StageState::kInited);
    return true;
}

bool AudioStage::start() {
    const StageState st = state();
    if (st == StageState::kRunning) return true;
    if (st != StageState::kInited && st != StageState::kStopped) {
        return false;
    }

    if (config_ && config_->enableAudio && audio_ && !audio_->start()) {
        setState(StageState::kFailed);
        return false;
    }

    running_ = true;
    worker_ = std::thread(&AudioStage::runLoop, this);
    setState(StageState::kRunning);
    return true;
}

void AudioStage::requestStop() {
    const StageState st = state();
    if (st != StageState::kRunning) return;
    setState(StageState::kStopping);
    running_ = false;
}

void AudioStage::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
    if (audio_ && config_ && config_->enableAudio) {
        audio_->stop();
    }
    const StageState st = state();
    if (st == StageState::kStopping || st == StageState::kRunning) {
        setState(StageState::kStopped);
    }
}

void AudioStage::setOutputQueue(const std::shared_ptr<EdgeQueue<AudioPacket>>& queue) {
    output_ = queue;
}

void AudioStage::runLoop() {
    while (running_.load()) {
        if (!config_ || !config_->enableAudio || !audio_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        AudioFrame frame = audio_->captureFrame();
        if (frame.empty()) {
            continue;
        }

        AudioPacket pkt;
        pkt.trace.traceId = nextTraceId_.fetch_add(1);
        pkt.trace.frameId = pkt.trace.traceId;
        pkt.trace.ingestTsMs = nowMs();
        pkt.audio = std::move(frame);

        if (output_) {
            output_->push(std::move(pkt), std::chrono::milliseconds{1});
        }
    }
}

int64_t AudioStage::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace reallive::stage
