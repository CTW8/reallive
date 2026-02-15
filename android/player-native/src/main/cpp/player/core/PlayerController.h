#pragma once

#include <android/native_window.h>

#include <memory>
#include <mutex>
#include <string>

#include "player/interface/IPlayer.h"

namespace reallive::player {

class PlayerController {
public:
    PlayerController();
    ~PlayerController();

    bool playLive(const std::string& url);
    bool playHistory(const std::string& url, int64_t startMs);
    void seekTo(int64_t positionMs);
    void stop();
    void setSurface(ANativeWindow* window);
    PlayerStats stats() const;

private:
    mutable std::mutex mutex_;
    std::unique_ptr<IPlayer> player_;
};

}  // namespace reallive::player
