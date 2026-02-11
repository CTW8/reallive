#pragma once

#include "IDecoder.h"

namespace reallive {
namespace puller {

/// Abstract interface for rendering decoded frames (optional, for preview)
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// Initialize the renderer with stream dimensions
    virtual bool init(int width, int height) = 0;

    /// Render a single decoded frame
    virtual bool render(const Frame& frame) = 0;

    /// Release renderer resources
    virtual void destroy() = 0;
};

} // namespace puller
} // namespace reallive
