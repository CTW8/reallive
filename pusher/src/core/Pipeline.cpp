#include "core/Pipeline.h"
#include "core/stage/StagePipeline.h"
#include "core/TextOverlay.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <thread>
#include <mutex>
#include <condition_variable>

// Platform-specific includes (Raspberry Pi 5)
#ifdef REALLIVE_HAS_RPI5
#include "platform/rpi5/LibcameraCapture.h"
#include "platform/rpi5/AvcodecEncoder.h"
#include "platform/rpi5/RtmpStreamer.h"
#ifdef REALLIVE_HAS_ALSA
#include "platform/rpi5/AlsaCapture.h"
#endif
#endif
#include "core/LocalRecorder.h"

#ifdef REALLIVE_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

#ifdef REALLIVE_HAS_TFLITE
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model.h>
#endif

namespace reallive {

namespace {

constexpr std::array<uint8_t, 16> kTelemetrySeiUuid = {
    0x52, 0x65, 0x61, 0x4C, 0x69, 0x76, 0x65, 0x53,
    0x65, 0x69, 0x4D, 0x65, 0x74, 0x72, 0x69, 0x63
};

struct SystemTelemetry {
    double cpuPct = 0.0;
    std::vector<double> cpuCorePct;
    double memoryPct = 0.0;
    double memoryUsedMb = 0.0;
    double memoryTotalMb = 0.0;
    double storagePct = 0.0;
    double storageUsedGb = 0.0;
    double storageTotalGb = 0.0;
};

struct PersonBox {
    bool valid = false;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    double score = 0.0;
    int64_t ts = 0;
};

struct CpuCounters {
    uint64_t total = 0;
    uint64_t idle = 0;
    bool valid = false;
};

struct CpuStatSnapshot {
    CpuCounters total;
    std::vector<CpuCounters> cores;
};

double clampPercent(double value) {
    if (!std::isfinite(value)) return 0.0;
    if (value < 0.0) return 0.0;
    if (value > 100.0) return 100.0;
    return value;
}

std::string jsonEscape(const std::string& input) {
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

std::string formatNumber(double value, int precision = 1) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

struct StreamLevelConfig {
    int width;
    int height;
    int fps;
    int bitrateKbps;
};

int resolveOutputFps(int requestedCameraFps, int fallbackEncoderFps) {
    const int fromCamera = requestedCameraFps > 0 ? requestedCameraFps : 0;
    const int fromEncoder = fallbackEncoderFps > 0 ? fallbackEncoderFps : 0;
    const int base = std::max(fromCamera, fromEncoder);
    return std::max(24, base > 0 ? base : 25);
}

StreamLevelConfig levelConfig(int level, int outputFps) {
    const int safe = std::max(0, std::min(4, level));
    const int fps = std::max(24, outputFps);
    switch (safe) {
    case 0: return {640, 360, fps, 700};
    case 1: return {960, 540, fps, 1200};
    case 2: return {1280, 720, fps, 2000};
    case 3: return {1920, 1080, fps, 2800};
    default: return {1920, 1080, fps, 4000};
    }
}

Frame resizeNv12Nearest(const Frame& src, int dstW, int dstH) {
    Frame out;
    if (src.empty() || src.width <= 0 || src.height <= 0 || dstW <= 0 || dstH <= 0) {
        return out;
    }
    if (src.width == dstW && src.height == dstH) {
        return src;
    }

    const size_t srcYSize = static_cast<size_t>(src.width) * static_cast<size_t>(src.height);
    const size_t srcUvSize = srcYSize / 2;
    if (src.data.size() < srcYSize + srcUvSize) {
        return out;
    }

    out.width = dstW;
    out.height = dstH;
    out.stride = dstW;
    out.pts = src.pts;
    out.pixelFormat = src.pixelFormat.empty() ? "NV12" : src.pixelFormat;
    out.data.resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 3 / 2);

    const uint8_t* srcY = src.data.data();
    const uint8_t* srcUV = src.data.data() + srcYSize;
    uint8_t* dstY = out.data.data();
    uint8_t* dstUV = out.data.data() + static_cast<size_t>(dstW) * static_cast<size_t>(dstH);

    for (int y = 0; y < dstH; ++y) {
        const int sy = std::min(src.height - 1, (y * src.height) / dstH);
        for (int x = 0; x < dstW; ++x) {
            const int sx = std::min(src.width - 1, (x * src.width) / dstW);
            dstY[static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)] =
                srcY[static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx)];
        }
    }

    const int srcUvH = src.height / 2;
    const int dstUvH = dstH / 2;
    for (int y = 0; y < dstUvH; ++y) {
        const int sy = std::min(std::max(0, srcUvH - 1), (y * std::max(1, srcUvH)) / std::max(1, dstUvH));
        for (int x = 0; x < dstW; x += 2) {
            int sx = (x * src.width) / std::max(1, dstW);
            if (sx & 1) sx -= 1;
            sx = std::max(0, std::min(std::max(0, src.width - 2), sx));
            const size_t srcIdx = static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx);
            const size_t dstIdx = static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x);
            dstUV[dstIdx] = srcUV[srcIdx];
            if (x + 1 < dstW) {
                dstUV[dstIdx + 1] = srcUV[srcIdx + 1];
            }
        }
    }

    return out;
}

void flipNv12Horizontal(std::vector<uint8_t>& data, int width, int height) {
    if (width <= 1 || height <= 1) return;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvSize = ySize / 2;
    if (data.size() < ySize + uvSize) return;
    uint8_t* y = data.data();
    uint8_t* uv = data.data() + ySize;

    for (int row = 0; row < height; ++row) {
        uint8_t* line = y + static_cast<size_t>(row) * static_cast<size_t>(width);
        for (int l = 0, r = width - 1; l < r; ++l, --r) {
            std::swap(line[l], line[r]);
        }
    }

    for (int row = 0; row < height / 2; ++row) {
        uint8_t* line = uv + static_cast<size_t>(row) * static_cast<size_t>(width);
        for (int l = 0, r = width - 2; l < r; l += 2, r -= 2) {
            std::swap(line[l], line[r]);
            std::swap(line[l + 1], line[r + 1]);
        }
    }
}

void flipNv12Vertical(std::vector<uint8_t>& data, int width, int height) {
    if (width <= 1 || height <= 1) return;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvSize = ySize / 2;
    if (data.size() < ySize + uvSize) return;
    uint8_t* y = data.data();
    uint8_t* uv = data.data() + ySize;

    std::vector<uint8_t> tmpRow(static_cast<size_t>(width), 0);
    for (int t = 0, b = height - 1; t < b; ++t, --b) {
        uint8_t* top = y + static_cast<size_t>(t) * static_cast<size_t>(width);
        uint8_t* bottom = y + static_cast<size_t>(b) * static_cast<size_t>(width);
        std::memcpy(tmpRow.data(), top, static_cast<size_t>(width));
        std::memcpy(top, bottom, static_cast<size_t>(width));
        std::memcpy(bottom, tmpRow.data(), static_cast<size_t>(width));
    }

    for (int t = 0, b = (height / 2) - 1; t < b; ++t, --b) {
        uint8_t* top = uv + static_cast<size_t>(t) * static_cast<size_t>(width);
        uint8_t* bottom = uv + static_cast<size_t>(b) * static_cast<size_t>(width);
        std::memcpy(tmpRow.data(), top, static_cast<size_t>(width));
        std::memcpy(top, bottom, static_cast<size_t>(width));
        std::memcpy(bottom, tmpRow.data(), static_cast<size_t>(width));
    }
}

void applyNightVisionNv12(std::vector<uint8_t>& data, int width, int height) {
    if (width <= 1 || height <= 1) return;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t uvSize = ySize / 2;
    if (data.size() < ySize + uvSize) return;
    uint8_t* y = data.data();
    uint8_t* uv = data.data() + ySize;

    // Boost luminance and flatten shadows/noise.
    for (size_t i = 0; i < ySize; ++i) {
        int v = static_cast<int>(y[i]);
        v = 12 + (v * 11) / 10; // mild gain + lift
        if (v > 255) v = 255;
        y[i] = static_cast<uint8_t>(v);
    }

    // Slight green-ish neutralization in chroma for typical night-vision look.
    for (size_t i = 0; i + 1 < uvSize; i += 2) {
        uv[i] = 96;      // U
        uv[i + 1] = 140; // V
    }
}

double estimateNv12LumaMean(const std::vector<uint8_t>& data, int width, int height) {
    if (width <= 0 || height <= 0) return 255.0;
    const size_t ySize = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (data.size() < ySize) return 255.0;
    const uint8_t* y = data.data();

    const int stepX = std::max(1, width / 64);
    const int stepY = std::max(1, height / 36);
    uint64_t sum = 0;
    uint64_t cnt = 0;
    for (int yy = 0; yy < height; yy += stepY) {
        const size_t row = static_cast<size_t>(yy) * static_cast<size_t>(width);
        for (int xx = 0; xx < width; xx += stepX) {
            sum += y[row + static_cast<size_t>(xx)];
            cnt++;
        }
    }
    if (cnt == 0) return 255.0;
    return static_cast<double>(sum) / static_cast<double>(cnt);
}

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

constexpr int64_t kEpochMsMin = 946684800000LL;   // 2000-01-01
constexpr int64_t kEpochMsMax = 4102444800000LL;  // 2100-01-01

int64_t normalizeTimestampEpochMs(int64_t raw, int64_t fallback) {
    if (raw <= 0) return fallback;
    if (raw >= kEpochMsMin && raw <= kEpochMsMax) return raw;  // ms
    if (raw >= (kEpochMsMin / 1000LL) && raw <= (kEpochMsMax / 1000LL)) return raw * 1000LL;  // s
    if (raw >= (kEpochMsMin * 1000LL) && raw <= (kEpochMsMax * 1000LL)) return raw / 1000LL;  // us
    if (raw >= (kEpochMsMin * 1000000LL) && raw <= (kEpochMsMax * 1000000LL)) return raw / 1000000LL;  // ns
    return fallback;
}

int64_t normalizeFrameTimestampMs(int64_t framePts) {
    const int64_t fallback = wallClockMs();
    if (framePts <= 0) return fallback;

    // Most capture backends provide pts in us; keep compatibility first.
    const int64_t usToMs = framePts / 1000LL;
    const int64_t fromUsPath = normalizeTimestampEpochMs(usToMs, -1);
    if (fromUsPath > 0) return fromUsPath;

    const int64_t fromRaw = normalizeTimestampEpochMs(framePts, -1);
    if (fromRaw > 0) return fromRaw;
    return fallback;
}

