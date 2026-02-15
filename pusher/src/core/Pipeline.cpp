#include "core/Pipeline.h"
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
#include "platform/rpi5/LibcameraCapture.h"
#include "platform/rpi5/AvcodecEncoder.h"
#include "platform/rpi5/AlsaCapture.h"
#include "platform/rpi5/RtmpStreamer.h"
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

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
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
    int64_t nowMs,
    const PersonBox& personState,
    const std::vector<PersonBox>& personEvents
) {
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
            << "\"storage_total_gb\":" << formatNumber(telemetry.storageTotalGb, 2)
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
            int64_t lastPersonEventMs = 0;

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
                    if (person.valid &&
                        (lastPersonEventMs <= 0 ||
                         localTs - lastPersonEventMs >= config_.detection.eventMinIntervalMs)) {
                        lastPersonEventMs = localTs;
                        pendingPersonEvents.push_back(person);
                        if (pendingPersonEvents.size() > 8) {
                            pendingPersonEvents.erase(pendingPersonEvents.begin());
                        }
                        shouldWriteEvent = true;
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

            if (!streamer_->sendVideoPacket(packet)) {
                std::cerr << "[Pipeline] Failed to send video packet" << std::endl;
                if (!streamer_->isConnected()) {
                    std::cerr << "[Pipeline] Streamer disconnected, stopping" << std::endl;
                    running_ = false;
                    captureCv.notify_all();
                    detectCv.notify_all();
                    sendCv.notify_all();
                }
                continue;
            }

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
    constexpr int64_t kOverlayFreshMs = 160;

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

        auto captureTime = Clock::now();
        const int64_t frameTsMs = (frame.pts > 0) ? (frame.pts / 1000) : wallClockMs();

        if (config_.detection.enabled) {
            Frame frameForDetect = frame;
            {
                std::lock_guard<std::mutex> lock(detectMutex);
                detectFrame = std::move(frameForDetect);
                detectFrameTsMs = frameTsMs;
                detectFrameReady = true;
            }
            detectCv.notify_one();

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

        auto now = Clock::now();
        if (now - lastSeiTime >= seiInterval) {
            const SystemTelemetry telemetry = usageSampler.sample();
            PersonBox personSnapshot;
            std::vector<PersonBox> eventSnapshot;
            if (config_.detection.enabled) {
                std::lock_guard<std::mutex> lock(detectMutex);
                personSnapshot = latestPerson;
                eventSnapshot = pendingPersonEvents;
                pendingPersonEvents.clear();
            }
            const std::string payload = buildTelemetryPayload(
                config_,
                telemetry,
                wallClockMs(),
                personSnapshot,
                eventSnapshot
            );
            injectTelemetrySei(packet.data, payload);
            lastSeiTime = now;
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
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime);
        if (elapsed.count() >= 1000) {
            const uint64_t sentNow = framesSent_.load();
            const uint64_t sentDelta = sentNow >= lastFramesSentForFps ? (sentNow - lastFramesSentForFps) : 0;
            currentFps_ = static_cast<double>(sentDelta) * 1000.0 / elapsed.count();
            lastFramesSentForFps = sentNow;
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
                      << " | P99: " << p99Time / 1000 << "ms" << std::endl;
            
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
