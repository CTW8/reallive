#pragma once

#include <cstdint>

struct RenderConfig {
    int width = 1920;
    int height = 1080;
    bool fullscreen = false;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(const RenderConfig& config) = 0;
    virtual void renderFrame(const uint8_t* data, int width, int height) = 0;
    virtual void close() = 0;
};