double clamp01(double value) {
    if (!std::isfinite(value)) return 0.0;
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

class MotionPersonDetector {
public:
    MotionPersonDetector() = default;

    explicit MotionPersonDetector(const DetectionConfig& cfg)
        : cfg_(cfg) {
        normalizeConfig();
        loadLabels();
        initTflite();
#ifdef REALLIVE_HAS_OPENCV
        hasOpenCv_ = cfg_.useOpenCvMotion;
#else
        hasOpenCv_ = false;
#endif
        std::cout << "[PersonDetect] motion="
                  << ((hasOpenCv_ && cfg_.useOpenCvMotion) ? "opencv" : "fallback")
                  << " tflite=" << (tfliteReady_ ? "on" : "off")
                  << " infer_on_motion=" << (cfg_.inferOnMotionOnly ? "true" : "false")
                  << std::endl;
    }

    PersonBox detect(const Frame& frame, int64_t nowMs) {
        if (!cfg_.enabled || frame.empty() || frame.width <= 0 || frame.height <= 0) {
            return {};
        }
        const int minFrameSize = frame.width * frame.height;
        if (static_cast<int>(frame.data.size()) < minFrameSize) {
            return {};
        }

        frameCount_++;
        const bool onDetectFrame = (cfg_.intervalFrames <= 1) || ((frameCount_ % cfg_.intervalFrames) == 0);
        if (!onDetectFrame) {
            PersonBox tracked;
            if (updateTemplateTrack(frame, nowMs, tracked)) {
                lastDetectedMs_ = nowMs;
                lastBox_ = tracked;
                return tracked;
            }
            return heldBox(nowMs);
        }

        PersonBox motionCandidate;
        double motionRatio = 0.0;
        const bool hasMotion = detectMotion(frame, nowMs, motionCandidate, motionRatio);
        const bool useModelPath = (cfg_.useTfliteSsd && tfliteReady_);

        if (useModelPath) {
            const bool inferAllowed = (!cfg_.inferOnMotionOnly || hasMotion);
            if (inferAllowed && nowMs - lastInferMs_ >= cfg_.inferMinIntervalMs) {
                lastInferMs_ = nowMs;
                PersonBox inferBox;
                const PersonBox gateMotion = hasMotion ? motionCandidate : PersonBox{};
                if (runTfliteInference(frame, gateMotion, nowMs, inferBox)) {
                    lastDetectedMs_ = nowMs;
                    lastBox_ = inferBox;
                    refreshTrackTemplate(frame, inferBox, true);
                    return inferBox;
                }
            }
            const bool heavyMotion = hasMotion && motionRatio > 0.08;
            if (!heavyMotion) {
                PersonBox tracked;
                if (updateTemplateTrack(frame, nowMs, tracked)) {
                    lastDetectedMs_ = nowMs;
                    lastBox_ = tracked;
                    return tracked;
                }
            } else {
                // During aggressive camera movement, avoid reusing stale boxes.
                return {};
            }
            return heldBox(nowMs);
        }

        if (!hasMotion) {
            PersonBox tracked;
            if (updateTemplateTrack(frame, nowMs, tracked)) {
                lastDetectedMs_ = nowMs;
                lastBox_ = tracked;
                return tracked;
            }
            return heldBox(nowMs);
        }

        motionCandidate.score = clamp01(std::max(motionCandidate.score, motionRatio * 2.2));
        lastDetectedMs_ = nowMs;
        lastBox_ = motionCandidate;
        refreshTrackTemplate(frame, motionCandidate, true);
        return motionCandidate;
    }

private:
    void normalizeConfig() {
        if (cfg_.intervalFrames < 1) cfg_.intervalFrames = 1;
        if (cfg_.diffThreshold < 1) cfg_.diffThreshold = 1;
        if (cfg_.motionRatioThreshold <= 0.0 || cfg_.motionRatioThreshold >= 1.0) {
            cfg_.motionRatioThreshold = 0.015;
        }
        if (cfg_.minBoxAreaRatio <= 0.0 || cfg_.minBoxAreaRatio >= 1.0) {
            cfg_.minBoxAreaRatio = 0.006;
        }
        if (cfg_.holdMs < 0) cfg_.holdMs = 0;
        if (cfg_.tfliteInputSize < 128) cfg_.tfliteInputSize = 128;
        if (cfg_.inferMinIntervalMs < 10) cfg_.inferMinIntervalMs = 10;
        if (cfg_.personScoreThreshold <= 0.0 || cfg_.personScoreThreshold >= 1.0) {
            cfg_.personScoreThreshold = 0.55;
        }
    }

    static std::string trim(std::string s) {
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
        size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
        return s.substr(b, e - b);
    }

    static std::string lower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    void loadLabels() {
        labels_.clear();
        personClassId_ = 0;
        std::ifstream file(cfg_.tfliteLabelPath);
        if (!file.is_open()) return;
        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty()) continue;
            labels_.push_back(line);
        }
        for (size_t i = 0; i < labels_.size(); i++) {
            const std::string lbl = lower(labels_[i]);
            if (lbl.find("person") != std::string::npos || lbl == "people") {
                personClassId_ = static_cast<int>(i);
                break;
            }
        }
    }

    bool isPersonClass(int cls) const {
        if (cls < 0) return false;
        if (cls < static_cast<int>(labels_.size())) {
            const std::string lbl = lower(labels_[cls]);
            return lbl.find("person") != std::string::npos || lbl == "people";
        }
        return cls == personClassId_;
    }

    static double iou(const PersonBox& a, const PersonBox& b) {
        if (!a.valid || !b.valid) return 0.0;
        const int ax2 = a.x + a.w;
        const int ay2 = a.y + a.h;
        const int bx2 = b.x + b.w;
        const int by2 = b.y + b.h;
        const int ix1 = std::max(a.x, b.x);
        const int iy1 = std::max(a.y, b.y);
        const int ix2 = std::min(ax2, bx2);
        const int iy2 = std::min(ay2, by2);
        const int iw = std::max(0, ix2 - ix1);
        const int ih = std::max(0, iy2 - iy1);
        const double inter = static_cast<double>(iw) * static_cast<double>(ih);
        const double unionArea =
            static_cast<double>(a.w) * static_cast<double>(a.h) +
            static_cast<double>(b.w) * static_cast<double>(b.h) - inter;
        if (unionArea <= 0.0) return 0.0;
        return inter / unionArea;
    }

    struct LetterboxTransform {
        int srcW = 0;
        int srcH = 0;
        int dstW = 0;
        int dstH = 0;
        int resizedW = 0;
        int resizedH = 0;
        int padX = 0;
        int padY = 0;
        float scale = 1.0f;
    };

    static LetterboxTransform nv12ToRgbLetterbox(const Frame& frame, int dstW, int dstH, std::vector<uint8_t>& out) {
        LetterboxTransform tx;
        tx.srcW = frame.width;
        tx.srcH = frame.height;
        tx.dstW = dstW;
        tx.dstH = dstH;

        if (frame.width <= 0 || frame.height <= 0 || dstW <= 0 || dstH <= 0) {
            out.clear();
            return tx;
        }

        const float sx = static_cast<float>(dstW) / static_cast<float>(frame.width);
        const float sy = static_cast<float>(dstH) / static_cast<float>(frame.height);
        tx.scale = std::max(1e-6f, std::min(sx, sy));
        tx.resizedW = std::max(1, std::min(dstW, static_cast<int>(std::round(frame.width * tx.scale))));
        tx.resizedH = std::max(1, std::min(dstH, static_cast<int>(std::round(frame.height * tx.scale))));
        tx.padX = std::max(0, (dstW - tx.resizedW) / 2);
        tx.padY = std::max(0, (dstH - tx.resizedH) / 2);

        out.assign(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 3u, static_cast<uint8_t>(114));
        const uint8_t* yPlane = frame.data.data();
        const uint8_t* uvPlane = frame.data.data() + frame.width * frame.height;

        const float invScale = 1.0f / tx.scale;
        for (int ry = 0; ry < tx.resizedH; ry++) {
            const int syPx = std::max(0, std::min(frame.height - 1, static_cast<int>(std::floor(ry * invScale))));
            const int dy = tx.padY + ry;
            for (int rx = 0; rx < tx.resizedW; rx++) {
                const int sxPx = std::max(0, std::min(frame.width - 1, static_cast<int>(std::floor(rx * invScale))));
                const int dx = tx.padX + rx;
                const int y = static_cast<int>(yPlane[syPx * frame.width + sxPx]);
                const int uvIndex = (syPx / 2) * frame.width + (sxPx / 2) * 2;
                const int u = static_cast<int>(uvPlane[uvIndex]);
                const int v = static_cast<int>(uvPlane[uvIndex + 1]);

                const int c = std::max(0, y - 16);
                const int d = u - 128;
                const int e = v - 128;
                int r = (298 * c + 409 * e + 128) >> 8;
                int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int b = (298 * c + 516 * d + 128) >> 8;
                r = std::max(0, std::min(255, r));
                g = std::max(0, std::min(255, g));
                b = std::max(0, std::min(255, b));

                const size_t idx = (static_cast<size_t>(dy) * static_cast<size_t>(tx.dstW) +
                                    static_cast<size_t>(dx)) * 3u;
                out[idx + 0] = static_cast<uint8_t>(r);
                out[idx + 1] = static_cast<uint8_t>(g);
                out[idx + 2] = static_cast<uint8_t>(b);
            }
        }
        return tx;
    }

    bool updateTemplateTrack(const Frame& frame, int64_t nowMs, PersonBox& out) {
#ifdef REALLIVE_HAS_OPENCV
        if (!hasOpenCv_ || !trackReady_ || !lastBox_.valid) return false;
        if (lastTrackRunMs_ > 0 && nowMs - lastTrackRunMs_ < 66) return false;
        const cv::Mat y(frame.height, frame.width, CV_8UC1,
                        const_cast<uint8_t*>(frame.data.data()));
        if (y.empty() || trackTemplate_.empty()) return false;

        const int bw = std::max(8, trackBox_.w);
        const int bh = std::max(8, trackBox_.h);
        const int padX = std::max(12, bw / 3);
        const int padY = std::max(12, bh / 3);
        const int sx = std::max(0, trackBox_.x - padX);
        const int sy = std::max(0, trackBox_.y - padY);
        const int ex = std::min(frame.width, trackBox_.x + bw + padX);
        const int ey = std::min(frame.height, trackBox_.y + bh + padY);
        const cv::Rect searchRect(sx, sy, std::max(1, ex - sx), std::max(1, ey - sy));
        if (searchRect.width < trackTemplate_.cols || searchRect.height < trackTemplate_.rows) {
            return false;
        }

        const cv::Mat search = y(searchRect);
        cv::Mat result;
        cv::matchTemplate(search, trackTemplate_, result, cv::TM_CCOEFF_NORMED);

        double minVal = 0.0;
        double maxVal = 0.0;
        cv::Point minLoc;
        cv::Point maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
        (void)minVal;
        (void)minLoc;
        if (maxVal < 0.70) return false;

        const cv::Rect found(
            searchRect.x + maxLoc.x,
            searchRect.y + maxLoc.y,
            trackTemplate_.cols,
            trackTemplate_.rows
        );
        if (found.width <= 2 || found.height <= 2) return false;

        out.valid = true;
        out.x = std::max(0, std::min(frame.width - 1, found.x));
        out.y = std::max(0, std::min(frame.height - 1, found.y));
        const int x2 = std::max(0, std::min(frame.width, found.x + found.width));
        const int y2 = std::max(0, std::min(frame.height, found.y + found.height));
        out.w = std::max(2, x2 - out.x);
        out.h = std::max(2, y2 - out.y);
        out.ts = nowMs;
        out.score = clamp01(maxVal);

        trackBox_ = out;
        lastTrackRunMs_ = nowMs;
        if (maxVal > 0.82 || (nowMs - lastTemplateRefreshMs_) > 900) {
            refreshTrackTemplate(frame, out, false);
            lastTemplateRefreshMs_ = nowMs;
        }
        return true;
#else
        (void)frame;
        (void)nowMs;
        (void)out;
        return false;
#endif
    }

    void refreshTrackTemplate(const Frame& frame, const PersonBox& box, bool force) {
#ifdef REALLIVE_HAS_OPENCV
        if (!hasOpenCv_ || !box.valid || frame.width <= 0 || frame.height <= 0) return;
        const cv::Mat y(frame.height, frame.width, CV_8UC1,
                        const_cast<uint8_t*>(frame.data.data()));
        if (y.empty()) return;

        const int x = std::max(0, box.x);
        const int y0 = std::max(0, box.y);
        const int x2 = std::min(frame.width, box.x + box.w);
        const int y2 = std::min(frame.height, box.y + box.h);
        const int w = std::max(0, x2 - x);
        const int h = std::max(0, y2 - y0);
        if (w < 8 || h < 8) return;

        cv::Rect roi(x, y0, w, h);
        if (!force && !trackTemplate_.empty()) {
            const double oldArea = static_cast<double>(trackTemplate_.cols) * static_cast<double>(trackTemplate_.rows);
            const double newArea = static_cast<double>(w) * static_cast<double>(h);
            if (oldArea > 1.0) {
                const double ratio = newArea / oldArea;
                if (ratio < 0.4 || ratio > 2.5) {
                    return;
                }
            }
        }

        trackTemplate_ = y(roi).clone();
        trackBox_ = box;
        trackReady_ = !trackTemplate_.empty();
#else
        (void)frame;
        (void)box;
        (void)force;
#endif
    }

    bool detectMotionFallback(const Frame& frame, int64_t nowMs, PersonBox& box, double& ratio) {
        const int sampleW = std::max(64, std::min(240, frame.width / 6));
        const int sampleH = std::max(36, std::min(160, frame.height / 6));
        const size_t sampleSize = static_cast<size_t>(sampleW) * static_cast<size_t>(sampleH);
        if (sampleSize == 0) return false;

        if (prevLuma_.size() != sampleSize) {
            prevLuma_.assign(sampleSize, 0);
            hasPrev_ = false;
        }

        const uint8_t* yPlane = frame.data.data();
        int changed = 0;
        int minX = sampleW;
        int minY = sampleH;
        int maxX = -1;
        int maxY = -1;

        for (int sy = 0; sy < sampleH; sy++) {
            const int srcY = std::min(frame.height - 1, (sy * frame.height) / sampleH);
            for (int sx = 0; sx < sampleW; sx++) {
                const int srcX = std::min(frame.width - 1, (sx * frame.width) / sampleW);
                const uint8_t current = yPlane[srcY * frame.width + srcX];
                const size_t idx = static_cast<size_t>(sy) * static_cast<size_t>(sampleW) + static_cast<size_t>(sx);
                const uint8_t prev = prevLuma_[idx];
                prevLuma_[idx] = current;

                if (!hasPrev_) continue;
                const int diff = std::abs(static_cast<int>(current) - static_cast<int>(prev));
                if (diff < cfg_.diffThreshold) continue;
                changed++;
                minX = std::min(minX, sx);
                minY = std::min(minY, sy);
                maxX = std::max(maxX, sx);
                maxY = std::max(maxY, sy);
            }
        }
        hasPrev_ = true;
        if (changed <= 0 || maxX < minX || maxY < minY) return false;

        ratio = static_cast<double>(changed) / static_cast<double>(sampleW * sampleH);
        if (ratio < cfg_.motionRatioThreshold) return false;

        box.valid = true;
        box.x = (minX * frame.width) / sampleW;
        box.y = (minY * frame.height) / sampleH;
        box.w = std::max(2, ((maxX + 1) * frame.width) / sampleW - box.x);
        box.h = std::max(2, ((maxY + 1) * frame.height) / sampleH - box.y);
        const double areaRatio = static_cast<double>(box.w) * static_cast<double>(box.h) /
                                 static_cast<double>(frame.width * frame.height);
        if (areaRatio < cfg_.minBoxAreaRatio) return false;
        box.ts = nowMs;
        box.score = clamp01(ratio * 3.0);
        return true;
    }

    bool detectMotion(const Frame& frame, int64_t nowMs, PersonBox& box, double& ratio) {
#ifdef REALLIVE_HAS_OPENCV
        if (hasOpenCv_) {
            cv::Mat y(frame.height, frame.width, CV_8UC1,
                      const_cast<uint8_t*>(frame.data.data()));
            cv::Mat small;
            const int procW = std::max(160, std::min(384, frame.width / 4));
            const int procH = std::max(90, std::min(216, frame.height / 4));
            cv::resize(y, small, cv::Size(procW, procH), 0, 0, cv::INTER_AREA);
            cv::GaussianBlur(small, small, cv::Size(5, 5), 0.0);

            if (prevSmall_.empty()) {
                prevSmall_ = small.clone();
                return false;
            }

            cv::Mat diff;
            cv::absdiff(small, prevSmall_, diff);
            cv::threshold(diff, diff, cfg_.diffThreshold, 255, cv::THRESH_BINARY);
            cv::morphologyEx(diff, diff, cv::MORPH_OPEN,
                             cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
            cv::dilate(diff, diff, cv::Mat(), cv::Point(-1, -1), 2);
            prevSmall_ = small;

            const int changed = cv::countNonZero(diff);
            ratio = static_cast<double>(changed) / static_cast<double>(diff.rows * diff.cols);
            if (ratio < cfg_.motionRatioThreshold) return false;

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(diff, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            if (contours.empty()) return false;

            double bestArea = 0.0;
            cv::Rect bestRect;
            for (const auto& contour : contours) {
                cv::Rect r = cv::boundingRect(contour);
                const double area = static_cast<double>(r.width) * static_cast<double>(r.height);
                if (area > bestArea) {
                    bestArea = area;
                    bestRect = r;
                }
            }
            if (bestArea <= 0.0) return false;

            box.valid = true;
            box.x = (bestRect.x * frame.width) / procW;
            box.y = (bestRect.y * frame.height) / procH;
            box.w = std::max(2, (bestRect.width * frame.width) / procW);
            box.h = std::max(2, (bestRect.height * frame.height) / procH);
            const double areaRatio = static_cast<double>(box.w) * static_cast<double>(box.h) /
                                     static_cast<double>(frame.width * frame.height);
            if (areaRatio < cfg_.minBoxAreaRatio) return false;
            box.ts = nowMs;
            box.score = clamp01(ratio * 2.5);
            return true;
        }
#endif
        return detectMotionFallback(frame, nowMs, box, ratio);
    }

    void initTflite() {
#ifdef REALLIVE_HAS_TFLITE
        if (!cfg_.useTfliteSsd || cfg_.tfliteModelPath.empty()) return;
        tfliteModel_ = tflite::FlatBufferModel::BuildFromFile(cfg_.tfliteModelPath.c_str());
        if (!tfliteModel_) return;

        tflite::ops::builtin::BuiltinOpResolver resolver;
        tflite::InterpreterBuilder builder(*tfliteModel_, resolver);
        builder(&tfliteInterpreter_);
        if (!tfliteInterpreter_) return;
        tfliteInterpreter_->SetNumThreads(2);
        if (tfliteInterpreter_->AllocateTensors() != kTfLiteOk) {
            tfliteInterpreter_.reset();
            tfliteModel_.reset();
            return;
        }
        if (tfliteInterpreter_->inputs().empty()) {
            tfliteInterpreter_.reset();
            tfliteModel_.reset();
            return;
        }

        tfliteInputTensor_ = tfliteInterpreter_->inputs()[0];
        const TfLiteTensor* input = tfliteInterpreter_->tensor(tfliteInputTensor_);
        if (!input || !input->dims || input->dims->size < 4) {
            tfliteInterpreter_.reset();
            tfliteModel_.reset();
            return;
        }
        tfliteInputH_ = input->dims->data[1];
        tfliteInputW_ = input->dims->data[2];
        if (tfliteInputW_ <= 0 || tfliteInputH_ <= 0) {
            tfliteInterpreter_.reset();
            tfliteModel_.reset();
            return;
        }

        if (tfliteInputW_ != cfg_.tfliteInputSize || tfliteInputH_ != cfg_.tfliteInputSize) {
            cfg_.tfliteInputSize = std::max(tfliteInputW_, tfliteInputH_);
        }

        tfliteIsYoloV8_ = false;
        tfliteYoloChannelsFirst_ = true;
        tfliteYoloPredCount_ = 0;
        const auto& outs = tfliteInterpreter_->outputs();
        if (!outs.empty()) {
            const TfLiteTensor* out = tfliteInterpreter_->tensor(outs[0]);
            if (out && out->dims && out->dims->size == 3 &&
                out->type == kTfLiteFloat32) {
                const int d1 = out->dims->data[1];
                const int d2 = out->dims->data[2];
                if (d1 == 84 && d2 > 0) {
                    tfliteIsYoloV8_ = true;
                    tfliteYoloChannelsFirst_ = true;
                    tfliteYoloPredCount_ = d2;
                } else if (d2 == 84 && d1 > 0) {
                    tfliteIsYoloV8_ = true;
                    tfliteYoloChannelsFirst_ = false;
                    tfliteYoloPredCount_ = d1;
                }
            }
        }

        tfliteReady_ = true;
        std::cout << "[PersonDetect] tflite model loaded: " << cfg_.tfliteModelPath
                  << " input=" << tfliteInputW_ << "x" << tfliteInputH_
                  << " output_mode=" << (tfliteIsYoloV8_ ? "yolov8" : "unknown")
                  << std::endl;
#else
        tfliteReady_ = false;
#endif
    }

    bool runTfliteInference(const Frame& frame, const PersonBox& motion, int64_t nowMs, PersonBox& out) {
#ifndef REALLIVE_HAS_TFLITE
        (void)frame;
        (void)motion;
        (void)nowMs;
        (void)out;
        return false;
#else
        if (!tfliteReady_ || !tfliteInterpreter_) return false;

        std::vector<uint8_t> rgb;
        const LetterboxTransform lb = nv12ToRgbLetterbox(frame, tfliteInputW_, tfliteInputH_, rgb);
        if (rgb.empty()) return false;

        TfLiteTensor* input = tfliteInterpreter_->tensor(tfliteInputTensor_);
        if (!input) return false;

        if (input->type == kTfLiteFloat32) {
            float* ptr = tfliteInterpreter_->typed_input_tensor<float>(0);
            const size_t n = static_cast<size_t>(tfliteInputW_) * static_cast<size_t>(tfliteInputH_) * 3u;
            for (size_t i = 0; i < n; i++) {
                ptr[i] = static_cast<float>(rgb[i]) / 255.0f;
            }
        } else if (input->type == kTfLiteUInt8) {
            uint8_t* ptr = tfliteInterpreter_->typed_input_tensor<uint8_t>(0);
            std::memcpy(ptr, rgb.data(), rgb.size());
        } else if (input->type == kTfLiteInt8) {
            int8_t* ptr = tfliteInterpreter_->typed_input_tensor<int8_t>(0);
            const float scale = input->params.scale > 0 ? input->params.scale : 1.0f / 128.0f;
            const int zeroPoint = input->params.zero_point;
            for (size_t i = 0; i < rgb.size(); i++) {
                const float normalized = static_cast<float>(rgb[i]) / 255.0f;
                int q = static_cast<int>(std::round(normalized / scale)) + zeroPoint;
                q = std::max(-128, std::min(127, q));
                ptr[i] = static_cast<int8_t>(q);
            }
        } else {
            return false;
        }

        if (tfliteInterpreter_->Invoke() != kTfLiteOk) return false;

        const auto& outs = tfliteInterpreter_->outputs();
        if (outs.empty()) return false;

        if (tfliteIsYoloV8_) {
            const TfLiteTensor* pred = tfliteInterpreter_->tensor(outs[0]);
            if (!pred || pred->type != kTfLiteFloat32 || !pred->data.f) return false;
            if (!pred->dims || pred->dims->size != 3) return false;

            const int d1 = pred->dims->data[1];
            const int d2 = pred->dims->data[2];
            const bool channelsFirst = (d1 == 84);
            const int channels = channelsFirst ? d1 : d2;
            const int predCount = channelsFirst ? d2 : d1;
            if (channels != 84 || predCount <= 0) return false;

            const float* data = pred->data.f;
            const int personCls = std::max(0, std::min(79, personClassId_));

            auto valAt = [&](int c, int i) -> float {
                if (channelsFirst) {
                    return data[c * predCount + i];
                }
                return data[i * channels + c];
            };

            std::vector<PersonBox> candidates;
            candidates.reserve(64);
            for (int i = 0; i < predCount; i++) {
                float score = valAt(4 + personCls, i);
                if (score < 0.0f || score > 1.0f) {
                    score = 1.0f / (1.0f + std::exp(-score));
                }
                if (score < static_cast<float>(cfg_.personScoreThreshold)) continue;

                float cx = valAt(0, i);
                float cy = valAt(1, i);
                float bw = valAt(2, i);
                float bh = valAt(3, i);
                if (!(bw > 0.0f && bh > 0.0f)) continue;

                const float maxAbs = std::max(
                    std::max(std::fabs(cx), std::fabs(cy)),
                    std::max(std::fabs(bw), std::fabs(bh)));
                if (maxAbs <= 2.0f) {
                    cx *= static_cast<float>(tfliteInputW_);
                    cy *= static_cast<float>(tfliteInputH_);
                    bw *= static_cast<float>(tfliteInputW_);
                    bh *= static_cast<float>(tfliteInputH_);
                }

                const float x1i = cx - bw * 0.5f;
                const float y1i = cy - bh * 0.5f;
                const float x2i = cx + bw * 0.5f;
                const float y2i = cy + bh * 0.5f;

                if (x2i <= static_cast<float>(lb.padX) ||
                    y2i <= static_cast<float>(lb.padY) ||
                    x1i >= static_cast<float>(lb.padX + lb.resizedW) ||
                    y1i >= static_cast<float>(lb.padY + lb.resizedH)) {
                    continue;
                }

                const float x1f = (x1i - static_cast<float>(lb.padX)) / lb.scale;
                const float y1f = (y1i - static_cast<float>(lb.padY)) / lb.scale;
                const float x2f = (x2i - static_cast<float>(lb.padX)) / lb.scale;
                const float y2f = (y2i - static_cast<float>(lb.padY)) / lb.scale;

                if (x2f <= 0.0f || y2f <= 0.0f ||
                    x1f >= static_cast<float>(frame.width) ||
                    y1f >= static_cast<float>(frame.height)) {
                    continue;
                }

                const int x1 = std::max(0, std::min(frame.width - 1, static_cast<int>(std::floor(x1f))));
                const int y1 = std::max(0, std::min(frame.height - 1, static_cast<int>(std::floor(y1f))));
                const int x2 = std::max(0, std::min(frame.width, static_cast<int>(std::ceil(x2f))));
                const int y2 = std::max(0, std::min(frame.height, static_cast<int>(std::ceil(y2f))));
                const int w = std::max(0, x2 - x1);
                const int h = std::max(0, y2 - y1);
                if (w <= 1 || h <= 1) continue;

                const double areaRatio = static_cast<double>(w) * static_cast<double>(h) /
                                         static_cast<double>(frame.width * frame.height);
                if (areaRatio < cfg_.minBoxAreaRatio) continue;

                PersonBox box;
                box.valid = true;
                box.x = x1;
                box.y = y1;
                box.w = w;
                box.h = h;
                box.ts = nowMs;
                box.score = clamp01(static_cast<double>(score));

                if (cfg_.inferOnMotionOnly && motion.valid && iou(box, motion) < 0.02) {
                    continue;
                }

                candidates.push_back(box);
            }

            if (candidates.empty()) return false;

            std::sort(candidates.begin(), candidates.end(),
                      [](const PersonBox& a, const PersonBox& b) { return a.score > b.score; });

            std::vector<PersonBox> kept;
            kept.reserve(16);
            constexpr double kNmsIouThreshold = 0.45;
            for (const auto& cand : candidates) {
                bool suppressed = false;
                for (const auto& k : kept) {
                    if (iou(cand, k) > kNmsIouThreshold) {
                        suppressed = true;
                        break;
                    }
                }
                if (!suppressed) {
                    kept.push_back(cand);
                }
                if (kept.size() >= 16) break;
            }

            if (kept.empty()) return false;
            out = kept.front();
            return true;
        }

        // Backward-compatible SSD style parser (kept as fallback).
        if (outs.size() < 3) return false;
        const TfLiteTensor* boxes = tfliteInterpreter_->tensor(outs[0]);
        const TfLiteTensor* classes = tfliteInterpreter_->tensor(outs[1]);
        const TfLiteTensor* scores = tfliteInterpreter_->tensor(outs[2]);
        const TfLiteTensor* counts = outs.size() > 3 ? tfliteInterpreter_->tensor(outs[3]) : nullptr;
        if (!boxes || !classes || !scores || boxes->type != kTfLiteFloat32 ||
            classes->type != kTfLiteFloat32 || scores->type != kTfLiteFloat32 ||
            !boxes->data.f || !classes->data.f || !scores->data.f) {
            return false;
        }

        int count = 10;
        if (counts) {
            if (counts->type == kTfLiteFloat32 && counts->data.f) {
                count = static_cast<int>(std::round(counts->data.f[0]));
            } else if (counts->type == kTfLiteInt32 && counts->data.i32) {
                count = counts->data.i32[0];
            }
        } else if (boxes->dims && boxes->dims->size >= 3) {
            count = boxes->dims->data[1];
        }
        count = std::max(0, std::min(200, count));
        if (count <= 0) return false;

        PersonBox best;
        double bestScore = cfg_.personScoreThreshold;
        for (int i = 0; i < count; i++) {
            const double score = static_cast<double>(scores->data.f[i]);
            if (score < cfg_.personScoreThreshold) continue;

            const int cls = static_cast<int>(std::round(classes->data.f[i]));
            if (!isPersonClass(cls)) continue;

            const float yMinN = boxes->data.f[i * 4 + 0];
            const float xMinN = boxes->data.f[i * 4 + 1];
            const float yMaxN = boxes->data.f[i * 4 + 2];
            const float xMaxN = boxes->data.f[i * 4 + 3];

            PersonBox candidate;
            candidate.valid = true;
            candidate.x = std::max(0, std::min(frame.width - 1, static_cast<int>(std::floor(xMinN * frame.width))));
            candidate.y = std::max(0, std::min(frame.height - 1, static_cast<int>(std::floor(yMinN * frame.height))));
            const int x2 = std::max(0, std::min(frame.width, static_cast<int>(std::ceil(xMaxN * frame.width))));
            const int y2 = std::max(0, std::min(frame.height, static_cast<int>(std::ceil(yMaxN * frame.height))));
            candidate.w = std::max(0, x2 - candidate.x);
            candidate.h = std::max(0, y2 - candidate.y);
            candidate.ts = nowMs;
            candidate.score = clamp01(score);
            if (candidate.w <= 1 || candidate.h <= 1) continue;

            if (cfg_.inferOnMotionOnly && motion.valid && iou(candidate, motion) < 0.02) {
                continue;
            }
            if (score > bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
        if (!best.valid) return false;
        out = best;
        return true;
#endif
    }

    PersonBox heldBox(int64_t nowMs) {
        if (!lastBox_.valid) return {};
        if (cfg_.holdMs <= 0) return {};
        if (nowMs - lastDetectedMs_ > cfg_.holdMs) {
            lastBox_ = {};
#ifdef REALLIVE_HAS_OPENCV
            trackReady_ = false;
            trackTemplate_.release();
#endif
            lastTrackRunMs_ = 0;
            return {};
        }
        PersonBox held = lastBox_;
        // Keep original detection timestamp so overlay freshness can naturally
        // suppress stale boxes instead of extending them every detect tick.
        held.ts = lastDetectedMs_;
        return held;
    }

    DetectionConfig cfg_;
    uint64_t frameCount_ = 0;
    bool hasPrev_ = false;
    int64_t lastDetectedMs_ = 0;
    int64_t lastInferMs_ = std::numeric_limits<int64_t>::min() / 2;
    PersonBox lastBox_;
    std::vector<uint8_t> prevLuma_;
#ifdef REALLIVE_HAS_OPENCV
    cv::Mat prevSmall_;
    cv::Mat trackTemplate_;
#endif
    bool hasOpenCv_ = false;
    bool trackReady_ = false;
    int64_t lastTrackRunMs_ = 0;
    int64_t lastTemplateRefreshMs_ = 0;
    PersonBox trackBox_;
    std::vector<std::string> labels_;
    int personClassId_ = 0;
    bool tfliteReady_ = false;
    bool tfliteIsYoloV8_ = false;
    bool tfliteYoloChannelsFirst_ = true;
    int tfliteYoloPredCount_ = 0;
#ifdef REALLIVE_HAS_TFLITE
    std::unique_ptr<tflite::FlatBufferModel> tfliteModel_;
    std::unique_ptr<tflite::Interpreter> tfliteInterpreter_;
    int tfliteInputTensor_ = 0;
    int tfliteInputW_ = 320;
    int tfliteInputH_ = 320;
#endif
};

class DetectionEventJournal {
public:
    void init(const PusherConfig& config) {
        enabled_ = false;
        lastWriteMs_ = 0;
        if (!config.record.enabled) {
            return;
        }

        std::filesystem::path root(config.record.outputDir.empty() ? "./recordings" : config.record.outputDir);
        std::filesystem::path streamDir = root / sanitizeStreamKey(config.stream.streamKey.empty() ? "default" : config.stream.streamKey);
        std::error_code ec;
        std::filesystem::create_directories(streamDir, ec);
        if (ec) {
            return;
        }

        path_ = (streamDir / "events.ndjson").string();
        minIntervalMs_ = std::max(200, config.detection.eventMinIntervalMs);
        enabled_ = true;
    }

    void writePersonDetected(const PersonBox& box, int64_t tsMs) {
        if (!enabled_ || !box.valid) return;
        if (lastWriteMs_ > 0 && tsMs - lastWriteMs_ < minIntervalMs_) return;

        std::ofstream file(path_, std::ios::app);
        if (!file.is_open()) return;

        file << "{"
             << "\"ts\":" << tsMs << ","
             << "\"type\":\"person\","
             << "\"score\":" << formatNumber(box.score, 3) << ","
             << "\"bbox\":{"
                << "\"x\":" << box.x << ","
                << "\"y\":" << box.y << ","
                << "\"w\":" << box.w << ","
                << "\"h\":" << box.h
             << "}"
             << "}\n";
        lastWriteMs_ = tsMs;
    }

private:
    std::string sanitizeStreamKey(const std::string& raw) {
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

    bool enabled_ = false;
    int64_t lastWriteMs_ = 0;
    int minIntervalMs_ = 1000;
    std::string path_;
};

bool parseCpuCountersLine(const std::string& line, std::string& label, CpuCounters& counters) {
    std::istringstream iss(line);
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;
    uint64_t guest = 0;
    uint64_t guestNice = 0;
    iss >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guestNice;
    if (label.empty()) return false;
    if (!iss && !iss.eof()) return false;
    if (label.rfind("cpu", 0) != 0) return false;

    counters.idle = idle + iowait;
    counters.total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guestNice;
    counters.valid = true;
    return true;
}

CpuStatSnapshot readCpuStatSnapshot() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return {};

    CpuStatSnapshot snapshot;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line.rfind("cpu", 0) != 0) {
            if (snapshot.total.valid) break;
            continue;
        }

        std::string label;
        CpuCounters counters;
        if (!parseCpuCountersLine(line, label, counters)) continue;
        if (label == "cpu") {
            snapshot.total = counters;
            continue;
        }
        if (label.size() <= 3) continue;
        const std::string suffix = label.substr(3);
        if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(),
                                            [](unsigned char c) { return std::isdigit(c) != 0; })) {
            continue;
        }
        const size_t coreIndex = static_cast<size_t>(std::stoul(suffix));
        if (coreIndex >= snapshot.cores.size()) {
            snapshot.cores.resize(coreIndex + 1);
        }
        snapshot.cores[coreIndex] = counters;
    }
    return snapshot;
}

