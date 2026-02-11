#pragma once

#include "platform/ICameraCapture.h"

#include <memory>
#include <mutex>

// Forward declare libcamera types to avoid requiring libcamera headers
// for code that only uses the ICameraCapture interface.
namespace libcamera {
class CameraManager;
class Camera;
class CameraConfiguration;
class FrameBufferAllocator;
class Request;
class FrameBuffer;
class Stream;
} // namespace libcamera

namespace reallive {

class LibcameraCapture : public ICameraCapture {
public:
    LibcameraCapture();
    ~LibcameraCapture() override;

    bool open(const CaptureConfig& config) override;
    bool start() override;
    bool stop() override;
    Frame captureFrame() override;
    bool isOpen() const override;
    std::string getName() const override;

private:
    void requestComplete(libcamera::Request* request);

    std::unique_ptr<libcamera::CameraManager> cameraManager_;
    std::shared_ptr<libcamera::Camera> camera_;
    std::unique_ptr<libcamera::CameraConfiguration> cameraConfig_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;

    CaptureConfig config_;
    bool opened_ = false;
    bool started_ = false;

    // Frame buffering
    std::mutex frameMutex_;
    Frame latestFrame_;
    bool frameReady_ = false;

    // Memory-mapped buffer info
    struct MappedBuffer {
        void* ptr = nullptr;
        size_t length = 0;
    };
    std::vector<MappedBuffer> mappedBuffers_;

    std::vector<std::unique_ptr<libcamera::Request>> requests_;
};

} // namespace reallive
