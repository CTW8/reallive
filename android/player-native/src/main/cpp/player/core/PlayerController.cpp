#include "player/core/PlayerController.h"

#include "player/impl/FfmpegPlayer.h"

namespace reallive::player {

PlayerController::PlayerController()
    : player_(std::make_unique<FfmpegPlayer>()) {
}

PlayerController::~PlayerController() = default;

bool PlayerController::playLive(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return false;
    PlayerSource source;
    source.type = SourceType::Live;
    source.url = url;
    source.startMs = 0;
    return player_->play(source);
}

bool PlayerController::playHistory(const std::string& url, int64_t startMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return false;
    PlayerSource source;
    source.type = SourceType::History;
    source.url = url;
    source.startMs = startMs;
    return player_->play(source);
}

void PlayerController::seekTo(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return;
    player_->seekTo(positionMs);
}

void PlayerController::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return;
    player_->stop();
}

void PlayerController::setSurface(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return;
    player_->setSurface(window);
}

PlayerStats PlayerController::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player_) return {};
    return player_->stats();
}

}  // namespace reallive::player
