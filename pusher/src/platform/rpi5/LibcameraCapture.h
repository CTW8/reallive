#pragma once

#include "platform/ICameraCapture.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

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

    // Frame buffering with circular queue
    std::mutex frameMutex_;
    std::condition_variable frameCv_;
    
    // Circular buffer queue - stores multiple frames to reduce latency jitter
    static constexpr size_t kMaxQueueSize = 3;  // Keep at most 3 frames in queue
    std::vector<Frame> frameQueue_;
    size_t queueHead_ = 0;
    size_t queueTail_ = 0;
    size_t queueCount_ = 0;
    
    // Dropped frames counter
    std::atomic<uint64_t> droppedFrames_{0};
    
    // Legacy single frame support (for compatibility)
    Frame latestFrame_;
    bool frameReady_ = false;

    // Memory-mapped regions (for cleanup)
    struct MmapRegion {
        void* ptr = nullptr;
        size_t length = 0;
    };
    std::vector<MmapRegion> mmapRegions_;

    // Per-buffer plane mappings: pointer to plane data within mmap'd region
    struct MappedPlane {
        void* data = nullptr;
        size_t length = 0;
    };
    std::map<const libcamera::FrameBuffer*, std::vector<MappedPlane>> bufferPlanes_;

    std::vector<std::unique_ptr<libcamera::Request>> requests_;
};

} // namespace reallive
