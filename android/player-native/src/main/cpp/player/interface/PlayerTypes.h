#pragma once

#include <cstdint>
#include <string>

namespace reallive::player {

enum class SourceType {
    Live = 0,
    History = 1,
};

struct PlayerSource {
    SourceType type = SourceType::Live;
    std::string url;
    int64_t startMs = 0;
};

struct PlayerStats {
    int32_t videoWidth = 0;
    int32_t videoHeight = 0;
    double decodeFps = 0.0;
    double renderFps = 0.0;
    int64_t bufferedFrames = 0;
};

}  // namespace reallive::player
