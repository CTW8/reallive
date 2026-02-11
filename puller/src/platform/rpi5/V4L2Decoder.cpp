#include "platform/rpi5/V4L2Decoder.h"

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

namespace reallive {
namespace puller {

V4L2Decoder::V4L2Decoder() = default;

V4L2Decoder::~V4L2Decoder() {
    flush();

    // Unmap and close
    for (int i = 0; i < OUTPUT_BUFFER_COUNT; i++) {
        if (outputBuffers_[i].start) {
            munmap(outputBuffers_[i].start, outputBuffers_[i].length);
        }
    }
    for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
        if (captureBuffers_[i].start) {
            munmap(captureBuffers_[i].start, captureBuffers_[i].length);
        }
    }

    if (fd_ >= 0) {
        close(fd_);
    }
}

bool V4L2Decoder::init(const StreamInfo& info) {
    width_ = info.width;
    height_ = info.height;
    codec_ = info.videoCodec;

    // Try common RPi5 decoder device paths
    const char* devicePaths[] = {
        "/dev/video10",  // RPi stateful decoder
        "/dev/video11",
        nullptr
    };

    for (int i = 0; devicePaths[i]; i++) {
        if (openDevice(devicePaths[i])) {
            std::cout << "[V4L2Decoder] Opened device: " << devicePaths[i] << std::endl;
            break;
        }
    }

    if (fd_ < 0) {
        std::cerr << "[V4L2Decoder] No suitable V4L2 M2M device found." << std::endl;
        return false;
    }

    if (!setupOutputQueue()) {
        std::cerr << "[V4L2Decoder] Failed to set up output queue." << std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (!setupCaptureQueue()) {
        std::cerr << "[V4L2Decoder] Failed to set up capture queue." << std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Start streaming on both queues
    enum v4l2_buf_type outType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    enum v4l2_buf_type capType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (ioctl(fd_, VIDIOC_STREAMON, &outType) < 0) {
        std::cerr << "[V4L2Decoder] STREAMON output failed: " << strerror(errno) << std::endl;
        return false;
    }
    if (ioctl(fd_, VIDIOC_STREAMON, &capType) < 0) {
        std::cerr << "[V4L2Decoder] STREAMON capture failed: " << strerror(errno) << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "[V4L2Decoder] Initialized for " << width_ << "x" << height_ << std::endl;
    return true;
}

Frame V4L2Decoder::decode(const EncodedPacket& packet) {
    Frame frame;
    if (!initialized_ || packet.type != MediaType::Video) return frame;

    if (!enqueueOutput(packet)) {
        std::cerr << "[V4L2Decoder] Failed to enqueue output buffer." << std::endl;
        return frame;
    }

    return dequeueCaptureFrame();
}

void V4L2Decoder::flush() {
    if (fd_ < 0) return;

    // Stop streaming
    enum v4l2_buf_type outType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    enum v4l2_buf_type capType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd_, VIDIOC_STREAMOFF, &outType);
    ioctl(fd_, VIDIOC_STREAMOFF, &capType);

    initialized_ = false;
}

bool V4L2Decoder::openDevice(const std::string& devicePath) {
    int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) return false;

    // Check capabilities
    struct v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        close(fd);
        return false;
    }

    // Must support M2M (video mem-to-mem)
    bool isM2M = (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) ||
                 ((cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) &&
                  (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE));

    if (!isM2M) {
        close(fd);
        return false;
    }

    fd_ = fd;
    return true;
}

bool V4L2Decoder::setupOutputQueue() {
    // Set output format (compressed)
    struct v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.num_planes = 1;

    switch (codec_) {
        case CodecType::H264:
            fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
            break;
        case CodecType::H265:
            fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_HEVC;
            break;
        default:
            std::cerr << "[V4L2Decoder] Unsupported codec for V4L2." << std::endl;
            return false;
    }

    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = width_ * height_; // compressed buffer size hint
    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2Decoder] S_FMT output failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Request buffers
    struct v4l2_requestbuffers reqbuf{};
    reqbuf.count = OUTPUT_BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        std::cerr << "[V4L2Decoder] REQBUFS output failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Map buffers
    for (int i = 0; i < OUTPUT_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf{};
        struct v4l2_plane planes[1]{};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[V4L2Decoder] QUERYBUF output failed." << std::endl;
            return false;
        }

