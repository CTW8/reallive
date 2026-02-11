#include "core/Pipeline.h"

#include <iostream>
#include <chrono>

// Platform-specific includes (Raspberry Pi 5)
#include "platform/rpi5/LibcameraCapture.h"
#include "platform/rpi5/V4L2Encoder.h"
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
    encoder_ = std::make_unique<V4L2Encoder>();
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

    // Connect to streaming server
    if (!streamer_->connect(config.stream)) {
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
    uint64_t fpsFrameCount = 0;

    const auto frameDuration = std::chrono::microseconds(1000000 / config_.camera.fps);

    while (running_) {
        auto frameStart = Clock::now();

        // 1. Capture a frame from camera
        Frame frame = camera_->captureFrame();
        if (frame.empty()) {
            std::cerr << "[Pipeline] Empty frame captured, skipping" << std::endl;
            continue;
        }

        // 2. Encode the frame
        EncodedPacket packet = encoder_->encode(frame);
        if (packet.empty()) {
            continue;
        }

        // 3. Send the encoded packet
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

        // Calculate FPS every second
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime);
        if (elapsed.count() >= 1000) {
            currentFps_ = static_cast<double>(fpsFrameCount) * 1000.0 / elapsed.count();
            fpsFrameCount = 0;
            lastFpsTime = now;
        }

        // Pace to target frame rate
        auto frameEnd = Clock::now();
        auto processTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
        if (processTime < frameDuration) {
            std::this_thread::sleep_for(frameDuration - processTime);
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