class SystemUsageSampler {
public:
    SystemTelemetry sample() {
        SystemTelemetry telemetry;

        CpuStatSnapshot current = readCpuStatSnapshot();
        if (current.total.valid && previousTotal_.valid && current.total.total > previousTotal_.total) {
            const double totalDelta = static_cast<double>(current.total.total - previousTotal_.total);
            const double idleDelta = static_cast<double>(current.total.idle - previousTotal_.idle);
            telemetry.cpuPct = clampPercent((1.0 - idleDelta / totalDelta) * 100.0);
        } else {
            telemetry.cpuPct = 0.0;
        }
        if (current.total.valid) {
            previousTotal_ = current.total;
        }

        telemetry.cpuCorePct.assign(current.cores.size(), 0.0);
        for (size_t i = 0; i < current.cores.size(); i++) {
            const CpuCounters& core = current.cores[i];
            if (!core.valid) continue;
            if (i >= previousCores_.size()) continue;
            const CpuCounters& prevCore = previousCores_[i];
            if (!prevCore.valid || core.total <= prevCore.total) continue;
            const double totalDelta = static_cast<double>(core.total - prevCore.total);
            const double idleDelta = static_cast<double>(core.idle - prevCore.idle);
            telemetry.cpuCorePct[i] = clampPercent((1.0 - idleDelta / totalDelta) * 100.0);
        }
        previousCores_ = current.cores;

        std::ifstream memFile("/proc/meminfo");
        if (memFile.is_open()) {
            std::string key;
            uint64_t valueKb = 0;
            std::string unit;
            uint64_t memTotalKb = 0;
            uint64_t memAvailableKb = 0;
            while (memFile >> key >> valueKb >> unit) {
                if (key == "MemTotal:") memTotalKb = valueKb;
                if (key == "MemAvailable:") memAvailableKb = valueKb;
                if (memTotalKb > 0 && memAvailableKb > 0) break;
            }

            if (memTotalKb > 0) {
                const uint64_t memUsedKb = memTotalKb > memAvailableKb ? (memTotalKb - memAvailableKb) : 0;
                telemetry.memoryTotalMb = static_cast<double>(memTotalKb) / 1024.0;
                telemetry.memoryUsedMb = static_cast<double>(memUsedKb) / 1024.0;
                telemetry.memoryPct = clampPercent(static_cast<double>(memUsedKb) * 100.0 / static_cast<double>(memTotalKb));
            }
        }

        try {
            const auto space = std::filesystem::space("/");
            if (space.capacity > 0) {
                const uint64_t used = space.capacity > space.available ? (space.capacity - space.available) : 0;
                telemetry.storageTotalGb = static_cast<double>(space.capacity) / (1024.0 * 1024.0 * 1024.0);
                telemetry.storageUsedGb = static_cast<double>(used) / (1024.0 * 1024.0 * 1024.0);
                telemetry.storagePct = clampPercent(static_cast<double>(used) * 100.0 / static_cast<double>(space.capacity));
            }
        } catch (...) {
        }

        return telemetry;
    }

private:
    CpuCounters previousTotal_;
    std::vector<CpuCounters> previousCores_;
};

