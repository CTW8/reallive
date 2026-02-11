#include "platform/rpi5/LibcameraCapture.h"

#include <libcamera/libcamera.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <condition_variable>

namespace reallive {

LibcameraCapture::LibcameraCapture() = default;

LibcameraCapture::~LibcameraCapture() {
    stop();
    if (camera_) {
        camera_->release();
    }
    if (cameraManager_) {
        cameraManager_->stop();
    }
    // Unmap buffers
    for (auto& mb : mappedBuffers_) {
        if (mb.ptr && mb.ptr != MAP_FAILED) {
            munmap(mb.ptr, mb.length);
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

    // Use first camera (or match by device string)
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
    // Use NV12 pixel format for V4L2 M2M encoder compatibility
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

    std::cout << "[LibcameraCapture] Allocated " << allocator_->buffers(stream).size()
              << " buffers" << std::endl;

    // Memory-map the buffers
    const auto& buffers = allocator_->buffers(stream);
    for (const auto& buffer : buffers) {
        for (const auto& plane : buffer->planes()) {
            void* ptr = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                             plane.fd.get(), plane.offset);
            if (ptr == MAP_FAILED) {
                std::cerr << "[LibcameraCapture] Failed to mmap buffer" << std::endl;
                return false;
            }
            mappedBuffers_.push_back({ptr, plane.length});
        }
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

    int ret = camera_->start();
    if (ret != 0) {
        std::cerr << "[LibcameraCapture] Failed to start camera: " << ret << std::endl;
        return false;
    }

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

    const auto& buffers = request->buffers();
    for (auto& [stream, buffer] : buffers) {
        const auto& planes = buffer->planes();
        if (planes.empty()) continue;

        // Read the frame data from memory-mapped buffer
        const auto& meta = buffer->metadata();

        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_.data.clear();

        size_t totalSize = 0;
        for (const auto& plane : meta.planes()) {
            totalSize += plane.bytesused;
        }
        latestFrame_.data.resize(totalSize);

        // Copy from mmap'd planes
        size_t offset = 0;
        size_t bufIdx = 0;
        for (const auto& plane : meta.planes()) {
            if (bufIdx < mappedBuffers_.size()) {
                size_t copySize = std::min(plane.bytesused,
                                           static_cast<unsigned int>(mappedBuffers_[bufIdx].length));
                std::memcpy(latestFrame_.data.data() + offset,
                           mappedBuffers_[bufIdx].ptr, copySize);
                offset += copySize;
            }
            bufIdx++;
        }

        latestFrame_.width = config_.width;
        latestFrame_.height = config_.height;
        latestFrame_.stride = config_.width;
        latestFrame_.pixelFormat = "NV12";
        latestFrame_.pts = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        frameReady_ = true;
    }

    // Re-queue the request for continuous capture
    request->reuse(libcamera::Request::ReuseBuffers);
    camera_->queueRequest(request);
}

Frame LibcameraCapture::captureFrame() {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!frameReady_) {
        return Frame{}; // empty frame
    }
    frameReady_ = false;
    return std::move(latestFrame_);
}

bool LibcameraCapture::isOpen() const {
    return opened_;
}

std::string LibcameraCapture::getName() const {
    return "LibcameraCapture (RPi5 CSI)";
}

} // namespace reallive
