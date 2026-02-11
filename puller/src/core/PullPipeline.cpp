#include "core/PullPipeline.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

// Platform-specific includes (conditionally compiled)
#include "platform/rpi5/FFmpegReceiver.h"
#include "platform/rpi5/Mp4Storage.h"
#ifdef ENABLE_HW_DECODE
#include "platform/rpi5/V4L2Decoder.h"
#endif

namespace reallive {
namespace puller {

PullPipeline::PullPipeline() = default;

PullPipeline::~PullPipeline() {
    stop();
}

bool PullPipeline::init(const PullerConfig& config) {
    config_ = config;

    // Create platform-specific components
    receiver_ = std::make_unique<FFmpegReceiver>();
    storage_ = std::make_unique<Mp4Storage>();

#ifdef ENABLE_HW_DECODE
    if (config_.hardwareDecode) {
        decoder_ = std::make_unique<V4L2Decoder>();
    }
#endif

    // Connect to stream
    std::string streamUrl = Config::buildStreamUrl(config_);
    std::cout << "[Pipeline] Connecting to: " << streamUrl << std::endl;

    if (!receiver_->connect(streamUrl)) {
        std::cerr << "[Pipeline] Failed to connect to stream." << std::endl;
        return false;
    }

    streamInfo_ = receiver_->getStreamInfo();
    std::cout << "[Pipeline] Stream info: "
              << streamInfo_.width << "x" << streamInfo_.height
              << " @ " << streamInfo_.fps << " fps" << std::endl;

    // Initialize decoder if present
    if (decoder_) {
        if (!decoder_->init(streamInfo_)) {
            std::cerr << "[Pipeline] Failed to init decoder, disabling." << std::endl;
            decoder_.reset();
        }
    }

    // Ensure output directory exists
    std::filesystem::create_directories(config_.outputDir);

    return true;
}

bool PullPipeline::start() {
    if (running_.load()) {
        std::cerr << "[Pipeline] Already running." << std::endl;
        return false;
    }

    // Start receiver
    if (!receiver_->start()) {
        std::cerr << "[Pipeline] Failed to start receiver." << std::endl;
        return false;
    }

    // Open first storage segment
    std::string filename = generateSegmentFilename();
    if (!storage_->open(filename, streamInfo_)) {
        std::cerr << "[Pipeline] Failed to open storage: " << filename << std::endl;
        return false;
    }

    segmentStartTime_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    segmentIndex_ = 0;

    running_.store(true);
    workerThread_ = std::thread(&PullPipeline::workerLoop, this);

    std::cout << "[Pipeline] Started." << std::endl;
    return true;
}

void PullPipeline::stop() {
    if (!running_.load()) return;

    running_.store(false);

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    if (receiver_) receiver_->stop();
    if (decoder_) decoder_->flush();
    if (storage_) storage_->close();

    std::cout << "[Pipeline] Stopped." << std::endl;
}

bool PullPipeline::isRunning() const {
    return running_.load();
}

void PullPipeline::workerLoop() {
    std::cout << "[Pipeline] Worker loop started." << std::endl;

    while (running_.load()) {
        EncodedPacket packet = receiver_->receivePacket();
        if (packet.empty()) {
            // Stream ended or error
            std::cout << "[Pipeline] Stream ended or empty packet." << std::endl;
            break;
        }

        // Check if we need to rotate to a new segment
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t elapsed = now - segmentStartTime_;

        if (!rotateSegmentIfNeeded(elapsed)) {
            std::cerr << "[Pipeline] Segment rotation failed, stopping." << std::endl;
            break;
        }

        // Direct storage mode: write encoded packets directly to container
        if (!storage_->writePacket(packet)) {
            std::cerr << "[Pipeline] Failed to write packet." << std::endl;
            // Non-fatal, continue
        }
    }

    running_.store(false);
    std::cout << "[Pipeline] Worker loop exited." << std::endl;
}

std::string PullPipeline::generateSegmentFilename() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << config_.outputDir << "/"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << "_" << std::setfill('0') << std::setw(4) << segmentIndex_
        << "." << config_.format;

    return oss.str();
}

bool PullPipeline::rotateSegmentIfNeeded(int64_t elapsedSeconds) {
    if (config_.segmentDuration <= 0) return true;
    if (elapsedSeconds < config_.segmentDuration) return true;

    std::cout << "[Pipeline] Rotating segment after " << elapsedSeconds << "s" << std::endl;

    // Close current segment
    storage_->close();

    // Update tracking
    segmentIndex_++;
    segmentStartTime_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Open new segment
    std::string filename = generateSegmentFilename();
    if (!storage_->open(filename, streamInfo_)) {
        std::cerr << "[Pipeline] Failed to open new segment: " << filename << std::endl;
        return false;
    }

    std::cout << "[Pipeline] New segment: " << filename << std::endl;
    return true;
}

} // namespace puller
} // namespace reallive