        outputBuffers_[i].length = planes[0].length;
        outputBuffers_[i].start = mmap(nullptr, planes[0].length,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       fd_, planes[0].m.mem_offset);
        if (outputBuffers_[i].start == MAP_FAILED) {
            outputBuffers_[i].start = nullptr;
            return false;
        }
    }

    return true;
}

bool V4L2Decoder::setupCaptureQueue() {
    // Set capture format (raw decoded)
    struct v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width_;
    fmt.fmt.pix_mp.height = height_;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = width_ * height_ * 3 / 2;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2Decoder] S_FMT capture failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Request buffers
    struct v4l2_requestbuffers reqbuf{};
    reqbuf.count = CAPTURE_BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        std::cerr << "[V4L2Decoder] REQBUFS capture failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Map and enqueue capture buffers
    for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf{};
        struct v4l2_plane planes[1]{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            return false;
        }

        captureBuffers_[i].length = planes[0].length;
        captureBuffers_[i].start = mmap(nullptr, planes[0].length,
                                        PROT_READ | PROT_WRITE, MAP_SHARED,
                                        fd_, planes[0].m.mem_offset);
        if (captureBuffers_[i].start == MAP_FAILED) {
            captureBuffers_[i].start = nullptr;
            return false;
        }

        // Enqueue capture buffer
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            return false;
        }
    }

    return true;
}

bool V4L2Decoder::enqueueOutput(const EncodedPacket& packet) {
    int idx = outputBufferIndex_ % OUTPUT_BUFFER_COUNT;

    if (packet.data.size() > outputBuffers_[idx].length) {
        std::cerr << "[V4L2Decoder] Packet too large for buffer." << std::endl;
        return false;
    }

    memcpy(outputBuffers_[idx].start, packet.data.data(), packet.data.size());

    struct v4l2_buffer buf{};
    struct v4l2_plane planes[1]{};
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = idx;
    buf.m.planes = planes;
    buf.length = 1;
    planes[0].bytesused = packet.data.size();

    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "[V4L2Decoder] QBUF output failed: " << strerror(errno) << std::endl;
        return false;
    }

    outputBufferIndex_++;
    return true;
}

Frame V4L2Decoder::dequeueCaptureFrame() {
    Frame frame;

    struct v4l2_buffer buf{};
    struct v4l2_plane planes[1]{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        // EAGAIN is normal for non-blocking
        if (errno != EAGAIN) {
            std::cerr << "[V4L2Decoder] DQBUF capture failed: " << strerror(errno) << std::endl;
        }
        return frame;
    }

    int idx = buf.index;
    size_t bytesUsed = planes[0].bytesused;

    frame.type = MediaType::Video;
    frame.width = width_;
    frame.height = height_;
    frame.pixelFormat = PixelFormat::NV12;
    frame.data.assign(
        static_cast<uint8_t*>(captureBuffers_[idx].start),
        static_cast<uint8_t*>(captureBuffers_[idx].start) + bytesUsed);

    // Re-enqueue capture buffer
    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "[V4L2Decoder] Re-QBUF capture failed." << std::endl;
    }

    // Also dequeue the output buffer
    struct v4l2_buffer outbuf{};
    struct v4l2_plane outplanes[1]{};
    outbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    outbuf.memory = V4L2_MEMORY_MMAP;
    outbuf.m.planes = outplanes;
    outbuf.length = 1;
    ioctl(fd_, VIDIOC_DQBUF, &outbuf); // ignore error

    return frame;
}

} // namespace puller
} // namespace reallive
