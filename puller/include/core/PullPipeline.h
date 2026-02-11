#pragma once

#include "../platform/IStreamReceiver.h"
#include "../platform/IDecoder.h"
#include "../platform/IStorage.h"
#include "Config.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace reallive {
namespace puller {

/// Pipeline that orchestrates: pull stream -> (optional decode) -> storage
class PullPipeline {
public:
    PullPipeline();
    ~PullPipeline();

    /// Initialize the pipeline with configuration
    bool init(const PullerConfig& config);

    /// Start the pipeline (begins pulling and storing)
    bool start();

    /// Stop the pipeline
    void stop();

    /// Check if the pipeline is currently running
    bool isRunning() const;

private:
    /// Main worker loop: receives packets and writes to storage
    void workerLoop();

    /// Generate a segment filename based on current time
    std::string generateSegmentFilename() const;

    /// Rotate to a new storage segment if needed
    bool rotateSegmentIfNeeded(int64_t elapsedSeconds);

    PullerConfig config_;
    std::unique_ptr<IStreamReceiver> receiver_;
    std::unique_ptr<IDecoder> decoder_;
    std::unique_ptr<IStorage> storage_;
    StreamInfo streamInfo_;

    std::thread workerThread_;
    std::atomic<bool> running_{false};

    // Segment tracking
    int64_t segmentStartTime_ = 0;
    int segmentIndex_ = 0;
};

} // namespace puller
} // namespace reallive
