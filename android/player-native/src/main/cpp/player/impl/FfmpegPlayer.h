#pragma once

#include <android/native_window.h>

#include "player/interface/IPlayer.h"

namespace reallive::player {

struct NativePlayerContext;

class FfmpegPlayer final : public IPlayer {
public:
    FfmpegPlayer();
    ~FfmpegPlayer() override;

    bool play(const PlayerSource& source) override;
    void seekTo(int64_t positionMs) override;
    void stop() override;
    void setSurface(ANativeWindow* window) override;
    PlayerStats stats() const override;

private:
    NativePlayerContext* ctx_ = nullptr;
};

}  // namespace reallive::player