void appendSeiField(std::vector<uint8_t>& rbsp, int value) {
    while (value >= 0xFF) {
        rbsp.push_back(0xFF);
        value -= 0xFF;
    }
    rbsp.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> escapeRbsp(const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> ebsp;
    ebsp.reserve(rbsp.size() + 16);
    int zeroCount = 0;
    for (uint8_t b : rbsp) {
        if (zeroCount >= 2 && b <= 0x03) {
            ebsp.push_back(0x03);
            zeroCount = 0;
        }
        ebsp.push_back(b);
        zeroCount = (b == 0x00) ? (zeroCount + 1) : 0;
    }
    return ebsp;
}

bool isAnnexBPacket(const std::vector<uint8_t>& data) {
    if (data.size() >= 4) {
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) return true;
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) return true;
    }
    const size_t limit = std::min<size_t>(data.size(), 32);
    for (size_t i = 0; i + 3 < limit; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            (data[i + 2] == 0x01 || (data[i + 2] == 0x00 && data[i + 3] == 0x01))) {
            return true;
        }
    }
    return false;
}

std::string buildTelemetryPayload(
    const PusherConfig& config,
    const SystemTelemetry& telemetry,
    double outFps,
    double outBitrateBps,
    int64_t nowMs,
    const std::string& ptzAction,
    int ptzSpeed,
    int ptzZoomStep,
    int ptzZoomLevel,
    const std::string& ptzPreset,
    int64_t ptzUpdatedAtMs,
    const PersonBox& personState,
    const std::vector<PersonBox>& personEvents
) {
    constexpr double kTwoPi = 6.283185307179586;
    const double phase = std::fmod(static_cast<double>(nowMs) / 1000.0, 120.0) / 120.0 * kTwoPi;
    const int fakePanDeg = static_cast<int>(std::llround(std::sin(phase) * 90.0));
    const int fakeTiltDeg = static_cast<int>(std::llround(std::cos(phase * 1.7) * 30.0));
    const int64_t ptzTs = ptzUpdatedAtMs > 0 ? ptzUpdatedAtMs : nowMs;

    std::ostringstream oss;
    oss << "{"
        << "\"v\":1,"
        << "\"ts\":" << nowMs << ","
        << "\"stream_key\":\"" << jsonEscape(config.stream.streamKey) << "\","
        << "\"device\":{"
            << "\"cpu_pct\":" << formatNumber(telemetry.cpuPct) << ","
            << "\"cpu_core_pct\":[";
    for (size_t i = 0; i < telemetry.cpuCorePct.size(); i++) {
        if (i) oss << ",";
        oss << formatNumber(telemetry.cpuCorePct[i]);
    }
    oss << "],"
            << "\"mem_pct\":" << formatNumber(telemetry.memoryPct) << ","
            << "\"mem_used_mb\":" << formatNumber(telemetry.memoryUsedMb) << ","
            << "\"mem_total_mb\":" << formatNumber(telemetry.memoryTotalMb) << ","
            << "\"storage_pct\":" << formatNumber(telemetry.storagePct) << ","
            << "\"storage_used_gb\":" << formatNumber(telemetry.storageUsedGb, 2) << ","
            << "\"storage_total_gb\":" << formatNumber(telemetry.storageTotalGb, 2) << ","
            << "\"stream_out_fps\":" << formatNumber(outFps, 2) << ","
            << "\"stream_out_bitrate_bps\":" << static_cast<long long>(std::llround(std::max(0.0, outBitrateBps))) << ","
            << "\"stream_out_bitrate_kbps\":" << formatNumber(outBitrateBps / 1000.0, 1)
        << "},"
        << "\"camera\":{"
            << "\"width\":" << config.camera.width << ","
            << "\"height\":" << config.camera.height << ","
            << "\"fps\":" << config.camera.fps << ","
            << "\"pixel_format\":\"" << jsonEscape(config.camera.pixelFormat) << "\","
            << "\"codec\":\"" << jsonEscape(config.encoder.codec) << "\","
            << "\"bitrate\":" << config.encoder.bitrate << ","
            << "\"profile\":\"" << jsonEscape(config.encoder.profile) << "\","
            << "\"gop\":" << config.encoder.gopSize << ","
            << "\"audio_enabled\":" << (config.enableAudio ? "true" : "false") << ","
            << "\"detect_tflite_enabled\":" << (config.detection.useTfliteSsd ? "true" : "false") << ","
            << "\"detect_infer_on_motion_only\":" << (config.detection.inferOnMotionOnly ? "true" : "false") << ","
            << "\"detect_person_score_threshold\":" << formatNumber(config.detection.personScoreThreshold, 2)
        << "},"
        << "\"configurable\":{"
            << "\"resolution\":["
                << "{\"width\":640,\"height\":480},"
                << "{\"width\":1280,\"height\":720},"
                << "{\"width\":1920,\"height\":1080}"
            << "],"
            << "\"fps\":[10,15,24,25,30,50,60],"
            << "\"profile\":[\"baseline\",\"main\",\"high\"],"
            << "\"bitrate\":{\"min\":300000,\"max\":8000000,\"step\":100000},"
            << "\"gop\":{\"min\":10,\"max\":120,\"step\":5},"
            << "\"person_score_threshold\":{\"min\":0.3,\"max\":0.95,\"step\":0.01},"
            << "\"detect_infer_interval_ms\":{\"min\":10,\"max\":1000,\"step\":10}"
        << "},"
        << "\"ptz\":{"
            << "\"simulated\":true,"
            << "\"status\":\"online\","
            << "\"action\":\"" << jsonEscape(ptzAction) << "\","
            << "\"speed\":" << ptzSpeed << ","
            << "\"zoom_step\":" << ptzZoomStep << ","
            << "\"zoom_level\":" << ptzZoomLevel << ","
            << "\"preset\":\"" << jsonEscape(ptzPreset) << "\","
            << "\"updated_at\":" << ptzTs << ","
            << "\"pan_deg\":" << fakePanDeg << ","
            << "\"tilt_deg\":" << fakeTiltDeg << ","
            << "\"roll_deg\":0"
        << "},"
        << "\"person\":{"
            << "\"active\":" << (personState.valid ? "true" : "false") << ","
            << "\"score\":" << formatNumber(personState.score, 3) << ","
            << "\"ts\":" << personState.ts << ","
            << "\"bbox\":{"
                << "\"x\":" << personState.x << ","
                << "\"y\":" << personState.y << ","
                << "\"w\":" << personState.w << ","
                << "\"h\":" << personState.h
            << "}"
        << "},"
        << "\"events\":[";
    for (size_t i = 0; i < personEvents.size(); i++) {
        if (i) oss << ",";
        const auto& evt = personEvents[i];
        oss << "{"
            << "\"type\":\"person_detected\","
            << "\"ts\":" << evt.ts << ","
            << "\"score\":" << formatNumber(evt.score, 3) << ","
            << "\"bbox\":{"
                << "\"x\":" << evt.x << ","
                << "\"y\":" << evt.y << ","
                << "\"w\":" << evt.w << ","
                << "\"h\":" << evt.h
            << "}"
            << "}";
    }
    oss << "]"
        << "}";
    return oss.str();
}

