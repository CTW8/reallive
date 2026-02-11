#include "platform/rpi5/V4L2Encoder.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <chrono>

namespace reallive {

// V4L2 M2M encoder device on Raspberry Pi 5
static const char* kEncoderDevice = "/dev/video11";

V4L2Encoder::V4L2Encoder() = default;

V4L2Encoder::~V4L2Encoder() {
    flush();
    // Unmap and cleanup
    for (int i = 0; i < kOutputBufferCount; i++) {
        if (outputBuffers_[i].ptr && outputBuffers_[i].ptr != MAP_FAILED) {
            munmap(outputBuffers_[i].ptr, outputBuffers_[i].length);
        }
    }
    for (int i = 0; i < kCaptureBufferCount; i++) {
        if (captureBuffers_[i].ptr && captureBuffers_[i].ptr != MAP_FAILED) {
            munmap(captureBuffers_[i].ptr, captureBuffers_[i].length);
        }
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool V4L2Encoder::init(const EncoderConfig& config) {
    config_ = config;

    // Open the V4L2 M2M encoder device
    fd_ = open(kEncoderDevice, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "[V4L2Encoder] Failed to open " << kEncoderDevice
                  << ": " << strerror(errno) << std::endl;
        return false;
    }

    // Verify it's a M2M device
    struct v4l2_capability cap;
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[V4L2Encoder] VIDIOC_QUERYCAP failed" << std::endl;
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
        std::cerr << "[V4L2Encoder] Device does not support M2M multiplanar" << std::endl;
        return false;
    }

    std::cout << "[V4L2Encoder] Using device: " << cap.card << std::endl;

    if (!setupOutputFormat()) return false;
    if (!setupCaptureFormat()) return false;

    // Set bitrate via V4L2 control
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_MPEG_VIDEO_BITRATE;
    ctrl.value = config.bitrate;
    if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[V4L2Encoder] Warning: Failed to set bitrate" << std::endl;
    }

    // Set H.264 profile
    ctrl.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
    if (config.profile == "baseline") {
        ctrl.value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    } else if (config.profile == "high") {
        ctrl.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
    } else {
        ctrl.value = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
    }
    if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[V4L2Encoder] Warning: Failed to set H.264 profile" << std::endl;
    }

    // Set GOP size (keyframe interval)
    ctrl.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE;
    ctrl.value = config.gopSize;
    if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[V4L2Encoder] Warning: Failed to set GOP size" << std::endl;
    }

    // Set repeat sequence header (SPS/PPS with each keyframe)
    ctrl.id = V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER;
    ctrl.value = 1;
    if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[V4L2Encoder] Warning: Failed to set repeat sequence header" << std::endl;
    }

    if (!allocateBuffers()) return false;
    if (!startStreaming()) return false;

    initialized_ = true;
    std::cout << "[V4L2Encoder] Initialized: " << config.width << "x" << config.height
              << " @ " << config.bitrate / 1000 << " kbps, profile=" << config.profile
              << std::endl;
    return true;
}

bool V4L2Encoder::setupOutputFormat() {
    // OUTPUT = raw video input (NV12)
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width = config_.width;
    fmt.fmt.pix_mp.height = config_.height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = config_.width * config_.height * 3 / 2;
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline = config_.width;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2Encoder] Failed to set output format: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool V4L2Encoder::setupCaptureFormat() {
    // CAPTURE = encoded H.264 output
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = config_.width;
    fmt.fmt.pix_mp.height = config_.height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = config_.width * config_.height; // generous buffer
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2Encoder] Failed to set capture format: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool V4L2Encoder::allocateBuffers() {
    // Allocate OUTPUT buffers (raw input)
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = kOutputBufferCount;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        std::cerr << "[V4L2Encoder] Failed to request output buffers" << std::endl;
        return false;
    }

    for (int i = 0; i < kOutputBufferCount; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[V4L2Encoder] QUERYBUF failed for output " << i << std::endl;
            return false;
        }

        outputBuffers_[i].length = planes[0].length;
        outputBuffers_[i].ptr = mmap(nullptr, planes[0].length,
                                     PROT_READ | PROT_WRITE, MAP_SHARED,
                                     fd_, planes[0].m.mem_offset);
        if (outputBuffers_[i].ptr == MAP_FAILED) {
            std::cerr << "[V4L2Encoder] mmap failed for output " << i << std::endl;
            return false;
        }
    }

    // Allocate CAPTURE buffers (encoded output)
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = kCaptureBufferCount;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        std::cerr << "[V4L2Encoder] Failed to request capture buffers" << std::endl;
        return false;
    }

    for (int i = 0; i < kCaptureBufferCount; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[V4L2Encoder] QUERYBUF failed for capture " << i << std::endl;
            return false;
        }

        captureBuffers_[i].length = planes[0].length;
        captureBuffers_[i].ptr = mmap(nullptr, planes[0].length,
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      fd_, planes[0].m.mem_offset);
        if (captureBuffers_[i].ptr == MAP_FAILED) {
            std::cerr << "[V4L2Encoder] mmap failed for capture " << i << std::endl;
            return false;
        }
    }

    return true;
}

