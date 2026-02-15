#pragma once

#include <android/native_window.h>

#include "player/interface/PlayerTypes.h"

namespace reallive::player {

class IPlayer {
public:
    virtual ~IPlayer() = default;

    virtual bool play(const PlayerSource& source) = 0;
    virtual void seekTo(int64_t positionMs) = 0;
    virtual void stop() = 0;
    virtual void setSurface(ANativeWindow* window) = 0;
    virtual PlayerStats stats() const = 0;
};

}  // namespace reallive::player