void injectTelemetrySei(std::vector<uint8_t>& packet, const std::string& payload) {
    if (packet.empty() || payload.empty()) return;

    const int payloadSize = static_cast<int>(kTelemetrySeiUuid.size() + payload.size());
    std::vector<uint8_t> rbsp;
    rbsp.reserve(payload.size() + 32);
    rbsp.push_back(0x06);
    appendSeiField(rbsp, 5);
    appendSeiField(rbsp, payloadSize);
    rbsp.insert(rbsp.end(), kTelemetrySeiUuid.begin(), kTelemetrySeiUuid.end());
    rbsp.insert(rbsp.end(), payload.begin(), payload.end());
    rbsp.push_back(0x80);

    const std::vector<uint8_t> ebsp = escapeRbsp(rbsp);
    std::vector<uint8_t> prefix;
    if (isAnnexBPacket(packet)) {
        prefix.reserve(4 + ebsp.size());
        prefix.push_back(0x00);
        prefix.push_back(0x00);
        prefix.push_back(0x00);
        prefix.push_back(0x01);
        prefix.insert(prefix.end(), ebsp.begin(), ebsp.end());
    } else {
        const uint32_t naluSize = static_cast<uint32_t>(ebsp.size());
        prefix.reserve(4 + ebsp.size());
        prefix.push_back(static_cast<uint8_t>((naluSize >> 24) & 0xFF));
        prefix.push_back(static_cast<uint8_t>((naluSize >> 16) & 0xFF));
        prefix.push_back(static_cast<uint8_t>((naluSize >> 8) & 0xFF));
        prefix.push_back(static_cast<uint8_t>(naluSize & 0xFF));
        prefix.insert(prefix.end(), ebsp.begin(), ebsp.end());
    }

    packet.insert(packet.begin(), prefix.begin(), prefix.end());
}

} // namespace

Pipeline::Pipeline() = default;

Pipeline::~Pipeline() {
    stop();
}

bool Pipeline::createComponents(const PusherConfig& config) {
#ifdef REALLIVE_HAS_RPI5
    // Create platform-specific implementations for Raspberry Pi 5
    camera_ = std::make_unique<LibcameraCapture>();
    encoder_ = std::make_unique<AvcodecEncoder>();
    streamer_ = std::make_unique<RtmpStreamer>();

    if (config.enableAudio) {
#ifdef REALLIVE_HAS_ALSA
        audio_ = std::make_unique<AlsaCapture>();
#else
        std::cerr << "[Pipeline] Audio requested, but ALSA support is not built" << std::endl;
#endif
    }

    return true;
#else
    (void)config;
    std::cerr << "[Pipeline] No platform backend compiled for this target" << std::endl;
    return false;
#endif
}

bool Pipeline::init(const PusherConfig& config) {
    config_ = config;
    config_.camera.fps = std::max(24, config_.camera.fps);
    config_.encoder.fps = std::max(24, config_.encoder.fps);
    runtimeMotionEnabled_ = config_.detection.enabled;
    runtimePersonEnabled_ = config_.detection.enabled;
    runtimeWatermarkEnabled_ = true;
    runtimeImageFlipMode_ = 0;
    runtimeNightVisionEnabled_ = false;
    runtimeNightVisionMode_ = 0;
    const int fixedOutputFps = resolveOutputFps(config_.camera.fps, config_.encoder.fps);
    const auto bootLevelCfg = levelConfig(runtimeProfileLevel_.load(), fixedOutputFps);
    runtimeProfileTargetWidth_ = bootLevelCfg.width;
    runtimeProfileTargetHeight_ = bootLevelCfg.height;
    runtimeProfileTargetFps_ = bootLevelCfg.fps;
    runtimeProfileTargetBitrateKbps_ = bootLevelCfg.bitrateKbps;

    const char* stageModeEnv = std::getenv("REALLIVE_STAGE_PIPELINE");
    const bool wantStagePipeline = stageModeEnv && std::string(stageModeEnv) == "1";
    if (wantStagePipeline) {
#ifdef REALLIVE_HAS_RPI5
        stagePipeline_ = std::make_unique<stage::StagePipeline>();
        useStagePipeline_ = stagePipeline_->init(
            config_,
            std::make_unique<LibcameraCapture>(),
            std::make_unique<AvcodecEncoder>(),
            std::make_unique<RtmpStreamer>(),
#ifdef REALLIVE_HAS_ALSA
            config_.enableAudio ? std::make_unique<AlsaCapture>() : nullptr
#else
            nullptr
#endif
        );
        if (useStagePipeline_) {
            livePushDesired_ = true;
            livePushActive_ = false;
            std::cout << "[Pipeline] StagePipeline mode enabled (experimental)" << std::endl;
            return true;
        }
        std::cerr << "[Pipeline] StagePipeline init failed, fallback to legacy pipeline" << std::endl;
        stagePipeline_.reset();
#else
        std::cout << "[Pipeline] StagePipeline mode unavailable: no RPi5 backend compiled" << std::endl;
#endif
    }

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
#ifdef REALLIVE_HAS_RPI5
    auto* avEncoder = dynamic_cast<AvcodecEncoder*>(encoder_.get());
    if (avEncoder) {
        config_.stream.videoExtraData = avEncoder->getExtraData();
        config_.stream.videoExtraDataSize = avEncoder->getExtraDataSize();
        config_.stream.videoWidth = config.encoder.width;
        config_.stream.videoHeight = config.encoder.height;
    }
#endif

    // Connect to streaming server
    if (!streamer_->connect(config_.stream)) {
        std::cerr << "[Pipeline] Failed to connect to server: "
                  << config.stream.url << std::endl;
        return false;
    }
    livePushDesired_ = true;
    livePushActive_ = true;
    std::cout << "[Pipeline] Connected to: " << config.stream.url << std::endl;

    if (config_.record.enabled) {
        recorder_ = std::make_unique<LocalRecorder>();
        if (!recorder_->init(
                config_.record,
                config_.stream.streamKey,
                config_.stream.videoExtraData,
                config_.stream.videoExtraDataSize,
                config_.encoder.width,
                config_.encoder.height)) {
            std::cerr << "[Pipeline] Failed to init local recorder" << std::endl;
            recorder_.reset();
        }
    } else {
        recorder_.reset();
    }

    return true;
}

