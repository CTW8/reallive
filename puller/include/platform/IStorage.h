#pragma once

#include "IDecoder.h"

#include <string>

namespace reallive {
namespace puller {

/// Abstract interface for storing media data to file
class IStorage {
public:
    virtual ~IStorage() = default;

    /// Open a file for writing with the given stream info
    virtual bool open(const std::string& filepath, const StreamInfo& info) = 0;

    /// Write an encoded packet directly (no decoding needed)
    virtual bool writePacket(const EncodedPacket& packet) = 0;

    /// Write a decoded frame (requires re-encoding or raw storage)
    virtual bool writeFrame(const Frame& frame) = 0;

    /// Close the file and finalize
    virtual void close() = 0;
};

} // namespace puller
} // namespace reallive
