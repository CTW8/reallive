#include "core/ControlServer.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace reallive {

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        const std::string kv = query.substr(pos, amp - pos);
        size_t eq = kv.find('=');
        if (eq == std::string::npos) {
            if (!kv.empty()) out[kv] = "";
        } else {
            out[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        pos = amp + 1;
    }
    return out;
}

std::string urlDecode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '+') {
            out.push_back(' ');
            continue;
        }
        if (c == '%' && i + 2 < input.size()) {
            auto hex = input.substr(i + 1, 2);
            char* end = nullptr;
            long v = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0' && v >= 0 && v <= 255) {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string statusText(int code) {
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

std::string makeHttpJsonResponse(int statusCode, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusText(statusCode) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
        << "Content-Length: " << body.size() << "\r\n\r\n"
        << body;
    return oss.str();
}

std::string jsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (char ch : s) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    out.push_back('"');
    return out;
}

std::string jsonExtractRaw(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return "";
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\r' || body[p] == '\n')) p++;
    if (p >= body.size()) return "";

    if (body[p] == '"') {
        std::string out;
        ++p;
        bool esc = false;
        for (; p < body.size(); ++p) {
            char ch = body[p];
            if (esc) {
                out.push_back(ch);
                esc = false;
            } else if (ch == '\\') {
                esc = true;
            } else if (ch == '"') {
                return out;
            } else {
                out.push_back(ch);
            }
        }
        return "";
    }

    size_t e = body.find_first_of(",}\r\n", p);
    if (e == std::string::npos) e = body.size();
    return trim(body.substr(p, e - p));
}

int64_t toInt64(const std::string& s, int64_t fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (!end || *end != '\0') return fallback;
    return static_cast<int64_t>(v);
}

} // namespace

ControlServer::ControlServer(const PusherConfig& config)
    : config_(config) {
}

ControlServer::~ControlServer() {
    stop();
}

bool ControlServer::start() {
    if (running_) return true;
    if (!config_.control.enabled) return true;

    serverFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::cerr << "[Control] socket() failed" << std::endl;
        return false;
    }

    int opt = 1;
    ::setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.control.port));
    if (config_.control.host.empty() || config_.control.host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, config_.control.host.c_str(), &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (::bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Control] bind() failed: " << std::strerror(errno) << std::endl;
        ::close(serverFd_);
        serverFd_ = -1;
        return false;
    }
    if (::listen(serverFd_, 16) < 0) {
        std::cerr << "[Control] listen() failed: " << std::strerror(errno) << std::endl;
        ::close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    running_ = true;
    serverThread_ = std::thread(&ControlServer::serveLoop, this);
    std::cout << "[Control] Listening on " << config_.control.host
              << ":" << config_.control.port << std::endl;
    return true;
}

void ControlServer::stop() {
    if (!running_) return;
    running_ = false;

    if (serverFd_ >= 0) {
        ::shutdown(serverFd_, SHUT_RDWR);
        ::close(serverFd_);
        serverFd_ = -1;
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    terminateAllSessions();
}

bool ControlServer::isRunning() const {
    return running_;
}

void ControlServer::serveLoop() {
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = ::accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!running_) break;
            continue;
        }
        handleClient(clientFd);
        ::close(clientFd);
    }
}

void ControlServer::handleClient(int clientFd) {
    std::string req;
    req.reserve(4096);
    std::array<char, 4096> buf{};
    ssize_t n = 0;

    while ((n = ::recv(clientFd, buf.data(), buf.size(), 0)) > 0) {
        req.append(buf.data(), static_cast<size_t>(n));
        if (req.find("\r\n\r\n") != std::string::npos) break;
        if (req.size() > 1024 * 1024) break;
    }
    if (req.empty()) return;

    const size_t headerEnd = req.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return;
    const std::string header = req.substr(0, headerEnd);
    std::string body = req.substr(headerEnd + 4);

    size_t firstLineEnd = header.find("\r\n");
    std::string firstLine = firstLineEnd == std::string::npos ? header : header.substr(0, firstLineEnd);
    std::istringstream fl(firstLine);
    std::string method;
    std::string pathWithQuery;
    std::string httpVersion;
    fl >> method >> pathWithQuery >> httpVersion;
    if (method.empty() || pathWithQuery.empty()) return;

    size_t clPos = header.find("Content-Length:");
    int contentLength = 0;
    if (clPos != std::string::npos) {
        size_t vPos = clPos + std::string("Content-Length:").size();
        size_t lineEnd = header.find("\r\n", vPos);
        std::string v = trim(header.substr(vPos, lineEnd == std::string::npos ? std::string::npos : lineEnd - vPos));
        contentLength = static_cast<int>(toInt64(v, 0));
    }
    while (static_cast<int>(body.size()) < contentLength) {
        n = ::recv(clientFd, buf.data(), buf.size(), 0);
        if (n <= 0) break;
        body.append(buf.data(), static_cast<size_t>(n));
    }

    int statusCode = 200;
    std::string json = handleRequest(method, pathWithQuery, body, statusCode);
    const std::string resp = makeHttpJsonResponse(statusCode, json);
    ::send(clientFd, resp.data(), resp.size(), 0);
}