bool Pipeline::start() {
    if (useStagePipeline_ && stagePipeline_) {
        if (running_) {
            std::cerr << "[Pipeline] Already running" << std::endl;
            return false;
        }
        if (!stagePipeline_->start()) {
            std::cerr << "[Pipeline] Failed to start StagePipeline" << std::endl;
            return false;
        }
        running_ = true;
        livePushActive_ = true;
        std::cout << "[Pipeline] Started StagePipeline" << std::endl;
        return true;
    }

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
    if (useStagePipeline_ && stagePipeline_) {
        if (!running_) return;
        running_ = false;
        stagePipeline_->stop();
        livePushActive_ = false;
        std::cout << "[Pipeline] StagePipeline stopped" << std::endl;
        return;
    }

    if (!running_) return;

    running_ = false;

    if (videoThread_.joinable()) {
        videoThread_.join();
    }
    if (audioThread_.joinable()) {
        audioThread_.join();
    }

    // Stop components in reverse order
    if (streamer_) {
        std::lock_guard<std::mutex> lock(streamerMutex_);
        if (streamer_->isConnected()) {
            streamer_->disconnect();
        }
        livePushActive_ = false;
    }
    if (recorder_) {
        recorder_->close();
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
    auto lastSeiTime = Clock::now() - std::chrono::milliseconds(2000);
    auto lastSeiLogTime = Clock::now() - std::chrono::seconds(10);
    auto lastSlowLogTime = Clock::now() - std::chrono::seconds(10);
    auto lastAdaptLogTime = Clock::now() - std::chrono::seconds(10);
    uint64_t droppedFrames = 0;
    uint64_t totalProcessTime = 0;
    uint64_t maxProcessTime = 0;
    SystemUsageSampler usageSampler;

    std::mutex captureMutex;
    std::condition_variable captureCv;
    std::deque<Frame> captureQueue;
    constexpr size_t kCaptureQueueMax = 2;
    std::atomic<uint64_t> captureDropped{0};
    std::atomic<uint64_t> sendDropped{0};

    std::mutex sendMutex;
    std::condition_variable sendCv;
    std::deque<EncodedPacket> sendQueue;
    constexpr size_t kSendQueueMax = 4;
    bool sendStop = false;

    PersonBox latestPerson;
    std::vector<PersonBox> pendingPersonEvents;

    std::mutex detectMutex;
    std::condition_variable detectCv;
    bool detectStop = false;
    bool detectFrameReady = false;
    Frame detectFrame;
    int64_t detectFrameTsMs = 0;
    std::thread detectThread;
    if (config_.detection.enabled) {
        detectThread = std::thread([&]() {
            MotionPersonDetector personDetector(config_.detection);
            DetectionEventJournal detectionJournal;
            detectionJournal.init(config_);
            bool personPresent = false;
            int64_t lastPersonGoneMs = 0;
            const int64_t personRearmMs = std::max<int64_t>(200, config_.detection.eventMinIntervalMs);

            while (true) {
                Frame localFrame;
                int64_t localTs = 0;
                {
                    std::unique_lock<std::mutex> lock(detectMutex);
                    detectCv.wait(lock, [&]() { return detectStop || detectFrameReady; });
                    if (detectStop && !detectFrameReady) {
                        break;
                    }
                    localFrame = std::move(detectFrame);
                    localTs = detectFrameTsMs;
                    detectFrameReady = false;
                }

                if (localFrame.empty()) continue;

                const PersonBox person = personDetector.detect(localFrame, localTs);

                bool shouldWriteEvent = false;
                {
                    std::lock_guard<std::mutex> lock(detectMutex);
                    latestPerson = person;
                    if (person.valid) {
                        const bool rearmed = (lastPersonGoneMs <= 0) ||
                                             (localTs - lastPersonGoneMs >= personRearmMs);
                        if (!personPresent && rearmed) {
                            pendingPersonEvents.push_back(person);
                            if (pendingPersonEvents.size() > 8) {
                                pendingPersonEvents.erase(pendingPersonEvents.begin());
                            }
                            shouldWriteEvent = true;
                        }
                        personPresent = true;
                    } else {
                        if (personPresent) {
                            lastPersonGoneMs = localTs;
                        }
                        personPresent = false;
                    }
                }
                if (shouldWriteEvent) {
                    detectionJournal.writePersonDetected(person, localTs);
                }
            }
        });
    }

    std::thread captureThread([&]() {
        while (running_) {
            Frame frame = camera_->captureFrame();
            if (frame.empty()) {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(captureMutex);
                if (captureQueue.size() >= kCaptureQueueMax) {
                    captureQueue.pop_front();
                    captureDropped++;
                }
                captureQueue.push_back(std::move(frame));
            }
            captureCv.notify_one();
        }
        captureCv.notify_all();
    });

    std::thread sendThread([&]() {
        while (true) {
            EncodedPacket packet;
            {
                std::unique_lock<std::mutex> lock(sendMutex);
                sendCv.wait(lock, [&]() { return sendStop || !sendQueue.empty(); });
                if (sendStop && sendQueue.empty()) {
                    break;
                }
                packet = std::move(sendQueue.front());
                sendQueue.pop_front();
            }
            if (packet.empty()) continue;

            if (!livePushDesired_.load()) {
                continue;
            }

            bool sentOk = false;
            {
                std::lock_guard<std::mutex> streamLock(streamerMutex_);
                if (!livePushDesired_.load()) {
                    continue;
                }
                if (!streamer_->isConnected()) {
                    if (!streamer_->connect(config_.stream)) {
                        livePushActive_ = false;
                        std::cerr << "[Pipeline] Failed to reconnect streamer, dropping packet" << std::endl;
                        continue;
                    }
                    livePushActive_ = true;
                    std::cout << "[Pipeline] RTMP push resumed" << std::endl;
                }

                if (!streamer_->sendVideoPacket(packet)) {
                    std::cerr << "[Pipeline] Failed to send video packet" << std::endl;
                    if (!streamer_->isConnected()) {
                        livePushActive_ = false;
                    }
                } else {
                    sentOk = true;
                }
            }

            if (!sentOk) continue;
            framesSent_++;
            bytesSent_ += packet.data.size();
        }
    });

    const auto seiInterval = std::chrono::milliseconds(1000);

    const auto maxProcessThreshold = std::chrono::microseconds(1000000 / config_.camera.fps * 2); // 允许最大2倍帧间隔
    
    // 统计窗口
    const int statsWindow = config_.camera.fps * 5; // 5秒统计窗口
    std::vector<uint64_t> processTimes;
    processTimes.reserve(statsWindow);
    uint64_t lastCaptureWait = 0;
    uint64_t lastFramesSentForFps = 0;
    uint64_t lastBytesSentForBitrate = 0;
    double currentOutBitrateBps = 0.0;
    double ewmaOutBitrateBps = 0.0;
    std::deque<double> bitrate10sWindow;
    constexpr size_t kBitrateWindowSize = 10;
    constexpr double kBitrateEwmaAlpha = 0.25;
    constexpr int64_t kOverlayFreshMs = 160;
    int64_t nextEncodeDueMs = 0;
    uint64_t lastSendDropForAdapt = 0;
    uint64_t lastCaptureDropForAdapt = 0;
    uint64_t detectDispatchCounter = 0;
    bool autoNightVisionActive = false;
    auto lastAutoNightEval = Clock::now() - std::chrono::seconds(1);
    int activeEncWidth = std::max(2, config_.encoder.width);
    int activeEncHeight = std::max(2, config_.encoder.height);
    int activeEncFps = std::max(1, config_.encoder.fps);
    int activeEncBitrateKbps = std::max(100, config_.encoder.bitrate / 1000);

    auto reconfigureEncodePath = [&](int targetWidth,
                                     int targetHeight,
                                     int targetFps,
                                     int targetBitrateKbps,
                                     const char* reason) -> bool {
        targetWidth = std::max(2, targetWidth & ~1);
        targetHeight = std::max(2, targetHeight & ~1);
        targetFps = std::max(1, targetFps);
        targetBitrateKbps = std::max(100, targetBitrateKbps);
        if (targetWidth == activeEncWidth &&
            targetHeight == activeEncHeight &&
            targetFps == activeEncFps &&
            targetBitrateKbps == activeEncBitrateKbps) {
            return true;
        }

        std::cout << "[Stream Reconfig] reason=" << reason
                  << " from=" << activeEncWidth << "x" << activeEncHeight
                  << "@" << activeEncFps << "fps/" << activeEncBitrateKbps << "kbps"
                  << " to=" << targetWidth << "x" << targetHeight
                  << "@" << targetFps << "fps/" << targetBitrateKbps << "kbps"
                  << std::endl;

#ifdef REALLIVE_HAS_RPI5
        auto nextEncoder = std::make_unique<AvcodecEncoder>();
        EncoderConfig nextEncoderCfg = config_.encoder;
        nextEncoderCfg.width = targetWidth;
        nextEncoderCfg.height = targetHeight;
        nextEncoderCfg.fps = targetFps;
        nextEncoderCfg.bitrate = targetBitrateKbps * 1000;
        if (!nextEncoder->init(nextEncoderCfg)) {
            std::cerr << "[Stream Reconfig] encoder init failed, keep previous config" << std::endl;
            return false;
        }

        StreamConfig nextStreamCfg = config_.stream;
        nextStreamCfg.videoExtraData = nextEncoder->getExtraData();
        nextStreamCfg.videoExtraDataSize = nextEncoder->getExtraDataSize();
        nextStreamCfg.videoWidth = targetWidth;
        nextStreamCfg.videoHeight = targetHeight;

        bool streamReady = true;
        {
            std::lock_guard<std::mutex> streamLock(streamerMutex_);
            if (streamer_->isConnected()) {
                streamer_->disconnect();
            }
            if (livePushDesired_.load()) {
                if (!streamer_->connect(nextStreamCfg)) {
                    std::cerr << "[Stream Reconfig] RTMP reconnect failed after profile switch" << std::endl;
                    livePushActive_ = false;
                    streamReady = false;
                } else {
                    livePushActive_ = true;
                }
            } else {
                livePushActive_ = false;
            }
        }

        if (!streamReady && livePushDesired_.load()) {
            return false;
        }

        encoder_ = std::move(nextEncoder);
        config_.encoder = nextEncoderCfg;
        config_.stream = nextStreamCfg;

        {
            std::lock_guard<std::mutex> queueLock(sendMutex);
            sendQueue.clear();
        }

        if (recorder_ && recorder_->isEnabled()) {
            recorder_->close();
            if (!recorder_->init(
                    config_.record,
                    config_.stream.streamKey,
                    config_.stream.videoExtraData,
                    config_.stream.videoExtraDataSize,
                    targetWidth,
                    targetHeight)) {
                std::cerr << "[Stream Reconfig] recorder re-init failed, disable local record" << std::endl;
                recorder_.reset();
            }
        }

        activeEncWidth = targetWidth;
        activeEncHeight = targetHeight;
        activeEncFps = targetFps;
        activeEncBitrateKbps = targetBitrateKbps;
        return true;
#else
        (void)reason;
        return false;
#endif
    };

    while (running_) {
        auto frameStart = Clock::now();

        Frame frame;
        {
            std::unique_lock<std::mutex> lock(captureMutex);
            auto waitStart = Clock::now();
            captureCv.wait_for(lock, std::chrono::milliseconds(100), [&]() {
                return !running_ || !captureQueue.empty();
            });
            auto waitEnd = Clock::now();
            lastCaptureWait = std::chrono::duration_cast<std::chrono::microseconds>(waitEnd - waitStart).count();
            if (!running_ && captureQueue.empty()) {
                break;
            }
            if (captureQueue.empty()) {
                continue;
            }
            frame = std::move(captureQueue.back());
            captureQueue.clear();
        }
        if (frame.empty()) {
            continue;
        }

        const int targetWidth = std::max(2, runtimeProfileTargetWidth_.load());
        const int targetHeight = std::max(2, runtimeProfileTargetHeight_.load());
        const int targetFps = std::max(1, runtimeProfileTargetFps_.load());
        const int targetBitrateKbps = std::max(100, runtimeProfileTargetBitrateKbps_.load());
        reconfigureEncodePath(
            targetWidth,
            targetHeight,
            targetFps,
            targetBitrateKbps,
            "runtime-policy");

        auto captureTime = Clock::now();
        const int64_t frameTsMs = normalizeFrameTimestampMs(frame.pts);

        const int64_t minIntervalMs = std::max<int64_t>(1, 1000 / targetFps);
        if (nextEncodeDueMs <= 0) {
            nextEncodeDueMs = frameTsMs;
        }
        if (frameTsMs < nextEncodeDueMs) {
            continue;
        }
        nextEncodeDueMs += minIntervalMs;
        // If upstream stalls/jumps, re-anchor to current frame to avoid burst catch-up.
        if (frameTsMs - nextEncodeDueMs > minIntervalMs * 3) {
            nextEncodeDueMs = frameTsMs + minIntervalMs;
        }

        auto detectOverlayStart = Clock::now();

        const bool detectEnabled = runtimeMotionEnabled_.load() || runtimePersonEnabled_.load();
        if (config_.detection.enabled && detectEnabled) {
            const int dispatchEvery = std::max(1, config_.detection.intervalFrames);
            detectDispatchCounter += 1;
            if (detectDispatchCounter % static_cast<uint64_t>(dispatchEvery) == 0) {
                Frame frameForDetect = frame;
                {
                    std::lock_guard<std::mutex> lock(detectMutex);
                    detectFrame = std::move(frameForDetect);
                    detectFrameTsMs = frameTsMs;
                    detectFrameReady = true;
                }
                detectCv.notify_one();
            }

            PersonBox person;
            {
                std::lock_guard<std::mutex> lock(detectMutex);
                person = latestPerson;
            }
            const int64_t overlayAgeMs = std::llabs(frameTsMs - person.ts);
            if (person.valid && config_.detection.drawOverlay && overlayAgeMs <= kOverlayFreshMs) {
                TextOverlay::drawBoundingBox(
                    frame.data.data(),
                    frame.width,
                    frame.height,
                    person.x,
                    person.y,
                    person.w,
                    person.h
                );
            }
        }
        auto detectOverlayEnd = Clock::now();

        // 2. Draw timestamp overlay on frame before encoding
        const int flipMode = runtimeImageFlipMode_.load();
        if (flipMode == 1 || flipMode == 3) {
            flipNv12Horizontal(frame.data, frame.width, frame.height);
        }
        if (flipMode == 2 || flipMode == 3) {
            flipNv12Vertical(frame.data, frame.width, frame.height);
        }

        const bool nightEnabled = runtimeNightVisionEnabled_.load();
        const int nightMode = runtimeNightVisionMode_.load();
        bool applyNightVision = false;
        if (nightEnabled) {
            if (nightMode == 1) {
                applyNightVision = true;
            } else if (nightMode == 0) {
                // Auto mode: enable only in low light with hysteresis to avoid flicker/color cast.
                auto nowAuto = Clock::now();
                if (nowAuto - lastAutoNightEval >= std::chrono::milliseconds(250)) {
                    const double luma = estimateNv12LumaMean(frame.data, frame.width, frame.height);
                    const double onThreshold = 70.0;
                    const double offThreshold = 86.0;
                    if (!autoNightVisionActive && luma <= onThreshold) {
                        autoNightVisionActive = true;
                    } else if (autoNightVisionActive && luma >= offThreshold) {
                        autoNightVisionActive = false;
                    }
                    lastAutoNightEval = nowAuto;
                }
                applyNightVision = autoNightVisionActive;
            }
        }
        if (applyNightVision) {
            applyNightVisionNv12(frame.data, frame.width, frame.height);
        }

        // Keep timestamp overlay always visible on pushed stream.
        // Runtime watermark switch should not hide core time evidence.
        TextOverlay::drawTimestamp(frame.data.data(), frame.width, frame.height);

        // 3. Encode the frame
        Frame encodeFrame = frame;
        if (frame.width != activeEncWidth || frame.height != activeEncHeight) {
            encodeFrame = resizeNv12Nearest(frame, activeEncWidth, activeEncHeight);
            if (encodeFrame.empty()) {
                std::cerr << "[Pipeline] NV12 resize failed "
                          << frame.width << "x" << frame.height
                          << " -> " << activeEncWidth << "x" << activeEncHeight
                          << std::endl;
                continue;
            }
        }
        auto encodeStart = Clock::now();
        EncodedPacket packet = encoder_->encode(encodeFrame);
        auto encodeEnd = Clock::now();
        
        if (packet.empty()) {
            continue;
        }

        // Record timing information for latency tracking
        packet.captureTime = std::chrono::duration_cast<std::chrono::microseconds>(captureTime.time_since_epoch()).count();
        packet.encodeTime = std::chrono::duration_cast<std::chrono::microseconds>(encodeEnd - encodeStart).count();

        auto now = Clock::now();
        if (now - lastSeiTime >= seiInterval) {
            const SystemTelemetry telemetry = usageSampler.sample();
            std::string ptzAction;
            std::string ptzPreset;
            int ptzSpeed = 5;
            int ptzZoomStep = 1;
            int ptzZoomLevel = 50;
            int64_t ptzUpdatedAtMs = 0;
            getRuntimePtzState(
                ptzAction,
                ptzSpeed,
                ptzZoomStep,
                ptzZoomLevel,
                ptzPreset,
                ptzUpdatedAtMs
            );
            PersonBox personSnapshot;
            std::vector<PersonBox> eventSnapshot;
            if (config_.detection.enabled && (runtimeMotionEnabled_.load() || runtimePersonEnabled_.load())) {
                std::lock_guard<std::mutex> lock(detectMutex);
                personSnapshot = latestPerson;
                eventSnapshot = pendingPersonEvents;
                pendingPersonEvents.clear();
            }
            const std::string payload = buildTelemetryPayload(
                config_,
                telemetry,
                currentFps_.load(),
                currentOutBitrateBps,
                wallClockMs(),
                ptzAction,
                ptzSpeed,
                ptzZoomStep,
                ptzZoomLevel,
                ptzPreset,
                ptzUpdatedAtMs,
                personSnapshot,
                eventSnapshot
            );
            injectTelemetrySei(packet.data, payload);
            lastSeiTime = now;
            if (now - lastSeiLogTime >= std::chrono::seconds(5)) {
                std::cout << "[SEI Inject] out_fps=" << std::fixed << std::setprecision(2) << currentFps_.load()
                          << " out_kbps=" << std::fixed << std::setprecision(1) << (currentOutBitrateBps / 1000.0)
                          << std::endl;
                lastSeiLogTime = now;
            }
        }

        if (recorder_ && recorder_->isEnabled()) {
            if (!recorder_->writeVideoPacket(packet)) {
                static uint64_t recorderErrCount = 0;
                recorderErrCount++;
                if (recorderErrCount % 30 == 1) {
                    std::cerr << "[Pipeline] Recorder write failed (" << recorderErrCount
                              << "), continuing stream" << std::endl;
                }
            }
        }

        auto enqueueStart = Clock::now();
        {
            std::lock_guard<std::mutex> lock(sendMutex);
            if (sendQueue.size() >= kSendQueueMax) {
                size_t dropIdx = 0;
                bool foundNonKey = false;
                for (size_t i = 0; i < sendQueue.size(); i++) {
                    if (!sendQueue[i].isKeyframe) {
                        dropIdx = i;
                        foundNonKey = true;
                        break;
                    }
                }
                if (!foundNonKey) {
                    dropIdx = 0;
                }
                auto dropIt = sendQueue.begin();
                std::advance(dropIt, static_cast<long>(dropIdx));
                sendQueue.erase(dropIt);
                sendDropped++;
            }
            sendQueue.push_back(std::move(packet));
        }
        sendCv.notify_one();
        auto enqueueEnd = Clock::now();

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
        const auto detectOverlayUs = std::chrono::duration_cast<std::chrono::microseconds>(detectOverlayEnd - detectOverlayStart).count();
        const auto enqueueUs = std::chrono::duration_cast<std::chrono::microseconds>(enqueueEnd - enqueueStart).count();
        if (processTime > maxProcessThreshold) {
            droppedFrames++;
            if (now - lastSlowLogTime >= std::chrono::seconds(2)) {
                std::cerr << "[Pipeline Slow] total=" << (processTime.count() / 1000.0) << "ms"
                          << " capture_wait=" << (lastCaptureWait / 1000.0) << "ms"
                          << " detect_overlay=" << (detectOverlayUs / 1000.0) << "ms"
                          << " encode=" << (encodeTime.count() / 1000.0) << "ms"
                          << " enqueue=" << (enqueueUs / 1000.0) << "ms"
                          << " capture_drop=" << captureDropped.load()
                          << " send_drop=" << sendDropped.load()
                          << std::endl;
                lastSlowLogTime = now;
            }
        }

        // 每秒计算FPS和打印详细统计
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime);
        if (elapsed.count() >= 1000) {
            const uint64_t sentNow = framesSent_.load();
            const uint64_t sentDelta = sentNow >= lastFramesSentForFps ? (sentNow - lastFramesSentForFps) : 0;
            currentFps_ = static_cast<double>(sentDelta) * 1000.0 / elapsed.count();
            lastFramesSentForFps = sentNow;
            const uint64_t bytesNow = bytesSent_.load();
            const uint64_t bytesDelta = bytesNow >= lastBytesSentForBitrate ? (bytesNow - lastBytesSentForBitrate) : 0;
            currentOutBitrateBps = static_cast<double>(bytesDelta) * 8.0 * 1000.0 / elapsed.count();
            if (ewmaOutBitrateBps <= 0.0) {
                ewmaOutBitrateBps = currentOutBitrateBps;
            } else {
                ewmaOutBitrateBps = kBitrateEwmaAlpha * currentOutBitrateBps +
                                    (1.0 - kBitrateEwmaAlpha) * ewmaOutBitrateBps;
            }
            bitrate10sWindow.push_back(currentOutBitrateBps);
            if (bitrate10sWindow.size() > kBitrateWindowSize) {
                bitrate10sWindow.pop_front();
            }
            lastBytesSentForBitrate = bytesNow;
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
                      << " | CaptureDrop: " << captureDropped.load()
                      << " | SendDrop: " << sendDropped.load()
                      << " | CaptureWait: " << lastCaptureWait / 1000 << "ms"
                      << " | Encode: " << encodeTime.count() / 1000 << "ms"
                      << " | AvgProcess: " << avgProcessTime / 1000 << "ms"
                      << " | MaxProcess: " << maxProcessTime / 1000 << "ms"
                      << " | P99: " << p99Time / 1000 << "ms"
                      << " | Level: L" << runtimeProfileLevel_.load()
                      << "@" << runtimeProfileTargetFps_.load() << "fps/" << runtimeProfileTargetBitrateKbps_.load() << "kbps"
                      << " " << activeEncWidth << "x" << activeEncHeight
                      << std::endl;

            const int64_t nowMs = wallClockMs();
            const uint64_t currentSendDrop = sendDropped.load();
            const uint64_t currentCaptureDrop = captureDropped.load();
            const uint64_t sendDropDelta = currentSendDrop >= lastSendDropForAdapt
                ? (currentSendDrop - lastSendDropForAdapt)
                : 0;
            const uint64_t captureDropDelta = currentCaptureDrop >= lastCaptureDropForAdapt
                ? (currentCaptureDrop - lastCaptureDropForAdapt)
                : 0;
            lastSendDropForAdapt = currentSendDrop;
            lastCaptureDropForAdapt = currentCaptureDrop;

            std::string mode;
            std::string policy;
            int minLevel = 0;
            int maxLevel = 4;
            int cooldownSec = 10;
            int upHoldSec = 25;
            int downHoldSec = 3;
            {
                std::lock_guard<std::mutex> lock(runtimeStreamMutex_);
                mode = runtimeStreamMode_;
                policy = runtimeAutoPolicy_;
                minLevel = runtimeAutoMinLevel_;
                maxLevel = runtimeAutoMaxLevel_;
                cooldownSec = runtimeAutoCooldownSec_;
                upHoldSec = runtimeAutoUpHoldSec_;
                downHoldSec = runtimeAutoDownHoldSec_;
            }

            if (mode == "auto") {
                const int currentLevel = runtimeProfileLevel_.load();
                const int64_t sinceSwitch = nowMs - runtimeLastSwitchMs_.load();
                const bool pushActive = livePushDesired_.load() && livePushActive_.load();
                const double targetFpsEval = std::max(1, runtimeProfileTargetFps_.load());
                double avg10sBps = 0.0;
                if (!bitrate10sWindow.empty()) {
                    double sum = 0.0;
                    for (const double sample : bitrate10sWindow) sum += sample;
                    avg10sBps = sum / static_cast<double>(bitrate10sWindow.size());
                }
                const double fastBps = currentOutBitrateBps;
                const double stableBps = avg10sBps > 0.0 ? avg10sBps : ewmaOutBitrateBps;
                const bool fpsBad = currentFps_.load() < targetFpsEval * 0.90;
                const bool fpsGood = currentFps_.load() >= targetFpsEval * 0.98;
                const bool severeFpsBad = currentFps_.load() < targetFpsEval * 0.80;
                const bool overloadBad = captureDropDelta > 4;
                const bool bad = pushActive && (
                    (sendDropDelta > 8) ||
                    overloadBad ||
                    (sinceSwitch > 8000 && fpsBad)
                );
                const bool good = pushActive &&
                                  (sendDropDelta == 0) &&
                                  (sinceSwitch > 12000) &&
                                  fpsGood;

                if (bad) {
                    if (runtimeAdaptBadSinceMs_.load() <= 0) runtimeAdaptBadSinceMs_ = nowMs;
                    runtimeAdaptGoodSinceMs_ = 0;
                } else if (good) {
                    if (runtimeAdaptGoodSinceMs_.load() <= 0) runtimeAdaptGoodSinceMs_ = nowMs;
                    runtimeAdaptBadSinceMs_ = 0;
                } else {
                    runtimeAdaptBadSinceMs_ = 0;
                    runtimeAdaptGoodSinceMs_ = 0;
                }

                const int policyDownBias = (policy == "quality") ? 1 : 0;
                const int policyUpBias = (policy == "stable") ? 8 : (policy == "quality" ? -6 : 0);
                const int downNeedSec = std::max(1, downHoldSec + policyDownBias);
                const int upNeedSec = std::max(5, upHoldSec + policyUpBias);
                const bool fastDownAllowed = severeFpsBad || overloadBad;
                const bool canSwitch = fastDownAllowed || (sinceSwitch >= static_cast<int64_t>(cooldownSec) * 1000);
                const int effectiveDownNeedSec = fastDownAllowed ? 1 : downNeedSec;
                const int nextUpLevel = currentLevel + 1;
                const bool highLevelCandidate = nextUpLevel >= 3;
                const int effectiveUpNeedSec = highLevelCandidate ? std::max(upNeedSec, 45) : upNeedSec;
                const double frameBudgetUs = 1000000.0 / targetFpsEval;
                const bool fpsHeadroomOk = currentFps_.load() >= targetFpsEval * 0.99;
                const bool cpuHeadroomOk = static_cast<double>(avgProcessTime) <= frameBudgetUs * 0.80 &&
                                           static_cast<double>(p99Time) <= frameBudgetUs * 1.10;
                const bool noDropHeadroom = (sendDropDelta == 0) && (captureDropDelta == 0);
                const bool highLevelGateOk = !highLevelCandidate || (fpsHeadroomOk && cpuHeadroomOk && noDropHeadroom);
                const int64_t badMs = runtimeAdaptBadSinceMs_.load() > 0 ? (nowMs - runtimeAdaptBadSinceMs_.load()) : 0;
                const int64_t goodMs = runtimeAdaptGoodSinceMs_.load() > 0 ? (nowMs - runtimeAdaptGoodSinceMs_.load()) : 0;

                if (now - lastAdaptLogTime >= std::chrono::seconds(10)) {
                    std::cout << "[Adapt Eval] mode=auto level=L" << currentLevel
                              << " policy=" << policy
                              << " range=L" << minLevel << "-L" << maxLevel
                              << " canSwitch=" << (canSwitch ? "1" : "0")
                              << " cooldownSec=" << cooldownSec
                              << " sinceSwitchMs=" << sinceSwitch
                              << " bad=" << (bad ? "1" : "0")
                              << " badMs=" << badMs
                              << " badNeedMs=" << (downNeedSec * 1000)
                              << " good=" << (good ? "1" : "0")
                              << " goodMs=" << goodMs
                              << " goodNeedMs=" << (effectiveUpNeedSec * 1000)
                              << " sendDropDelta=" << sendDropDelta
                              << " captureDropDelta=" << captureDropDelta
                              << " fastDownAllowed=" << (fastDownAllowed ? "1" : "0")
                              << " hiLevelCand=" << (highLevelCandidate ? "1" : "0")
                              << " hiGateOk=" << (highLevelGateOk ? "1" : "0")
                              << " cpuHeadroomOk=" << (cpuHeadroomOk ? "1" : "0")
                              << " fpsNow=" << std::fixed << std::setprecision(2) << currentFps_.load()
                              << " fpsTarget=" << targetFpsEval
                              << " fastKbps=" << static_cast<int>(fastBps / 1000.0)
                              << " ewmaKbps=" << static_cast<int>(ewmaOutBitrateBps / 1000.0)
                              << " avg10sKbps=" << static_cast<int>(avg10sBps / 1000.0)
                              << " targetKbps=" << runtimeProfileTargetBitrateKbps_.load()
                              << std::endl;
                    lastAdaptLogTime = now;
                }

                if (canSwitch && runtimeAdaptBadSinceMs_.load() > 0 &&
                    nowMs - runtimeAdaptBadSinceMs_.load() >= static_cast<int64_t>(effectiveDownNeedSec) * 1000 &&
                    currentLevel > minLevel) {
                    const int nextLevel = currentLevel - 1;
                    const auto cfg = levelConfig(nextLevel, resolveOutputFps(config_.camera.fps, config_.encoder.fps));
                    runtimeProfileLevel_ = nextLevel;
                    runtimeProfileTargetWidth_ = cfg.width;
                    runtimeProfileTargetHeight_ = cfg.height;
                    runtimeProfileTargetFps_ = cfg.fps;
                    runtimeProfileTargetBitrateKbps_ = cfg.bitrateKbps;
                    runtimeLastSwitchMs_ = nowMs;
                    runtimeAdaptBadSinceMs_ = 0;
                    runtimeAdaptGoodSinceMs_ = 0;
                    std::cout << "[Adapt] auto downshift -> L" << nextLevel
                              << " reason=network_bad sendDropDelta=" << sendDropDelta
                              << " captureDropDelta=" << captureDropDelta
                              << " fpsNow=" << std::fixed << std::setprecision(2) << currentFps_.load()
                              << " outKbps=" << static_cast<int>(currentOutBitrateBps / 1000.0) << std::endl;
                } else if (canSwitch && runtimeAdaptGoodSinceMs_.load() > 0 &&
                           nowMs - runtimeAdaptGoodSinceMs_.load() >= static_cast<int64_t>(effectiveUpNeedSec) * 1000 &&
                           currentLevel < maxLevel &&
                           highLevelGateOk) {
                    const int nextLevel = currentLevel + 1;
                    const auto cfg = levelConfig(nextLevel, resolveOutputFps(config_.camera.fps, config_.encoder.fps));
                    runtimeProfileLevel_ = nextLevel;
                    runtimeProfileTargetWidth_ = cfg.width;
                    runtimeProfileTargetHeight_ = cfg.height;
                    runtimeProfileTargetFps_ = cfg.fps;
                    runtimeProfileTargetBitrateKbps_ = cfg.bitrateKbps;
                    runtimeLastSwitchMs_ = nowMs;
                    runtimeAdaptBadSinceMs_ = 0;
                    runtimeAdaptGoodSinceMs_ = 0;
                    std::cout << "[Adapt] auto upshift -> L" << nextLevel
                              << " reason=network_good outKbps=" << static_cast<int>(currentOutBitrateBps / 1000.0)
                              << std::endl;
                }
            }
            
            lastLogTime = now;
            totalProcessTime = 0;
            maxProcessTime = 0;
        }
    }

    {
        std::lock_guard<std::mutex> lock(captureMutex);
        captureQueue.clear();
    }
    captureCv.notify_all();
    if (captureThread.joinable()) {
        captureThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(sendMutex);
        sendStop = true;
    }
    sendCv.notify_all();
    if (sendThread.joinable()) {
        sendThread.join();
    }

    if (config_.detection.enabled) {
        {
            std::lock_guard<std::mutex> lock(detectMutex);
            detectStop = true;
            detectFrameReady = false;
        }
        detectCv.notify_one();
        if (detectThread.joinable()) {
            detectThread.join();
        }
    }
}