bool V4L2Encoder::startStreaming() {
    // Queue all capture buffers
    for (int i = 0; i < kCaptureBufferCount; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[V4L2Encoder] Failed to queue capture buffer " << i << std::endl;
            return false;
        }
    }

    // Start streaming on both queues
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[V4L2Encoder] STREAMON failed for output" << std::endl;
        return false;
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[V4L2Encoder] STREAMON failed for capture" << std::endl;
        return false;
    }

    return true;
}

EncodedPacket V4L2Encoder::encode(const Frame& frame) {
    if (!initialized_ || frame.empty()) {
        return EncodedPacket{};
    }

    int bufIdx = frameCount_ % kOutputBufferCount;

    // Copy raw NV12 frame data to output buffer
    size_t copySize = std::min(frame.data.size(), outputBuffers_[bufIdx].length);
    memcpy(outputBuffers_[bufIdx].ptr, frame.data.data(), copySize);

    // Queue the output buffer (raw frame input)
    struct v4l2_buffer outBuf;
    struct v4l2_plane outPlanes[1];
    memset(&outBuf, 0, sizeof(outBuf));
    memset(&outPlanes, 0, sizeof(outPlanes));
    outBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    outBuf.memory = V4L2_MEMORY_MMAP;
    outBuf.index = bufIdx;
    outBuf.length = 1;
    outBuf.m.planes = outPlanes;
    outPlanes[0].bytesused = copySize;
    outPlanes[0].length = outputBuffers_[bufIdx].length;

    // Set timestamp for frame pacing
    int64_t usec = frameCount_ * 1000000LL / config_.fps;
    outBuf.timestamp.tv_sec = usec / 1000000;
    outBuf.timestamp.tv_usec = usec % 1000000;

    if (ioctl(fd_, VIDIOC_QBUF, &outBuf) < 0) {
        std::cerr << "[V4L2Encoder] Failed to queue output buffer" << std::endl;
        return EncodedPacket{};
    }

    // Dequeue encoded data from capture buffer
    struct v4l2_buffer capBuf;
    struct v4l2_plane capPlanes[1];
    memset(&capBuf, 0, sizeof(capBuf));
    memset(&capPlanes, 0, sizeof(capPlanes));
    capBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    capBuf.memory = V4L2_MEMORY_MMAP;
    capBuf.length = 1;
    capBuf.m.planes = capPlanes;

    // Poll with timeout for encoded output
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ret = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        std::cerr << "[V4L2Encoder] Timeout waiting for encoded frame" << std::endl;
        return EncodedPacket{};
    }

    if (ioctl(fd_, VIDIOC_DQBUF, &capBuf) < 0) {
        std::cerr << "[V4L2Encoder] Failed to dequeue capture buffer" << std::endl;
        return EncodedPacket{};
    }

    // Also dequeue the used output buffer
    struct v4l2_buffer usedOutBuf;
    struct v4l2_plane usedOutPlanes[1];
    memset(&usedOutBuf, 0, sizeof(usedOutBuf));
    memset(&usedOutPlanes, 0, sizeof(usedOutPlanes));
    usedOutBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    usedOutBuf.memory = V4L2_MEMORY_MMAP;
    usedOutBuf.length = 1;
    usedOutBuf.m.planes = usedOutPlanes;
    ioctl(fd_, VIDIOC_DQBUF, &usedOutBuf); // best effort

    // Build encoded packet
    EncodedPacket packet;
    size_t encodedSize = capPlanes[0].bytesused;
    int capIdx = capBuf.index;

    packet.data.resize(encodedSize);
    memcpy(packet.data.data(), captureBuffers_[capIdx].ptr, encodedSize);
    packet.pts = frame.pts;
    packet.dts = frame.pts;
    packet.isKeyframe = (capBuf.flags & V4L2_BUF_FLAG_KEYFRAME) != 0;

    // Re-queue the capture buffer
    if (ioctl(fd_, VIDIOC_QBUF, &capBuf) < 0) {
        std::cerr << "[V4L2Encoder] Failed to re-queue capture buffer" << std::endl;
    }

    frameCount_++;
    return packet;
}

void V4L2Encoder::flush() {
    if (fd_ < 0) return;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctl(fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd_, VIDIOC_STREAMOFF, &type);

    initialized_ = false;
}

std::string V4L2Encoder::getName() const {
    return "V4L2 M2M H.264 Encoder (RPi5)";
}

} // namespace reallive
