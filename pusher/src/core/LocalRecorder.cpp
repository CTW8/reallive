#include "core/LocalRecorder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
}

namespace reallive {

namespace {

int64_t nowWallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string ffErr(int code) {
    char buf[256];
    av_strerror(code, buf, sizeof(buf));
    return std::string(buf);
}

double thumbnailSeekSeconds(const std::string& mp4Path) {
    std::smatch m;
    const std::regex pattern(R"(segment_(\d+)_(\d+)\.mp4$)");
    if (std::regex_search(mp4Path, m, pattern)) {
        const int64_t startMs = std::atoll(m[1].str().c_str());
        const int64_t endMs = std::atoll(m[2].str().c_str());
        if (endMs > startMs) {
            const double durationSec = static_cast<double>(endMs - startMs) / 1000.0;
            const double seekSec = durationSec * 0.25;
            return std::max(0.8, std::min(3.0, seekSec));
        }
    }
    return 1.2;
}

} // namespace

LocalRecorder::LocalRecorder() {
    avformat_network_init();
}

LocalRecorder::~LocalRecorder() {
    close();
    avformat_network_deinit();
}

bool LocalRecorder::init(
    const RecordConfig& config,
    const std::string& streamKey,
    const uint8_t* videoExtraData,
    int videoExtraDataSize,
    int width,
    int height
) {
    close();

    config_ = config;
    if (!config_.enabled) {
        initialized_ = false;
        return true;
    }
    if (config_.outputDir.empty()) {
        config_.outputDir = "./recordings";
    }
    if (config_.segmentDurationSec <= 0) {
        config_.segmentDurationSec = 60;
    }
    if (config_.targetFreePercent < config_.minFreePercent) {
        config_.targetFreePercent = config_.minFreePercent + 5;
    }
    if (config_.targetFreePercent > 98) {
        config_.targetFreePercent = 98;
    }

    streamKey_ = sanitizeStreamKey(streamKey.empty() ? "default" : streamKey);
    width_ = width;
    height_ = height;

    if (videoExtraData && videoExtraDataSize > 0) {
        videoExtraData_.assign(videoExtraData, videoExtraData + videoExtraDataSize);
    } else {
        videoExtraData_.clear();
    }

    std::filesystem::path root(config_.outputDir);
    std::filesystem::path streamDir = root / streamKey_;
    std::error_code ec;
    std::filesystem::create_directories(streamDir, ec);
    if (ec) {
        std::cerr << "[LocalRecorder] Failed to create directory: " << streamDir
                  << ", error=" << ec.message() << std::endl;
        return false;
    }
    streamDir_ = streamDir.string();

    initialized_ = openSegment(nowWallMs());
    if (!initialized_) {
        std::cerr << "[LocalRecorder] Failed to open first segment" << std::endl;
        return false;
    }
    maybeCleanupOldSegments();
    std::cout << "[LocalRecorder] Enabled at " << streamDir_
              << ", segment=" << config_.segmentDurationSec << "s"
              << ", free-threshold=" << config_.minFreePercent << "%"
              << std::endl;
    return true;
}

bool LocalRecorder::isEnabled() const {
    return initialized_;
}

bool LocalRecorder::setCleanupPolicy(int minFreePercent, int targetFreePercent) {
    if (!initialized_) return false;
    minFreePercent = std::max(1, std::min(95, minFreePercent));
    targetFreePercent = std::max(minFreePercent + 1, std::min(98, targetFreePercent));
    {
        std::lock_guard<std::mutex> lock(policyMutex_);
        config_.minFreePercent = minFreePercent;
        config_.targetFreePercent = targetFreePercent;
    }
    maybeCleanupOldSegments();
    return true;
}

void LocalRecorder::getCleanupPolicy(int& minFreePercent, int& targetFreePercent) const {
    std::lock_guard<std::mutex> lock(policyMutex_);
    minFreePercent = config_.minFreePercent;
    targetFreePercent = config_.targetFreePercent;
}

bool LocalRecorder::openSegment(int64_t startMs) {
    std::error_code ec;
    std::filesystem::create_directories(streamDir_, ec);
    if (ec) {
        return false;
    }

    currentTempPath_ = makeTempPath(startMs);
    int ret = avformat_alloc_output_context2(&formatCtx_, nullptr, "mp4", currentTempPath_.c_str());
    if (ret < 0 || !formatCtx_) {
        std::cerr << "[LocalRecorder] alloc output context failed: " << ffErr(ret) << std::endl;
        formatCtx_ = nullptr;
        return false;
    }

    AVStream* vs = avformat_new_stream(formatCtx_, nullptr);
    if (!vs) {
        std::cerr << "[LocalRecorder] create video stream failed" << std::endl;
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }
    videoStreamIdx_ = vs->index;
    vs->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vs->codecpar->codec_id = AV_CODEC_ID_H264;
    vs->codecpar->width = width_;
    vs->codecpar->height = height_;
    vs->time_base = {1, 1000};

    if (!videoExtraData_.empty()) {
        vs->codecpar->extradata = static_cast<uint8_t*>(
            av_mallocz(videoExtraData_.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!vs->codecpar->extradata) {
            std::cerr << "[LocalRecorder] alloc extradata failed" << std::endl;
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }
        std::memcpy(vs->codecpar->extradata, videoExtraData_.data(), videoExtraData_.size());
        vs->codecpar->extradata_size = static_cast<int>(videoExtraData_.size());
    }

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&formatCtx_->pb, currentTempPath_.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "[LocalRecorder] open temp file failed: " << ffErr(ret) << std::endl;
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }
    }

