#include "core/stage/OutputStage.h"

#include <chrono>
#include <thread>

namespace reallive::stage {

OutputStage::OutputStage(StreamerPtr streamer)
    : streamer_(std::move(streamer)) {}

OutputStage::~OutputStage() {
    requestStop();
    join();
}

std::string OutputStage::name() const {
    return "OutputStage";
}

std::vector<PortSpec> OutputStage::ports() const {
    return {
        PortSpec{"encoded_video", PortDirection::kInput, PacketType::kEncodedVideo},
        PortSpec{"audio_frame", PortDirection::kInput, PacketType::kAudio},
    };
}

bool OutputStage::init(const StageInitContext& ctx) {
    if (!ctx.config || !streamer_) {
        setState(StageState::kFailed);
        return false;
    }
    config_ = ctx.config;
    if (!streamer_->connect(config_->stream)) {
        setState(StageState::kFailed);
        return false;
    }

    if (config_->record.enabled) {
        recorder_ = std::make_unique<LocalRecorder>();
        const bool ok = recorder_->init(
            config_->record,
            config_->stream.streamKey,
            nullptr,
            0,
            config_->encoder.width,
            config_->encoder.height
        );
        if (!ok) {
            recorder_.reset();
        }
    }

    liveEnabled_ = true;
    liveActive_ = true;
    framesSent_ = 0;
    bytesSent_ = 0;
    currentFps_ = 0.0;
    currentBitrateBps_ = 0.0;
    sendFailures_ = 0;
    setState(StageState::kInited);
    return true;
}

bool OutputStage::start() {
    const StageState st = state();
    if (st == StageState::kRunning) return true;
    if (st != StageState::kInited && st != StageState::kStopped) {
        return false;
    }
    running_ = true;
    worker_ = std::thread(&OutputStage::runLoop, this);
    setState(StageState::kRunning);
    return true;
}

void OutputStage::requestStop() {
    const StageState st = state();
    if (st != StageState::kRunning) return;
    setState(StageState::kStopping);
    running_ = false;
    if (input_) input_->close();
}

void OutputStage::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
    if (streamer_) {
        streamer_->disconnect();
    }
    if (recorder_) {
        recorder_->close();
    }
    liveActive_ = false;
    const StageState st = state();
    if (st == StageState::kStopping || st == StageState::kRunning) {
        setState(StageState::kStopped);
    }
}

void OutputStage::setInputQueue(const std::shared_ptr<EdgeQueue<EncodedPacketEx>>& queue) {
    input_ = queue;
}

void OutputStage::setAudioInputQueue(const std::shared_ptr<EdgeQueue<AudioPacket>>& queue) {
    audioInput_ = queue;
}

bool OutputStage::setLiveEnabled(bool enabled) {
    liveEnabled_ = enabled;
    if (!streamer_ || !config_) {
        liveActive_ = false;
        return false;
    }
    if (!enabled) {
        if (streamer_->isConnected()) {
            streamer_->disconnect();
        }
        liveActive_ = false;
        return true;
    }
    if (!streamer_->isConnected()) {
        if (!streamer_->connect(config_->stream)) {
            liveActive_ = false;
            return false;
        }
    }
    liveActive_ = streamer_->isConnected();
    return liveActive_;
}

bool OutputStage::isLiveEnabled() const {
    return liveEnabled_.load();
}

bool OutputStage::isLiveActive() const {
    return liveActive_.load();
}

uint64_t OutputStage::framesSent() const {
    return framesSent_.load();
}

uint64_t OutputStage::bytesSent() const {
    return bytesSent_.load();
}

double OutputStage::currentFps() const {
    return currentFps_.load();
}

double OutputStage::currentBitrateBps() const {
    return currentBitrateBps_.load();
}

uint64_t OutputStage::sendFailures() const {
    return sendFailures_.load();
}

bool OutputStage::setRecordCleanupPolicy(int minFreePercent, int targetFreePercent) {
    if (!recorder_ || !recorder_->isEnabled()) return false;
    return recorder_->setCleanupPolicy(minFreePercent, targetFreePercent);
}

bool OutputStage::getRecordCleanupPolicy(int& minFreePercent, int& targetFreePercent) const {
    if (!recorder_ || !recorder_->isEnabled()) {
        if (config_) {
            minFreePercent = config_->record.minFreePercent;
            targetFreePercent = config_->record.targetFreePercent;
        } else {
            minFreePercent = 15;
            targetFreePercent = 20;
        }
        return false;
    }
    recorder_->getCleanupPolicy(minFreePercent, targetFreePercent);
    return true;
}

void OutputStage::runLoop() {
    auto lastFpsTs = std::chrono::steady_clock::now();
    uint64_t lastFrames = 0;
    uint64_t lastBytes = 0;
    while (running_.load()) {
        if (audioInput_) {
            AudioPacket audioPkt;
            if (audioInput_->pop(audioPkt, std::chrono::milliseconds{0})) {
                if (liveEnabled_.load() && streamer_) {
                    if (!streamer_->isConnected()) {
                        if (!streamer_->connect(config_->stream)) {
                            liveActive_ = false;
                        }
                    }
                    if (streamer_->isConnected()) {
                        streamer_->sendAudioPacket(audioPkt.audio);
                    }
                }
            }
        }

        if (!input_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        EncodedPacketEx in;
        const bool ok = input_->pop(in, std::chrono::milliseconds{100});
        if (!ok) {
            continue;
        }
        if (!streamer_) {
            continue;
        }
        if (!liveEnabled_.load()) {
            continue;
        }
        if (!streamer_->isConnected()) {
            if (!streamer_->connect(config_->stream)) {
                liveActive_ = false;
                continue;
            }
        }
        liveActive_ = true;
        if (streamer_->sendVideoPacket(in.encoded)) {
            framesSent_++;
            bytesSent_ += in.encoded.data.size();
        } else {
            liveActive_ = false;
            sendFailures_++;
        }
        if (recorder_ && recorder_->isEnabled()) {
            recorder_->writeVideoPacket(in.encoded);
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTs);
        if (elapsed.count() >= 1000) {
            const uint64_t frames = framesSent_.load();
            const uint64_t delta = frames >= lastFrames ? (frames - lastFrames) : 0;
            currentFps_ = static_cast<double>(delta) * 1000.0 / static_cast<double>(elapsed.count());
            lastFrames = frames;
            const uint64_t bytes = bytesSent_.load();
            const uint64_t bytesDelta = bytes >= lastBytes ? (bytes - lastBytes) : 0;
            currentBitrateBps_ = static_cast<double>(bytesDelta) * 8.0 * 1000.0 / static_cast<double>(elapsed.count());
            lastBytes = bytes;
            lastFpsTs = now;
        }
    }
}

} // namespace reallive::stage