std::string ControlServer::handleRequest(
    const std::string& method,
    const std::string& pathWithQuery,
    const std::string& body,
    int& statusCode
) {
    reapExitedSessions();

    if (method == "OPTIONS") {
        statusCode = 200;
        return "{\"ok\":true}";
    }

    std::string path = pathWithQuery;
    std::string query;
    size_t q = pathWithQuery.find('?');
    if (q != std::string::npos) {
        path = pathWithQuery.substr(0, q);
        query = pathWithQuery.substr(q + 1);
    }
    const auto queryMap = parseQuery(query);

    if (method == "GET" && path == "/api/record/overview") {
        const std::string streamKey = urlDecode(queryMap.count("stream_key") ? queryMap.at("stream_key") : "");
        if (streamKey.empty()) {
            statusCode = 400;
            return "{\"error\":\"stream_key required\"}";
        }
        return handleOverview(streamKey);
    }

    if (method == "GET" && path == "/api/record/timeline") {
        const std::string streamKey = urlDecode(queryMap.count("stream_key") ? queryMap.at("stream_key") : "");
        if (streamKey.empty()) {
            statusCode = 400;
            return "{\"error\":\"stream_key required\"}";
        }
        int64_t startMs = toInt64(queryMap.count("start") ? queryMap.at("start") : "", -1);
        int64_t endMs = toInt64(queryMap.count("end") ? queryMap.at("end") : "", -1);
        return handleTimeline(streamKey, startMs, endMs);
    }

    if (method == "POST" && path == "/api/record/replay/start") {
        const std::string streamKey = jsonExtractRaw(body, "stream_key");
        if (streamKey.empty()) {
            statusCode = 400;
            return "{\"error\":\"stream_key required\"}";
        }
        int64_t tsMs = toInt64(jsonExtractRaw(body, "ts"), nowMs());
        return handleReplayStart(streamKey, tsMs);
    }

    if (method == "POST" && path == "/api/record/replay/stop") {
        const std::string streamKey = jsonExtractRaw(body, "stream_key");
        const std::string sessionId = jsonExtractRaw(body, "session_id").empty()
            ? jsonExtractRaw(body, "sessionId")
            : jsonExtractRaw(body, "session_id");
        if (streamKey.empty() && sessionId.empty()) {
            statusCode = 400;
            return "{\"error\":\"stream_key or session_id required\"}";
        }
        return handleReplayStop(streamKey, sessionId);
    }

    statusCode = 404;
    return "{\"error\":\"not found\"}";
}

