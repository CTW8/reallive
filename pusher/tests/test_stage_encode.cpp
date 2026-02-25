#include <gtest/gtest.h>

#include "core/Config.h"
#include "core/stage/EdgeQueue.h"
#include "core/stage/EncodeStage.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace reallive;
using namespace reallive::stage;

namespace {

class MockEncoder final : public IEncoder {
public:
    bool init(const EncoderConfig&) override {
        initialized_ = true;
        return true;
    }

    EncodedPacket encode(const Frame& frame) override {
        EncodedPacket pkt;
        if (!initialized_) return pkt;
        pkt.pts = frame.pts;
        pkt.dts = frame.pts;
        pkt.isKeyframe = false;
        pkt.data.resize(packetSize_, 0x11);
        encodeCount_++;
        return pkt;
    }

    void flush() override {}

    std::string getName() const override {
        return "MockEncoder";
    }

    void setPacketSize(size_t size) {
        packetSize_ = size;
    }

    uint64_t encodeCount() const {
        return encodeCount_.load();
    }

private:
    bool initialized_ = false;
    size_t packetSize_ = 800;
    std::atomic<uint64_t> encodeCount_{0};
};

ProcessedFramePacket makeFrame(uint64_t id, int64_t tsMs) {
    ProcessedFramePacket p;
    p.trace.traceId = id;
    p.trace.frameId = id;
    p.trace.ingestTsMs = tsMs;
    p.tsMs = tsMs;
    p.ptsUs = tsMs * 1000;
    auto fb = std::make_shared<FrameBuffer>();
    fb->width = 16;
    fb->height = 16;
    fb->stride = 16;
    fb->pixelFormat = "NV12";
    fb->data.resize(static_cast<size_t>(fb->width) * static_cast<size_t>(fb->height) * 3 / 2, 0x10);
    p.frame = std::move(fb);
    return p;
}

} // namespace

TEST(EncodeStageTest, RuntimeTargetFpsDropsExcessFrames) {
    auto mock = std::make_unique<MockEncoder>();
    auto* mockRaw = mock.get();
    EncodeStage stage(std::move(mock));

    PusherConfig cfg;
    cfg.encoder.fps = 30;
    cfg.encoder.bitrate = 2000000;
    StageInitContext ctx{&cfg};
    ASSERT_TRUE(stage.init(ctx));

    auto inQ = std::make_shared<EdgeQueue<ProcessedFramePacket>>(64, QueuePolicy::kDropOldest);
    auto outQ = std::make_shared<EdgeQueue<EncodedPacketEx>>(64, QueuePolicy::kDropOldest);
    stage.setInputQueue(inQ);
    stage.setOutputQueue(outQ);
    stage.setRuntimeTarget(10, 4000);

    ASSERT_TRUE(stage.start());

    for (uint64_t i = 0; i < 30; ++i) {
        ASSERT_TRUE(inQ->push(makeFrame(i + 1, static_cast<int64_t>(i) * 10)));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stage.requestStop();
    stage.join();

    size_t outCount = 0;
    EncodedPacketEx pkt;
    while (outQ->pop(pkt, std::chrono::milliseconds(1))) {
        outCount++;
    }

    EXPECT_GT(mockRaw->encodeCount(), 0u);
    EXPECT_LT(outCount, 10u);
}

TEST(EncodeStageTest, RuntimeTargetBitrateDropsNonKeyPackets) {
    auto mock = std::make_unique<MockEncoder>();
    auto* mockRaw = mock.get();
    mockRaw->setPacketSize(800);
    EncodeStage stage(std::move(mock));

    PusherConfig cfg;
    cfg.encoder.fps = 30;
    cfg.encoder.bitrate = 2000000;
    StageInitContext ctx{&cfg};
    ASSERT_TRUE(stage.init(ctx));

    auto inQ = std::make_shared<EdgeQueue<ProcessedFramePacket>>(64, QueuePolicy::kDropOldest);
    auto outQ = std::make_shared<EdgeQueue<EncodedPacketEx>>(64, QueuePolicy::kDropOldest);
    stage.setInputQueue(inQ);
    stage.setOutputQueue(outQ);
    stage.setRuntimeTarget(200, 10); // 10kbps ~= 1250 bytes/s

    ASSERT_TRUE(stage.start());

    for (uint64_t i = 0; i < 10; ++i) {
        ASSERT_TRUE(inQ->push(makeFrame(i + 1, static_cast<int64_t>(i) * 80)));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stage.requestStop();
    stage.join();

    size_t outCount = 0;
    EncodedPacketEx pkt;
    while (outQ->pop(pkt, std::chrono::milliseconds(1))) {
        outCount++;
    }

    EXPECT_GT(mockRaw->encodeCount(), 0u);
    EXPECT_LE(outCount, 2u);
}