    ret = avformat_write_header(formatCtx_, nullptr);
    if (ret < 0) {
        std::cerr << "[LocalRecorder] write header failed: " << ffErr(ret) << std::endl;
        if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&formatCtx_->pb);
        }
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }

    headerWritten_ = true;
    segmentStartWallMs_ = startMs;
    segmentStartPtsUs_ = -1;
    return true;
}

bool LocalRecorder::finalizeCurrentSegment(int64_t endMs) {
    if (!formatCtx_) {
        return true;
    }

    if (headerWritten_) {
        av_write_trailer(formatCtx_);
    }

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&formatCtx_->pb);
    }
    avformat_free_context(formatCtx_);
    formatCtx_ = nullptr;
    headerWritten_ = false;
    videoStreamIdx_ = -1;

    const std::string finalPath = makeFinalPath(segmentStartWallMs_, endMs);
    std::error_code ec;
    std::filesystem::rename(currentTempPath_, finalPath, ec);
    if (ec) {
        std::cerr << "[LocalRecorder] rename segment failed: " << ec.message()
                  << ", from=" << currentTempPath_
                  << ", to=" << finalPath << std::endl;
        return false;
    }

    if (config_.generateThumbnails) {
        generateThumbnail(finalPath);
    }

    maybeCleanupOldSegments();
    currentTempPath_.clear();
    return true;
}

bool LocalRecorder::rotateIfNeeded(const EncodedPacket& packet, int64_t nowMs) {
    if (!formatCtx_) return openSegment(nowMs);

    const int64_t durationMs = static_cast<int64_t>(config_.segmentDurationSec) * 1000;
    const int64_t elapsed = nowMs - segmentStartWallMs_;
    if (elapsed < durationMs) {
        return true;
    }

    const bool canRotate = packet.isKeyframe || elapsed >= durationMs + 5000;
    if (!canRotate) {
        return true;
    }

    if (!finalizeCurrentSegment(nowMs)) {
        return false;
    }
    return openSegment(nowMs);
}

bool LocalRecorder::writeVideoPacket(const EncodedPacket& packet) {
    if (!initialized_) return true;
    if (packet.empty()) return true;

    const int64_t nowMs = nowWallMs();
    if (!rotateIfNeeded(packet, nowMs)) {
        return false;
    }
    if (!formatCtx_) {
        return false;
    }

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt) return false;

    avpkt->data = const_cast<uint8_t*>(packet.data.data());
    avpkt->size = static_cast<int>(packet.data.size());
    avpkt->stream_index = videoStreamIdx_;

    int64_t ptsUs = packet.pts;
    int64_t dtsUs = packet.dts;
    if (segmentStartPtsUs_ < 0) {
        segmentStartPtsUs_ = std::max<int64_t>(0, ptsUs);
    }
    ptsUs -= segmentStartPtsUs_;
    dtsUs -= segmentStartPtsUs_;
    if (ptsUs < 0) ptsUs = 0;
    if (dtsUs < 0) dtsUs = 0;

    AVStream* stream = formatCtx_->streams[videoStreamIdx_];
    avpkt->pts = av_rescale_q(ptsUs, {1, 1000000}, stream->time_base);
    avpkt->dts = av_rescale_q(dtsUs, {1, 1000000}, stream->time_base);
    avpkt->duration = 0;
    if (packet.isKeyframe) {
        avpkt->flags |= AV_PKT_FLAG_KEY;
    }

    const int ret = av_interleaved_write_frame(formatCtx_, avpkt);
    av_packet_free(&avpkt);
    if (ret < 0) {
        std::cerr << "[LocalRecorder] write frame failed: " << ffErr(ret) << std::endl;
        return false;
    }
    return true;
}

