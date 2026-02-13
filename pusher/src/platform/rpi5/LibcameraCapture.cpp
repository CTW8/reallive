#include "platform/rpi5/LibcameraCapture.h"

#include <libcamera/libcamera.h>
#include <libcamera/controls.h>
#include <libcamera/control_ids.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <map>

namespace reallive {

LibcameraCapture::LibcameraCapture() {
    // Pre-allocate circular buffer to avoid runtime allocation
    frameQueue_.reserve(kMaxQueueSize);
}

LibcameraCapture::~LibcameraCapture() {
    stop();
    if (camera_) {
        camera_->release();
    }
    if (cameraManager_) {
        cameraManager_->stop();
    }
    // Unmap all regions
    for (auto& region : mmapRegions_) {
        if (region.ptr && region.ptr != MAP_FAILED) {
            munmap(region.ptr, region.length);
        }
    }
}

bool LibcameraCapture::open(const CaptureConfig& config) {
    config_ = config;

    // Start camera manager
    cameraManager_ = std::make_unique<libcamera::CameraManager>();
    int ret = cameraManager_->start();
    if (ret != 0) {
        std::cerr << "[LibcameraCapture] Failed to start CameraManager: " << ret << std::endl;
        return false;
    }

    // Get list of cameras
    auto cameras = cameraManager_->cameras();
    if (cameras.empty()) {
        std::cerr << "[LibcameraCapture] No cameras found" << std::endl;
        return false;
    }

    // Use first camera
    camera_ = cameras[0];
    std::cout << "[LibcameraCapture] Using camera: " << camera_->id() << std::endl;

    // Acquire camera
    ret = camera_->acquire();
    if (ret != 0) {
        std::cerr << "[LibcameraCapture] Failed to acquire camera: " << ret << std::endl;
        return false;
    }

    // Configure camera
    cameraConfig_ = camera_->generateConfiguration(
        { libcamera::StreamRole::VideoRecording });
    if (!cameraConfig_) {
        std::cerr << "[LibcameraCapture] Failed to generate camera configuration" << std::endl;
        return false;
    }

    auto& streamConfig = cameraConfig_->at(0);
    streamConfig.size.width = config.width;
    streamConfig.size.height = config.height;
    streamConfig.pixelFormat = libcamera::formats::NV12;
    streamConfig.bufferCount = 4;

    auto validation = cameraConfig_->validate();
    if (validation == libcamera::CameraConfiguration::Invalid) {
        std::cerr << "[LibcameraCapture] Camera configuration invalid" << std::endl;
        return false;
    }
    if (validation == libcamera::CameraConfiguration::Adjusted) {
        std::cout << "[LibcameraCapture] Configuration adjusted: "
                  << streamConfig.size.width << "x" << streamConfig.size.height << std::endl;
    }

    ret = camera_->configure(cameraConfig_.get());
    if (ret != 0) {
        std::cerr << "[LibcameraCapture] Failed to configure camera: " << ret << std::endl;
        return false;
    }

    // Allocate frame buffers
    allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
    libcamera::Stream* stream = streamConfig.stream();
    ret = allocator_->allocate(stream);
    if (ret < 0) {
        std::cerr << "[LibcameraCapture] Failed to allocate buffers: " << ret << std::endl;
        return false;
    }

    const auto& buffers = allocator_->buffers(stream);
    std::cout << "[LibcameraCapture] Allocated " << buffers.size() << " buffers" << std::endl;

    // Memory-map the buffers.
    // NV12 has 2 planes (Y + UV) that may share the same DMA-BUF fd.
    // We mmap each unique fd once at offset 0 for its full size,
    // then compute per-plane data pointers using plane.offset.
    std::map<int, void*> fdMap;  // fd -> mmap'd base pointer

    for (const auto& buffer : buffers) {
        std::vector<MappedPlane> planes;

        for (const auto& plane : buffer->planes()) {
            int fd = plane.fd.get();
            void* base = nullptr;

            auto it = fdMap.find(fd);
            if (it != fdMap.end()) {
                base = it->second;
            } else {
                // Get the total size of this fd
                off_t fdSize = lseek(fd, 0, SEEK_END);
                if (fdSize <= 0) {
                    // Fallback: use offset + length as size estimate
                    fdSize = plane.offset + plane.length;
                }

                base = mmap(nullptr, fdSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (base == MAP_FAILED) {
                    std::cerr << "[LibcameraCapture] Failed to mmap fd " << fd
                              << " (size=" << fdSize << "): " << strerror(errno) << std::endl;
                    return false;
                }

                fdMap[fd] = base;
                mmapRegions_.push_back({base, static_cast<size_t>(fdSize)});
            }

            // Plane data is at base + plane.offset
            planes.push_back({
                static_cast<uint8_t*>(base) + plane.offset,
                plane.length
            });
        }

        bufferPlanes_[buffer.get()] = std::move(planes);
    }

    // Create requests
    for (const auto& buffer : buffers) {
        auto request = camera_->createRequest();
        if (!request) {
            std::cerr << "[LibcameraCapture] Failed to create request" << std::endl;
            return false;
        }
        ret = request->addBuffer(stream, buffer.get());
        if (ret != 0) {
            std::cerr << "[LibcameraCapture] Failed to add buffer to request" << std::endl;
            return false;
        }
        requests_.push_back(std::move(request));
    }

    // Connect signal for completed requests
    camera_->requestCompleted.connect(this, &LibcameraCapture::requestComplete);

    opened_ = true;
    return true;
}

bool LibcameraCapture::start() {
    if (!opened_) return false;
    if (started_) return true;

    // Set framerate via FrameDurationLimits (in microseconds)
    // This forces the sensor to deliver frames at the configured fps.
    libcamera::ControlList controls;
    int64_t frameDurationUs = 1000000 / config_.fps;
    controls.set(libcamera::controls::FrameDurationLimits,
                 libcamera::Span<const int64_t, 2>({frameDurationUs, frameDurationUs}));

    int ret = camera_->start(&controls);
    if (ret != 0) {
        std::cerr << "[LibcameraCapture] Failed to start camera: " << ret << std::endl;
        return false;
    }

    std::cout << "[LibcameraCapture] Framerate set to " << config_.fps
              << " fps (frame duration " << frameDurationUs << " us)" << std::endl;

    // Queue all requests
    for (auto& req : requests_) {
        ret = camera_->queueRequest(req.get());
        if (ret != 0) {
            std::cerr << "[LibcameraCapture] Failed to queue request: " << ret << std::endl;
            return false;
        }
    }

    started_ = true;
    return true;
}

bool LibcameraCapture::stop() {
    if (!started_) return true;

    camera_->stop();
    started_ = false;
    return true;
}

void LibcameraCapture::requestComplete(libcamera::Request* request) {
    if (request->status() == libcamera::Request::RequestCancelled) {
        return;
    }

    const auto& reqBuffers = request->buffers();
    for (auto& [stream, buffer] : reqBuffers) {
        // Look up the pre-computed plane mappings for this specific buffer
        auto it = bufferPlanes_.find(buffer);
        if (it == bufferPlanes_.end()) continue;
        const auto& planes = it->second;

        const auto& meta = buffer->metadata();

        // Prepare frame data
        size_t totalSize = 0;
        for (const auto& p : meta.planes()) {
            totalSize += p.bytesused;
        }

        // Create new frame
        Frame newFrame;
        newFrame.data.resize(totalSize);

        // Copy from the correct mmap'd planes
        size_t offset = 0;
        for (size_t i = 0; i < meta.planes().size() && i < planes.size(); i++) {
            size_t copySize = std::min(
                static_cast<size_t>(meta.planes()[i].bytesused),
                planes[i].length);
            std::memcpy(newFrame.data.data() + offset,
                       planes[i].data, copySize);
            offset += copySize;
        }

        newFrame.width = config_.width;
        newFrame.height = config_.height;
        newFrame.stride = config_.width;
        newFrame.pixelFormat = "NV12";
        newFrame.pts = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        // Add to circular queue with lock
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            
            // If queue is full, drop the oldest frame
            if (queueCount_ >= kMaxQueueSize) {
                queueHead_ = (queueHead_ + 1) % kMaxQueueSize;
                queueCount_--;
                droppedFrames_++;
            }
            
            // Ensure queue has space
            if (frameQueue_.size() < kMaxQueueSize) {
                frameQueue_.push_back(std::move(newFrame));
            } else {
                frameQueue_[queueTail_] = std::move(newFrame);
            }
            queueTail_ = (queueTail_ + 1) % kMaxQueueSize;
            queueCount_++;
            
            // Also update legacy single-frame for compatibility
            if (!frameQueue_.empty()) {
                latestFrame_ = frameQueue_[(queueTail_ + kMaxQueueSize - 1) % kMaxQueueSize];
                frameReady_ = true;
            }
        }
        
        frameCv_.notify_one();
    }

    // Re-queue the request for continuous capture
    request->reuse(libcamera::Request::ReuseBuffers);
    camera_->queueRequest(request);
}

Frame LibcameraCapture::captureFrame() {
    std::unique_lock<std::mutex> lock(frameMutex_);
    // Wait up to 100ms for a new frame
    frameCv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
        return queueCount_ > 0;
    });
    
    if (queueCount_ == 0) {
        return Frame{}; // timeout, no frame
    }
    
    // Get the latest frame from queue (not the oldest, to reduce latency)
    // Take from tail-1 (most recent) rather than head (oldest)
    size_t idx = (queueTail_ + kMaxQueueSize - 1) % kMaxQueueSize;
    Frame result = std::move(frameQueue_[idx]);
    
    // Clear the queue to prevent buildup
    queueCount_ = 0;
    queueHead_ = 0;
    queueTail_ = 0;
    frameReady_ = false;
    
    return result;
}

bool LibcameraCapture::isOpen() const {
    return opened_;
}

std::string LibcameraCapture::getName() const {
    return "LibcameraCapture (RPi5 CSI)";
}

} // namespace reallive