void Pipeline::audioLoop() {
    while (running_ && audio_) {
        AudioFrame audioFrame = audio_->captureFrame();
        if (audioFrame.empty()) {
            continue;
        }

        if (!livePushDesired_.load()) {
            continue;
        }

        bool sendOk = false;
        {
            std::lock_guard<std::mutex> streamLock(streamerMutex_);
            if (!livePushDesired_.load()) {
                continue;
            }
            if (!streamer_->isConnected()) {
                if (streamer_->connect(config_.stream)) {
                    livePushActive_ = true;
                    std::cout << "[Pipeline] RTMP push resumed (audio loop)" << std::endl;
                } else {
                    livePushActive_ = false;
                    continue;
                }
            }
            sendOk = streamer_->sendAudioPacket(audioFrame);
            if (!sendOk && !streamer_->isConnected()) {
                livePushActive_ = false;
            }
        }

        if (!sendOk) {
            std::cerr << "[Pipeline] Failed to send audio packet" << std::endl;
        }
    }
}

bool Pipeline::isRunning() const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->isRunning();
    }
    return running_;
}

uint64_t Pipeline::getFramesSent() const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->framesSent();
    }
    return framesSent_;
}

uint64_t Pipeline::getBytesSent() const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->bytesSent();
    }
    return bytesSent_;
}