std::vector<ControlServer::Segment> ControlServer::loadSegments(const std::string& streamKey) const {
    std::vector<Segment> out;

    std::filesystem::path root(config_.record.outputDir.empty() ? "./recordings" : config_.record.outputDir);
    std::filesystem::path streamDir = root / sanitizeStreamKey(streamKey);
    std::error_code ec;
    if (!std::filesystem::exists(streamDir, ec)) {
        return out;
    }

    const std::regex pattern(R"(segment_(\d+)_([\d]+)\.mp4$)");
    for (const auto& entry : std::filesystem::directory_iterator(streamDir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        const std::string fn = entry.path().filename().string();
        std::smatch m;
        if (!std::regex_search(fn, m, pattern)) continue;
        Segment seg;
        seg.filePath = entry.path().string();
        seg.fileName = fn;
        seg.startMs = toInt64(m[1].str(), 0);
        seg.endMs = toInt64(m[2].str(), 0);
        if (seg.endMs <= seg.startMs) {
            seg.endMs = seg.startMs + 1000;
        }
        seg.durationMs = seg.endMs - seg.startMs;
        auto thumbPath = entry.path();
        thumbPath.replace_extension(".jpg");
        std::string thumb = thumbPath.string();
        if (std::filesystem::exists(thumb, ec)) {
            seg.thumbnailPath = thumb;
        }
        out.push_back(seg);
    }

    std::sort(out.begin(), out.end(), [](const Segment& a, const Segment& b) {
        return a.startMs < b.startMs;
    });
    return out;
}

std::string ControlServer::handleOverview(const std::string& streamKey) {
    const auto segments = loadSegments(streamKey);
    if (segments.empty()) {
        return "{\"hasHistory\":false,\"nowMs\":" + std::to_string(nowMs()) +
            ",\"totalDurationMs\":0,\"segmentCount\":0,\"timeRange\":null,\"ranges\":[]}";
    }

    int64_t total = 0;
    for (const auto& seg : segments) total += seg.durationMs;

    std::vector<std::pair<int64_t, int64_t>> ranges;
    for (const auto& seg : segments) {
        if (ranges.empty() || seg.startMs > ranges.back().second + 1000) {
            ranges.push_back({seg.startMs, seg.endMs});
        } else {
            ranges.back().second = std::max(ranges.back().second, seg.endMs);
        }
    }

    std::ostringstream oss;
    oss << "{"
        << "\"hasHistory\":true,"
        << "\"nowMs\":" << nowMs() << ","
        << "\"totalDurationMs\":" << total << ","
        << "\"segmentCount\":" << segments.size() << ","
        << "\"timeRange\":{\"startMs\":" << segments.front().startMs
        << ",\"endMs\":" << segments.back().endMs << "},"
        << "\"ranges\":[";
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i) oss << ",";
        oss << "{\"startMs\":" << ranges[i].first << ",\"endMs\":" << ranges[i].second << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string ControlServer::handleTimeline(const std::string& streamKey, int64_t startMs, int64_t endMs) {
    const auto all = loadSegments(streamKey);
    if (all.empty()) {
        return "{\"startMs\":null,\"endMs\":null,\"ranges\":[],\"thumbnails\":[],\"segments\":[],\"nowMs\":"
            + std::to_string(nowMs()) + "}";
    }

    if (startMs < 0) startMs = all.front().startMs;
    if (endMs < 0) endMs = all.back().endMs;
    if (endMs <= startMs) {
        startMs = all.front().startMs;
        endMs = all.back().endMs;
    }

    std::vector<Segment> segments;
    for (const auto& seg : all) {
        if (seg.endMs >= startMs && seg.startMs <= endMs) {
            segments.push_back(seg);
        }
    }

    std::vector<std::pair<int64_t, int64_t>> ranges;
    for (const auto& seg : segments) {
        if (ranges.empty() || seg.startMs > ranges.back().second + 1000) {
            ranges.push_back({seg.startMs, seg.endMs});
        } else {
            ranges.back().second = std::max(ranges.back().second, seg.endMs);
        }
    }

    std::ostringstream oss;
    oss << "{"
        << "\"startMs\":" << startMs << ","
        << "\"endMs\":" << endMs << ","
        << "\"ranges\":[";
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i) oss << ",";
        oss << "{\"startMs\":" << ranges[i].first << ",\"endMs\":" << ranges[i].second << "}";
    }
    oss << "],\"thumbnails\":[],\"segments\":[";
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i) oss << ",";
        const auto& seg = segments[i];
        oss << "{"
            << "\"id\":" << jsonString(seg.fileName) << ","
            << "\"startMs\":" << seg.startMs << ","
            << "\"endMs\":" << seg.endMs << ","
            << "\"durationMs\":" << seg.durationMs
            << "}";
    }
    oss << "],\"nowMs\":" << nowMs() << "}";
    return oss.str();
}

