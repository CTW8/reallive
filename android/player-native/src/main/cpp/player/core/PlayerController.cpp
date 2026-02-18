#include "player/core/PlayerController.h"

#include <android/log.h>

#include "player/impl/FfmpegPlayer.h"

namespace reallive::player {

#define RL_CTRL_TAG "RealLivePlayerController"
#define RL_CTRL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, RL_CTRL_TAG, __VA_ARGS__)

PlayerController::PlayerController()
    : player_(std::make_unique<FfmpegPlayer>()) {
    RL_CTRL_LOGI("PlayerController created %p", this);
}

PlayerController::~PlayerController() {
    RL_CTRL_LOGI("PlayerController destroyed %p", this);
}

bool PlayerController::playLive(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return false;
    RL_CTRL_LOGI("playLive url=%s", url.c_str());
    PlayerSource source;
    source.type = SourceType::Live;
    source.url = url;
    source.startMs = 0;
    return player_->play(source);
}

bool PlayerController::playHistory(const std::string& url, int64_t startMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return false;
    RL_CTRL_LOGI("playHistory startMs=%lld url=%s", static_cast<long long>(startMs), url.c_str());
    PlayerSource source;
    source.type = SourceType::History;
    source.url = url;
    source.startMs = startMs;
    return player_->play(source);
}

void PlayerController::seekTo(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return;
    RL_CTRL_LOGI("seekTo %lld", static_cast<long long>(positionMs));
    player_->seekTo(positionMs);
}

void PlayerController::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return;
    RL_CTRL_LOGI("stop");
    player_->stop();
}

void PlayerController::setSurface(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return;
    RL_CTRL_LOGI("setSurface window=%p", static_cast<void*>(window));
    player_->setSurface(window);
}

PlayerStats PlayerController::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return {};
    return player_->stats();
}

}  // namespace reallive::player