double Pipeline::getCurrentFps() const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->currentFps();
    }
    return currentFps_;
}

bool Pipeline::setLivePushEnabled(bool enabled) {
    if (useStagePipeline_) {
        livePushDesired_ = enabled;
        if (!stagePipeline_) {
            livePushActive_ = false;
            return false;
        }
        const bool ok = stagePipeline_->setLiveEnabled(enabled);
        livePushActive_ = stagePipeline_->isLiveActive();
        return ok;
    }

    livePushDesired_ = enabled;
    if (!streamer_) return false;

    std::lock_guard<std::mutex> lock(streamerMutex_);
    if (!enabled) {
        if (streamer_->isConnected()) {
            streamer_->disconnect();
        }
        livePushActive_ = false;
        return true;
    }

    if (!running_) {
        return true;
    }

    if (!streamer_->isConnected()) {
        if (!streamer_->connect(config_.stream)) {
            livePushActive_ = false;
            return false;
        }
    }
    livePushActive_ = streamer_->isConnected();
    return livePushActive_;
}

bool Pipeline::isLivePushEnabled() const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->isLiveEnabled();
    }
    return livePushDesired_.load();
}

bool Pipeline::isLivePushActive() const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->isLiveActive();
    }
    return livePushActive_.load();
}

bool Pipeline::setRecordCleanupPolicy(int minFreePercent, int targetFreePercent) {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->setRecordCleanupPolicy(minFreePercent, targetFreePercent);
    }
    if (!recorder_ || !recorder_->isEnabled()) return false;
    return recorder_->setCleanupPolicy(minFreePercent, targetFreePercent);
}

bool Pipeline::getRecordCleanupPolicy(int& minFreePercent, int& targetFreePercent) const {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->getRecordCleanupPolicy(minFreePercent, targetFreePercent);
    }
    if (!recorder_ || !recorder_->isEnabled()) {
        minFreePercent = config_.record.minFreePercent;
        targetFreePercent = config_.record.targetFreePercent;
        return false;
    }
    recorder_->getCleanupPolicy(minFreePercent, targetFreePercent);
    return true;
}

bool Pipeline::applyRuntimeSettings(
    bool motionEnabled,
    bool personEnabled,
    bool soundEnabled,
    const std::string& motionSensitivity,
    const std::string& soundSensitivity,
    const std::string& detectionZones,
    bool watermarkEnabled
) {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->applyRuntimeSettings(
            motionEnabled,
            personEnabled,
            soundEnabled,
            motionSensitivity,
            soundSensitivity,
            detectionZones,
            watermarkEnabled
        );
    }
    runtimeMotionEnabled_ = motionEnabled;
    runtimePersonEnabled_ = personEnabled;
    runtimeSoundEnabled_ = soundEnabled;
    {
        std::lock_guard<std::mutex> lock(runtimeSettingsMutex_);
        runtimeMotionSensitivity_ = motionSensitivity.empty() ? "High" : motionSensitivity;
        runtimeSoundSensitivity_ = soundSensitivity.empty() ? "Loud" : soundSensitivity;
        runtimeDetectionZones_ = detectionZones.empty() ? "2 zones configured" : detectionZones;
    }
    runtimeWatermarkEnabled_ = watermarkEnabled;
    return true;
}

void Pipeline::getRuntimeSettings(
    bool& motionEnabled,
    bool& personEnabled,
    bool& soundEnabled,
    std::string& motionSensitivity,
    std::string& soundSensitivity,
    std::string& detectionZones,
    bool& watermarkEnabled
) const {
    if (useStagePipeline_ && stagePipeline_) {
        stagePipeline_->getRuntimeSettings(
            motionEnabled,
            personEnabled,
            soundEnabled,
            motionSensitivity,
            soundSensitivity,
            detectionZones,
            watermarkEnabled
        );
        return;
    }
    motionEnabled = runtimeMotionEnabled_.load();
    personEnabled = runtimePersonEnabled_.load();
    soundEnabled = runtimeSoundEnabled_.load();
    {
        std::lock_guard<std::mutex> lock(runtimeSettingsMutex_);
        motionSensitivity = runtimeMotionSensitivity_;
        soundSensitivity = runtimeSoundSensitivity_;
        detectionZones = runtimeDetectionZones_;
    }
    watermarkEnabled = runtimeWatermarkEnabled_.load();
}

bool Pipeline::applyRuntimeVisualSettings(int imageFlipMode, bool nightVisionEnabled, int nightVisionMode) {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->applyRuntimeVisualSettings(imageFlipMode, nightVisionEnabled, nightVisionMode);
    }
    const int flip = std::max(0, std::min(3, imageFlipMode));
    const int night = std::max(0, std::min(2, nightVisionMode));
    runtimeImageFlipMode_ = flip;
    runtimeNightVisionEnabled_ = nightVisionEnabled;
    runtimeNightVisionMode_ = night;
    return true;
}

void Pipeline::getRuntimeVisualSettings(int& imageFlipMode, bool& nightVisionEnabled, int& nightVisionMode) const {
    if (useStagePipeline_ && stagePipeline_) {
        stagePipeline_->getRuntimeVisualSettings(imageFlipMode, nightVisionEnabled, nightVisionMode);
        return;
    }
    imageFlipMode = runtimeImageFlipMode_.load();
    nightVisionEnabled = runtimeNightVisionEnabled_.load();
    nightVisionMode = runtimeNightVisionMode_.load();
}

bool Pipeline::applyRuntimeStreamPolicy(
    const std::string& streamProfile,
    const std::string& streamMode,
    int manualLevel,
    int autoMinLevel,
    int autoMaxLevel,
    const std::string& autoPolicy,
    int autoCooldownSec,
    int autoUpHoldSec,
    int autoDownHoldSec
) {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->applyRuntimeStreamPolicy(
            streamProfile,
            streamMode,
            manualLevel,
            autoMinLevel,
            autoMaxLevel,
            autoPolicy,
            autoCooldownSec,
            autoUpHoldSec,
            autoDownHoldSec
        );
    }
    auto normalizeProfile = [](const std::string& raw) {
        std::string p = raw;
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (p == "360p" || p == "540p" || p == "720p" || p == "1080p" || p == "auto") return p;
        return std::string("auto");
    };
    auto profileToLevel = [](const std::string& profile) {
        if (profile == "360p") return 0;
        if (profile == "540p") return 1;
        if (profile == "720p") return 2;
        if (profile == "1080p") return 4;
        return 2;
    };

    const std::string profile = normalizeProfile(streamProfile);
    const bool profileManual = profile != "auto";
    const std::string mode = profileManual ? "manual" : ((streamMode == "manual") ? "manual" : "auto");
    const int manual = profileManual ? profileToLevel(profile) : std::max(0, std::min(4, manualLevel));
    const int minL = std::max(0, std::min(4, std::min(autoMinLevel, autoMaxLevel)));
    const int maxL = std::max(0, std::min(4, std::max(autoMinLevel, autoMaxLevel)));
    std::string policy = autoPolicy;
    std::transform(policy.begin(), policy.end(), policy.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (policy != "stable" && policy != "balanced" && policy != "quality") {
        policy = "balanced";
    }

    {
        std::lock_guard<std::mutex> lock(runtimeStreamMutex_);
        runtimeStreamProfile_ = profile;
        runtimeStreamMode_ = mode;
        runtimeManualLevel_ = manual;
        runtimeAutoMinLevel_ = minL;
        runtimeAutoMaxLevel_ = maxL;
        runtimeAutoPolicy_ = policy;
        runtimeAutoCooldownSec_ = std::max(3, std::min(120, autoCooldownSec));
        runtimeAutoUpHoldSec_ = std::max(5, std::min(180, autoUpHoldSec));
        runtimeAutoDownHoldSec_ = std::max(1, std::min(60, autoDownHoldSec));
    }

    if (mode == "manual") {
        const auto cfg = levelConfig(manual, resolveOutputFps(config_.camera.fps, config_.encoder.fps));
        runtimeProfileLevel_ = manual;
        runtimeProfileTargetWidth_ = cfg.width;
        runtimeProfileTargetHeight_ = cfg.height;
        runtimeProfileTargetFps_ = cfg.fps;
        runtimeProfileTargetBitrateKbps_ = cfg.bitrateKbps;
        runtimeLastSwitchMs_ = wallClockMs();
        runtimeAdaptBadSinceMs_ = 0;
        runtimeAdaptGoodSinceMs_ = 0;
    } else {
        int current = runtimeProfileLevel_.load();
        if (current < minL) current = minL;
        if (current > maxL) current = maxL;
        const auto cfg = levelConfig(current, resolveOutputFps(config_.camera.fps, config_.encoder.fps));
        runtimeProfileLevel_ = current;
        runtimeProfileTargetWidth_ = cfg.width;
        runtimeProfileTargetHeight_ = cfg.height;
        runtimeProfileTargetFps_ = cfg.fps;
        runtimeProfileTargetBitrateKbps_ = cfg.bitrateKbps;
    }
    return true;
}

void Pipeline::getRuntimeStreamPolicy(
    std::string& streamProfile,
    std::string& streamMode,
    int& manualLevel,
    int& autoMinLevel,
    int& autoMaxLevel,
    std::string& autoPolicy,
    int& autoCooldownSec,
    int& autoUpHoldSec,
    int& autoDownHoldSec,
    int& currentLevel,
    int& targetFps,
    int& targetBitrateKbps
) const {
    if (useStagePipeline_ && stagePipeline_) {
        stagePipeline_->getRuntimeStreamPolicy(
            streamProfile,
            streamMode,
            manualLevel,
            autoMinLevel,
            autoMaxLevel,
            autoPolicy,
            autoCooldownSec,
            autoUpHoldSec,
            autoDownHoldSec,
            currentLevel,
            targetFps,
            targetBitrateKbps
        );
        return;
    }
    {
        std::lock_guard<std::mutex> lock(runtimeStreamMutex_);
        streamProfile = runtimeStreamProfile_;
        streamMode = runtimeStreamMode_;
        manualLevel = runtimeManualLevel_;
        autoMinLevel = runtimeAutoMinLevel_;
        autoMaxLevel = runtimeAutoMaxLevel_;
        autoPolicy = runtimeAutoPolicy_;
        autoCooldownSec = runtimeAutoCooldownSec_;
        autoUpHoldSec = runtimeAutoUpHoldSec_;
        autoDownHoldSec = runtimeAutoDownHoldSec_;
    }
    currentLevel = runtimeProfileLevel_.load();
    targetFps = runtimeProfileTargetFps_.load();
    targetBitrateKbps = runtimeProfileTargetBitrateKbps_.load();
}

bool Pipeline::applyRuntimePtzCommand(
    const std::string& action,
    int speed,
    int zoomStep,
    int zoomLevel,
    const std::string& preset
) {
    if (useStagePipeline_ && stagePipeline_) {
        return stagePipeline_->applyRuntimePtzCommand(action, speed, zoomStep, zoomLevel, preset);
    }
    std::string safeAction = action;
    std::transform(safeAction.begin(), safeAction.end(), safeAction.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (safeAction.empty()) return false;
    speed = std::max(1, std::min(10, speed));
    zoomStep = std::max(1, std::min(10, zoomStep));
    zoomLevel = std::max(0, std::min(100, zoomLevel));

    {
        std::lock_guard<std::mutex> lock(runtimePtzMutex_);
        runtimePtzAction_ = safeAction;
        runtimePtzSpeed_ = speed;
        runtimePtzZoomStep_ = zoomStep;
        if (safeAction == "zoom_in") {
            runtimePtzZoomLevel_ = std::max(0, std::min(100, runtimePtzZoomLevel_ + zoomStep * 5));
        } else if (safeAction == "zoom_out") {
            runtimePtzZoomLevel_ = std::max(0, std::min(100, runtimePtzZoomLevel_ - zoomStep * 5));
        } else if (safeAction == "zoom_set") {
            runtimePtzZoomLevel_ = zoomLevel;
        } else if (zoomLevel >= 0) {
            runtimePtzZoomLevel_ = zoomLevel;
        }
        runtimePtzPreset_ = preset;
        runtimePtzUpdatedAtMs_ = wallClockMs();
    }
    return true;
}

void Pipeline::getRuntimePtzState(
    std::string& action,
    int& speed,
    int& zoomStep,
    int& zoomLevel,
    std::string& preset,
    int64_t& updatedAtMs
) const {
    if (useStagePipeline_ && stagePipeline_) {
        stagePipeline_->getRuntimePtzState(action, speed, zoomStep, zoomLevel, preset, updatedAtMs);
        return;
    }
    std::lock_guard<std::mutex> lock(runtimePtzMutex_);
    action = runtimePtzAction_;
    speed = runtimePtzSpeed_;
    zoomStep = runtimePtzZoomStep_;
    zoomLevel = runtimePtzZoomLevel_;
    preset = runtimePtzPreset_;
    updatedAtMs = runtimePtzUpdatedAtMs_;
}

} // namespace reallive