void LocalRecorder::close() {
    if (formatCtx_) {
        const int64_t endMs = nowWallMs();
        finalizeCurrentSegment(endMs);
    }
    initialized_ = false;
    headerWritten_ = false;
    videoStreamIdx_ = -1;
    formatCtx_ = nullptr;
    currentTempPath_.clear();
}

std::string LocalRecorder::sanitizeStreamKey(const std::string& raw) const {
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

std::string LocalRecorder::makeTempPath(int64_t startMs) const {
    return streamDir_ + "/segment_" + std::to_string(startMs) + "_open.writing";
}

std::string LocalRecorder::makeFinalPath(int64_t startMs, int64_t endMs) const {
    if (endMs < startMs) endMs = startMs;
    return streamDir_ + "/segment_" + std::to_string(startMs) + "_" + std::to_string(endMs) + ".mp4";
}

std::vector<std::string> LocalRecorder::listSegmentsOldestFirst() const {
    std::vector<std::pair<int64_t, std::string>> items;
    std::regex pattern(R"(segment_(\d+)_([\d]+)\.mp4$)");

    std::error_code ec;
    if (!std::filesystem::exists(streamDir_, ec)) {
        return {};
    }

    for (const auto& entry : std::filesystem::directory_iterator(streamDir_, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        const std::string filename = entry.path().filename().string();
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".mp4") continue;

        int64_t sortKey = 0;
        std::smatch m;
        if (std::regex_search(filename, m, pattern)) {
            sortKey = std::atoll(m[1].str().c_str());
        } else {
            sortKey = static_cast<int64_t>(entry.last_write_time().time_since_epoch().count());
        }
        items.push_back({sortKey, entry.path().string()});
    }

    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    std::vector<std::string> out;
    out.reserve(items.size());
    for (const auto& item : items) {
        out.push_back(item.second);
    }
    return out;
}

void LocalRecorder::maybeCleanupOldSegments() {
    if (streamDir_.empty()) return;
    int minFreePercent = 15;
    int targetFreePercent = 20;
    std::string outputDir;
    {
        std::lock_guard<std::mutex> lock(policyMutex_);
        minFreePercent = config_.minFreePercent;
        targetFreePercent = config_.targetFreePercent;
        outputDir = config_.outputDir;
    }

    auto getFreePct = [&]() -> double {
        std::error_code ec;
        auto space = std::filesystem::space(outputDir, ec);
        if (ec || space.capacity == 0) return 100.0;
        return static_cast<double>(space.available) * 100.0 / static_cast<double>(space.capacity);
    };

    double freePct = getFreePct();
    if (freePct >= static_cast<double>(minFreePercent)) {
        return;
    }

    auto files = listSegmentsOldestFirst();
    size_t deleted = 0;
    for (size_t i = 0; i < files.size() && files.size() - deleted > 1; i++) {
        if (freePct >= static_cast<double>(targetFreePercent)) {
            break;
        }

        const std::string& mp4Path = files[i];
        std::error_code ec;
        std::filesystem::remove(mp4Path, ec);
        if (ec) {
            continue;
        }
        deleted++;

        std::string jpgPath = mp4Path;
        const size_t pos = jpgPath.rfind(".mp4");
        if (pos != std::string::npos) {
            jpgPath.replace(pos, 4, ".jpg");
            std::filesystem::remove(jpgPath, ec);
        }

        freePct = getFreePct();
        std::cout << "[LocalRecorder] Deleted old segment due to low storage, free="
                  << freePct << "%" << std::endl;
    }
}

void LocalRecorder::generateThumbnail(const std::string& mp4Path) const {
    std::string jpgPath = mp4Path;
    const size_t pos = jpgPath.rfind(".mp4");
    if (pos == std::string::npos) return;
    jpgPath.replace(pos, 4, ".jpg");

    const double seekSec = thumbnailSeekSeconds(mp4Path);
    std::string cmd = "ffmpeg -y -loglevel error -ss " + std::to_string(seekSec) + " -i \"" + mp4Path +
                      "\" -frames:v 1 -q:v 3 -vf \"scale=320:180:force_original_aspect_ratio=increase,crop=320:180\" \"" +
                      jpgPath + "\" >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc != 0 || !std::filesystem::exists(jpgPath)) {
        std::string fallback = "ffmpeg -y -loglevel error -ss 0.3 -i \"" + mp4Path +
                               "\" -frames:v 1 -q:v 3 -vf \"scale=320:180:force_original_aspect_ratio=increase,crop=320:180\" \"" +
                               jpgPath + "\" >/dev/null 2>&1";
        std::system(fallback.c_str());
    }
}

} // namespace reallive
