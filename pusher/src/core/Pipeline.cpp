#include "core/Pipeline.h"
#include "core/TextOverlay.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <algorithm>

// Platform-specific includes (Raspberry Pi 5)
#include "platform/rpi5/LibcameraCapture.h"
#include "platform/rpi5/AvcodecEncoder.h"
#include "platform/rpi5/AlsaCapture.h"
#include "platform/rpi5/RtmpStreamer.h"

namespace reallive {

Pipeline::Pipeline() = default;

Pipeline::~Pipeline() {
    stop();
}

bool Pipeline::createComponents(const PusherConfig& config) {
    // Create platform-specific implementations for Raspberry Pi 5
    camera_ = std::make_unique<LibcameraCapture>();
    encoder_ = std::make_unique<AvcodecEncoder>();
    streamer_ = std::make_unique<RtmpStreamer>();

    if (config.enableAudio) {
        audio_ = std::make_unique<AlsaCapture>();
    }

    return true;
}

bool Pipeline::init(const PusherConfig& config) {
    config_ = config;

    if (!createComponents(config)) {
        std::cerr << "[Pipeline] Failed to create components" << std::endl;
        return false;
    }

    // Initialize camera
    if (!camera_->open(config.camera)) {
        std::cerr << "[Pipeline] Failed to open camera" << std::endl;
        return false;
    }
    std::cout << "[Pipeline] Camera opened: " << camera_->getName() << std::endl;

    // Initialize encoder
    if (!encoder_->init(config.encoder)) {
        std::cerr << "[Pipeline] Failed to init encoder" << std::endl;
        return false;
    }
    std::cout << "[Pipeline] Encoder initialized: " << encoder_->getName() << std::endl;

    // Initialize audio if enabled
    if (config.enableAudio && audio_) {
        if (!audio_->open(config.audio)) {
            std::cerr << "[Pipeline] Failed to open audio (continuing without audio)" << std::endl;
            audio_.reset();
        } else {
            std::cout << "[Pipeline] Audio opened: " << audio_->getName() << std::endl;
        }
    }
    config_.stream.enableAudio = config.enableAudio && audio_ != nullptr;

    // Pass encoder extradata (SPS/PPS) to the streamer for FLV header
    auto* avEncoder = dynamic_cast<AvcodecEncoder*>(encoder_.get());
    if (avEncoder) {
        config_.stream.videoExtraData = avEncoder->getExtraData();
        config_.stream.videoExtraDataSize = avEncoder->getExtraDataSize();
        config_.stream.videoWidth = config.encoder.width;
        config_.stream.videoHeight = config.encoder.height;
    }

    // Connect to streaming server
    if (!streamer_->connect(config_.stream)) {
        std::cerr << "[Pipeline] Failed to connect to server: "
                  << config.stream.url << std::endl;
        return false;
    }
    std::cout << "[Pipeline] Connected to: " << config.stream.url << std::endl;

    return true;
}

bool Pipeline::start() {
    if (running_) {
        std::cerr << "[Pipeline] Already running" << std::endl;
        return false;
    }

    // Start camera capture
    if (!camera_->start()) {
        std::cerr << "[Pipeline] Failed to start camera" << std::endl;
        return false;
    }

    // Start audio capture
    if (audio_ && !audio_->start()) {
        std::cerr << "[Pipeline] Failed to start audio (continuing without audio)" << std::endl;
        audio_.reset();
    }

    running_ = true;
    framesSent_ = 0;
    bytesSent_ = 0;

    // Launch video capture/encode/stream thread
    videoThread_ = std::thread(&Pipeline::videoLoop, this);

    // Launch audio thread if enabled
    if (audio_) {
        audioThread_ = std::thread(&Pipeline::audioLoop, this);
    }

    std::cout << "[Pipeline] Started streaming" << std::endl;
    return true;
}

void Pipeline::stop() {
    if (!running_) return;

    running_ = false;

    if (videoThread_.joinable()) {
        videoThread_.join();
    }
    if (audioThread_.joinable()) {
        audioThread_.join();
    }

    // Stop components in reverse order
    if (streamer_ && streamer_->isConnected()) {
        streamer_->disconnect();
    }
    if (audio_ && audio_->isOpen()) {
        audio_->stop();
    }
    if (encoder_) {
        encoder_->flush();
    }
    if (camera_ && camera_->isOpen()) {
        camera_->stop();
    }

    std::cout << "[Pipeline] Stopped. Frames sent: " << framesSent_.load()
              << ", Bytes sent: " << bytesSent_.load() << std::endl;
}

void Pipeline::videoLoop() {
    using Clock = std::chrono::steady_clock;
    auto lastFpsTime = Clock::now();
    auto lastLogTime = Clock::now();
    uint64_t fpsFrameCount = 0;
    uint64_t droppedFrames = 0;
    uint64_t totalProcessTime = 0;
    uint64_t maxProcessTime = 0;

    const auto maxProcessThreshold = std::chrono::microseconds(1000000 / config_.camera.fps * 2); // 允许最大2倍帧间隔
    
    // 统计窗口
    const int statsWindow = config_.camera.fps * 5; // 5秒统计窗口
    std::vector<uint64_t> processTimes;
    processTimes.reserve(statsWindow);
    uint64_t lastCaptureWait = 0;

    while (running_) {
        auto frameStart = Clock::now();

        // 1. Capture a frame from camera (blocks until frame ready or timeout)
        Frame frame = camera_->captureFrame();
        if (frame.empty()) {
            continue;
        }

        // 检查是否超时 - 如果累积延迟过大，选择性丢帧
        auto captureTime = Clock::now();
        auto waitTime = std::chrono::duration_cast<std::chrono::microseconds>(captureTime - frameStart);
        lastCaptureWait = waitTime.count();
        
        // 2. Draw timestamp overlay on frame before encoding
        TextOverlay::drawTimestamp(frame.data.data(), frame.width, frame.height);

        // 3. Encode the frame
        auto encodeStart = Clock::now();
        EncodedPacket packet = encoder_->encode(frame);
        auto encodeEnd = Clock::now();
        
        if (packet.empty()) {
            continue;
        }

        // Record timing information for latency tracking
        packet.captureTime = std::chrono::duration_cast<std::chrono::microseconds>(captureTime.time_since_epoch()).count();
        packet.encodeTime = std::chrono::duration_cast<std::chrono::microseconds>(encodeEnd - encodeStart).count();

        // 4. Send the encoded packet
        if (!streamer_->sendVideoPacket(packet)) {
            std::cerr << "[Pipeline] Failed to send video packet" << std::endl;
            if (!streamer_->isConnected()) {
                std::cerr << "[Pipeline] Streamer disconnected, stopping" << std::endl;
                running_ = false;
                break;
            }
            continue;
        }

        framesSent_++;
        bytesSent_ += packet.data.size();
        fpsFrameCount++;

        // 计算处理时间
        auto frameEnd = Clock::now();
        auto processTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
        auto encodeTime = std::chrono::duration_cast<std::chrono::microseconds>(encodeEnd - encodeStart);
        
        totalProcessTime += processTime.count();
        maxProcessTime = std::max(maxProcessTime, static_cast<uint64_t>(processTime.count()));
        
        // 保持统计窗口
        processTimes.push_back(processTime.count());
        if (processTimes.size() > static_cast<size_t>(statsWindow)) {
            processTimes.erase(processTimes.begin());
        }

        // 如果持续处理慢，记录丢帧
        if (processTime > maxProcessThreshold) {
            droppedFrames++;
            if (droppedFrames % 30 == 0) {
                std::cerr << "[Pipeline] WARNING: Frame processing too slow (" 
                          << processTime.count() / 1000 << "ms), dropped " 
                          << droppedFrames << " frames so far" << std::endl;
            }
        }

        // 每秒计算FPS和打印详细统计
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime);
        if (elapsed.count() >= 1000) {
            currentFps_ = static_cast<double>(fpsFrameCount) * 1000.0 / elapsed.count();
            fpsFrameCount = 0;
            lastFpsTime = now;
        }

        // 每5秒打印详细性能统计
        auto logElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime);
        if (logElapsed.count() >= 5) {
            // 计算平均处理时间
            uint64_t avgProcessTime = 0;
            if (!processTimes.empty()) {
                uint64_t sum = 0;
                for (auto t : processTimes) sum += t;
                avgProcessTime = sum / processTimes.size();
            }
            
            // 计算P99延迟
            uint64_t p99Time = 0;
            if (!processTimes.empty()) {
                std::vector<uint64_t> sorted = processTimes;
                std::sort(sorted.begin(), sorted.end());
                p99Time = sorted[static_cast<size_t>(sorted.size() * 0.99)];
            }

            std::cout << "[Pipeline Stats] FPS: " << std::fixed << std::setprecision(1) << currentFps_
                      << " | Frame: " << framesSent_.load()
                      << " | Bytes: " << (bytesSent_.load() / 1024 / 1024) << " MB"
                      << " | Dropped: " << droppedFrames
                      << " | CaptureWait: " << lastCaptureWait / 1000 << "ms"
                      << " | Encode: " << encodeTime.count() / 1000 << "ms"
                      << " | AvgProcess: " << avgProcessTime / 1000 << "ms"
                      << " | MaxProcess: " << maxProcessTime / 1000 << "ms"
                      << " | P99: " << p99Time / 1000 << "ms" << std::endl;
            
            lastLogTime = now;
            totalProcessTime = 0;
            maxProcessTime = 0;
        }
    }
}

void Pipeline::audioLoop() {
    while (running_ && audio_) {
        AudioFrame audioFrame = audio_->captureFrame();
        if (audioFrame.empty()) {
            continue;
        }

        if (!streamer_->sendAudioPacket(audioFrame)) {
            std::cerr << "[Pipeline] Failed to send audio packet" << std::endl;
        }
    }
}

bool Pipeline::isRunning() const {
    return running_;
}

uint64_t Pipeline::getFramesSent() const {
    return framesSent_;
}

uint64_t Pipeline::getBytesSent() const {
    return bytesSent_;
}

double Pipeline::getCurrentFps() const {
    return currentFps_;
}

} // namespace reallive
