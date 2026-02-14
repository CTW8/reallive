#pragma once

#include "core/Config.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace reallive {

class ControlServer {
public:
    explicit ControlServer(const PusherConfig& config);
    ~ControlServer();

    bool start();
    void stop();
    bool isRunning() const;

private:
    struct Segment {
        std::string filePath;
        std::string fileName;
        int64_t startMs = 0;
        int64_t endMs = 0;
        int64_t durationMs = 0;
        std::string thumbnailPath;
    };

    struct ReplaySession {
        std::string sessionId;
        std::string streamKey;
        std::string streamName;
        std::string sourceFile;
        std::string playbackUrl;
        int64_t requestedTs = 0;
        int offsetSec = 0;
        int pid = -1;
        int64_t startedAtMs = 0;
    };

    void serveLoop();
    void handleClient(int clientFd);

    std::string handleRequest(
        const std::string& method,
        const std::string& pathWithQuery,
        const std::string& body,
        int& statusCode
    );

    std::string handleOverview(const std::string& streamKey);
    std::string handleTimeline(const std::string& streamKey, int64_t startMs, int64_t endMs);
    std::string handleReplayStart(const std::string& streamKey, int64_t tsMs);
    std::string handleReplayStop(const std::string& streamKey, const std::string& sessionId);

    std::vector<Segment> loadSegments(const std::string& streamKey) const;
    static std::string sanitizeStreamKey(const std::string& raw);
    static int64_t nowMs();
    static std::string jsonEscape(const std::string& input);

    void reapExitedSessions();
    void terminateSession(const std::string& sessionId);
    void terminateAllSessions();

    PusherConfig config_;
    std::atomic<bool> running_{false};
    int serverFd_ = -1;
    std::thread serverThread_;
    mutable std::mutex sessionMutex_;
    std::unordered_map<std::string, ReplaySession> sessions_;
};

} // namespace reallive