std::string ControlServer::handleReplayStart(const std::string& streamKey, int64_t tsMs) {
    const auto segments = loadSegments(streamKey);
    if (segments.empty()) {
        return "{\"mode\":\"live\",\"playbackUrl\":null,\"offsetSec\":0}";
    }

    Segment target = segments.front();
    bool found = false;
    for (const auto& seg : segments) {
        if (tsMs >= seg.startMs && tsMs <= seg.endMs) {
            target = seg;
            found = true;
            break;
        }
    }
    if (!found) {
        for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
            if (it->startMs <= tsMs) {
                target = *it;
                found = true;
                break;
            }
        }
    }

    const int offsetSec = static_cast<int>(std::max<int64_t>(0, (tsMs - target.startMs) / 1000));

    static std::atomic<uint64_t> seq{1};
    const uint64_t id = seq.fetch_add(1);
    const std::string sessionId = std::to_string(nowMs()) + "_" + std::to_string(id);
    const std::string streamName = sanitizeStreamKey(streamKey) + "__" + sessionId;
    const std::string rtmpUrl = config_.control.replayRtmpBase + "/" + streamName;
    const std::string playbackUrl = "/history/" + streamName + ".flv";

    std::ostringstream cmd;
    cmd << config_.control.ffmpegBin
        << " -hide_banner -loglevel error -re -ss " << offsetSec
        << " -i " << jsonString(target.filePath)
        << " -c copy -f flv " << jsonString(rtmpUrl)
        << " >/dev/null 2>&1";

    pid_t pid = ::fork();
    if (pid == 0) {
        ::execl("/bin/sh", "sh", "-lc", cmd.str().c_str(), static_cast<char*>(nullptr));
        std::_Exit(127);
    }

    if (pid < 0) {
        return "{\"mode\":\"live\",\"playbackUrl\":null,\"offsetSec\":0,\"error\":\"fork failed\"}";
    }

    ReplaySession session;
    session.sessionId = sessionId;
    session.streamKey = streamKey;
    session.streamName = streamName;
    session.sourceFile = target.filePath;
    session.playbackUrl = playbackUrl;
    session.requestedTs = tsMs;
    session.offsetSec = offsetSec;
    session.pid = static_cast<int>(pid);
    session.startedAtMs = nowMs();

    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        sessions_[sessionId] = session;
    }

    std::ostringstream oss;
    oss << "{"
        << "\"mode\":\"history\","
        << "\"requestedTs\":" << tsMs << ","
        << "\"playbackUrl\":" << jsonString(playbackUrl) << ","
        << "\"offsetSec\":" << offsetSec << ","
        << "\"sessionId\":" << jsonString(sessionId) << ","
        << "\"transport\":\"flv-live\","
        << "\"segment\":{"
            << "\"startMs\":" << target.startMs << ","
            << "\"endMs\":" << target.endMs << ","
            << "\"durationMs\":" << target.durationMs
        << "}"
        << "}";
    return oss.str();
}

std::string ControlServer::handleReplayStop(const std::string& streamKey, const std::string& sessionId) {
    if (!sessionId.empty()) {
        terminateSession(sessionId);
        return "{\"ok\":true,\"stopped\":true}";
    }

    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        for (const auto& kv : sessions_) {
            if (kv.second.streamKey == streamKey) ids.push_back(kv.first);
        }
    }
    for (const auto& id : ids) {
        terminateSession(id);
    }
    return "{\"ok\":true,\"stopped\":true,\"count\":" + std::to_string(ids.size()) + "}";
}

void ControlServer::reapExitedSessions() {
    int status = 0;
    while (true) {
        pid_t pid = ::waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break;

        std::lock_guard<std::mutex> lock(sessionMutex_);
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
            if (it->second.pid == static_cast<int>(pid)) {
                sessions_.erase(it);
                break;
            }
        }
    }
}

void ControlServer::terminateSession(const std::string& sessionId) {
    ReplaySession session;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        auto it = sessions_.find(sessionId);
        if (it != sessions_.end()) {
            session = it->second;
            sessions_.erase(it);
            found = true;
        }
    }
    if (!found) return;

    if (session.pid > 0) {
        ::kill(session.pid, SIGTERM);
        ::waitpid(session.pid, nullptr, WNOHANG);
    }
}

void ControlServer::terminateAllSessions() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        for (const auto& kv : sessions_) ids.push_back(kv.first);
    }
    for (const auto& id : ids) {
        terminateSession(id);
    }
}

std::string ControlServer::sanitizeStreamKey(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw) {
        const bool ok = (ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') ||
                        ch == '-' || ch == '_' || ch == '.';
        out.push_back(ok ? ch : '_');
    }
    if (out.empty()) out = "default";
    return out;
}

int64_t ControlServer::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string ControlServer::jsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

} // namespace reallive
