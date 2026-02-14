#include "core/Pipeline.h"
#include "core/TextOverlay.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

// Platform-specific includes (Raspberry Pi 5)
#include "platform/rpi5/LibcameraCapture.h"
#include "platform/rpi5/AvcodecEncoder.h"
#include "platform/rpi5/AlsaCapture.h"
#include "platform/rpi5/RtmpStreamer.h"
#include "core/LocalRecorder.h"

namespace reallive {

namespace {

constexpr std::array<uint8_t, 16> kTelemetrySeiUuid = {
    0x52, 0x65, 0x61, 0x4C, 0x69, 0x76, 0x65, 0x53,
    0x65, 0x69, 0x4D, 0x65, 0x74, 0x72, 0x69, 0x63
};

struct SystemTelemetry {
    double cpuPct = 0.0;
    double memoryPct = 0.0;
    double memoryUsedMb = 0.0;
    double memoryTotalMb = 0.0;
    double storagePct = 0.0;
    double storageUsedGb = 0.0;
    double storageTotalGb = 0.0;
};

struct CpuCounters {
    uint64_t total = 0;
    uint64_t idle = 0;
    bool valid = false;
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

CpuCounters readCpuCounters() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return {};

    std::string line;
    if (!std::getline(file, line)) return {};

    std::istringstream iss(line);
    std::string cpu;
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
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guestNice;
    if (cpu != "cpu") return {};

    CpuCounters counters;
    counters.idle = idle + iowait;
    counters.total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guestNice;
    counters.valid = true;
    return counters;
}

class SystemUsageSampler {
public:
    SystemTelemetry sample() {
        SystemTelemetry telemetry;

        CpuCounters current = readCpuCounters();
        if (current.valid && previous_.valid && current.total > previous_.total) {
            const double totalDelta = static_cast<double>(current.total - previous_.total);
            const double idleDelta = static_cast<double>(current.idle - previous_.idle);
            telemetry.cpuPct = clampPercent((1.0 - idleDelta / totalDelta) * 100.0);
        } else {
            telemetry.cpuPct = 0.0;
        }
        if (current.valid) {
            previous_ = current;
        }

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
    CpuCounters previous_;
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

std::string buildTelemetryPayload(const PusherConfig& config, const SystemTelemetry& telemetry, int64_t nowMs) {
    std::ostringstream oss;
    oss << "{"
        << "\"v\":1,"
        << "\"ts\":" << nowMs << ","
        << "\"stream_key\":\"" << jsonEscape(config.stream.streamKey) << "\","
        << "\"device\":{"
            << "\"cpu_pct\":" << formatNumber(telemetry.cpuPct) << ","
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
            << "\"audio_enabled\":" << (config.enableAudio ? "true" : "false")
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
            << "\"gop\":{\"min\":10,\"max\":120,\"step\":5}"
        << "}"
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
    uint64_t fpsFrameCount = 0;
    uint64_t droppedFrames = 0;
    uint64_t totalProcessTime = 0;
    uint64_t maxProcessTime = 0;
    SystemUsageSampler usageSampler;
    const auto seiInterval = std::chrono::milliseconds(1000);

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

        auto now = Clock::now();
        if (now - lastSeiTime >= seiInterval) {
            const SystemTelemetry telemetry = usageSampler.sample();
            const std::string payload = buildTelemetryPayload(config_, telemetry, wallClockMs());
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
