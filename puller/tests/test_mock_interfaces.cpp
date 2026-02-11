/**
 * Puller Interface Mock Tests
 *
 * Tests the puller interfaces (IStreamReceiver, IDecoder, IStorage, IRenderer)
 * using mock implementations to validate lifecycle and state management.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <queue>

// Type definitions

struct DecoderConfig {
    std::string codec = "h264";
    int width = 1920;
    int height = 1080;
};

struct StorageConfig {
    std::string format = "mp4";
    int64_t max_file_size_bytes = 500 * 1024 * 1024;  // 500 MB
    int max_duration_sec = 3600;
};

struct RenderConfig {
    int width = 640;
    int height = 480;
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    int64_t dts = 0;
    bool is_keyframe = false;
};

struct Frame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t timestamp_us = 0;
};

// Interface definitions (from architecture)

class IStreamReceiver {
public:
    virtual ~IStreamReceiver() = default;
    virtual bool connect(const std::string& url) = 0;
    virtual EncodedPacket receivePacket() = 0;
    virtual void disconnect() = 0;
};

class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual bool init(const DecoderConfig& config) = 0;
    virtual Frame decode(const EncodedPacket& packet) = 0;
    virtual void flush() = 0;
};

class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool open(const std::string& path, const StorageConfig& config) = 0;
    virtual bool writePacket(const EncodedPacket& packet) = 0;
    virtual void close() = 0;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(const RenderConfig& config) = 0;
    virtual void renderFrame(const Frame& frame) = 0;
    virtual void close() = 0;
};

// Mock Implementations

class MockStreamReceiver : public IStreamReceiver {
public:
    bool connected = false;
    std::string last_url;
    std::queue<EncodedPacket> packet_queue;
    bool fail_connect = false;

    bool connect(const std::string& url) override {
        if (fail_connect) return false;
        if (connected) return false;
        last_url = url;
        connected = true;
        return true;
    }

    EncodedPacket receivePacket() override {
        if (!connected || packet_queue.empty()) {
            return EncodedPacket{};
        }
        EncodedPacket pkt = packet_queue.front();
        packet_queue.pop();
        return pkt;
    }

    void disconnect() override {
        connected = false;
    }

    // Test helper: enqueue packets
    void enqueuePacket(EncodedPacket pkt) {
        packet_queue.push(std::move(pkt));
    }
};

class MockDecoder : public IDecoder {
public:
    bool initialized = false;
    int frames_decoded = 0;
    bool flushed = false;
    DecoderConfig last_config;

    bool init(const DecoderConfig& config) override {
        if (initialized) return false;
        last_config = config;
        initialized = true;
        return true;
    }

    Frame decode(const EncodedPacket& packet) override {
        Frame f;
        if (!initialized || packet.data.empty()) return f;
        f.width = last_config.width;
        f.height = last_config.height;
        f.data.resize(f.width * f.height * 3 / 2);  // YUV420
        f.timestamp_us = packet.pts;
        frames_decoded++;
        return f;
    }

    void flush() override {
        flushed = true;
    }
};

class MockStorage : public IStorage {
public:
    bool is_open = false;
    std::string current_path;
    StorageConfig current_config;
    int64_t bytes_written = 0;
    int packets_written = 0;
    bool fail_write = false;

    bool open(const std::string& path, const StorageConfig& config) override {
        if (is_open) return false;
        current_path = path;
        current_config = config;
        bytes_written = 0;
        packets_written = 0;
        is_open = true;
        return true;
    }

    bool writePacket(const EncodedPacket& packet) override {
        if (!is_open || fail_write) return false;
        bytes_written += packet.data.size();
        packets_written++;
        return true;
    }

    void close() override {
        is_open = false;
    }

    bool needsRotation() const {
        return bytes_written >= current_config.max_file_size_bytes;
    }
};

class MockRenderer : public IRenderer {
public:
    bool initialized = false;
    int frames_rendered = 0;

    bool init(const RenderConfig& config) override {
        if (initialized) return false;
        initialized = true;
        return true;
    }

    void renderFrame(const Frame& frame) override {
        if (initialized && !frame.data.empty()) {
            frames_rendered++;
        }
    }

    void close() override {
        initialized = false;
    }
};

// ============================================================
// Tests
// ============================================================

// R-002: Stream receiver lifecycle
class StreamReceiverTest : public ::testing::Test {
protected:
    MockStreamReceiver receiver;
};

TEST_F(StreamReceiverTest, R002_ConnectReceiveDisconnect) {
    EXPECT_FALSE(receiver.connected);

    EXPECT_TRUE(receiver.connect("ws://localhost:3000/signaling"));
    EXPECT_TRUE(receiver.connected);
    EXPECT_EQ(receiver.last_url, "ws://localhost:3000/signaling");

    // Enqueue test packet
    EncodedPacket pkt;
    pkt.data = {0x00, 0x01, 0x02, 0x03};
    pkt.pts = 1000;
    pkt.is_keyframe = true;
    receiver.enqueuePacket(pkt);

    EncodedPacket received = receiver.receivePacket();
    EXPECT_EQ(received.data.size(), 4u);
    EXPECT_EQ(received.pts, 1000);
    EXPECT_TRUE(received.is_keyframe);

    receiver.disconnect();
    EXPECT_FALSE(receiver.connected);
}

TEST_F(StreamReceiverTest, ReceiveWhenEmpty) {
    receiver.connect("ws://localhost:3000/signaling");
    EncodedPacket pkt = receiver.receivePacket();
    EXPECT_TRUE(pkt.data.empty());
}

TEST_F(StreamReceiverTest, ReceiveWhenDisconnected) {
    EncodedPacket pkt = receiver.receivePacket();
    EXPECT_TRUE(pkt.data.empty());
}

TEST_F(StreamReceiverTest, ConnectionFailure) {
    receiver.fail_connect = true;
    EXPECT_FALSE(receiver.connect("ws://localhost:3000/signaling"));
    EXPECT_FALSE(receiver.connected);
}

TEST_F(StreamReceiverTest, CannotConnectTwice) {
    EXPECT_TRUE(receiver.connect("ws://localhost:3000/signaling"));
    EXPECT_FALSE(receiver.connect("ws://localhost:3000/signaling"));
}

// R-003: Decoder lifecycle
class DecoderTest : public ::testing::Test {
protected:
    MockDecoder decoder;
    DecoderConfig config;

    void SetUp() override {
        config.codec = "h264";
        config.width = 1280;
        config.height = 720;
    }
};

TEST_F(DecoderTest, R003_InitDecodeFlush) {
    EXPECT_TRUE(decoder.init(config));
    EXPECT_TRUE(decoder.initialized);
    EXPECT_EQ(decoder.last_config.codec, "h264");

    EncodedPacket pkt;
    pkt.data.resize(10000);
    pkt.pts = 33333;

    Frame frame = decoder.decode(pkt);
    EXPECT_EQ(frame.width, 1280);
    EXPECT_EQ(frame.height, 720);
    EXPECT_GT(frame.data.size(), 0u);
    EXPECT_EQ(frame.timestamp_us, 33333);
    EXPECT_EQ(decoder.frames_decoded, 1);

    decoder.flush();
    EXPECT_TRUE(decoder.flushed);
}

TEST_F(DecoderTest, DecodeWithoutInit) {
    EncodedPacket pkt;
    pkt.data.resize(100);
    Frame frame = decoder.decode(pkt);
    EXPECT_EQ(frame.data.size(), 0u);
}

TEST_F(DecoderTest, DecodeEmptyPacket) {
    decoder.init(config);
    EncodedPacket pkt;  // empty data
    Frame frame = decoder.decode(pkt);
    EXPECT_EQ(frame.data.size(), 0u);
}

// R-004: Storage write lifecycle
class StorageTest : public ::testing::Test {
protected:
    MockStorage storage;
    StorageConfig config;

    void SetUp() override {
        config.format = "mp4";
        config.max_file_size_bytes = 1024 * 1024;  // 1 MB for testing
        config.max_duration_sec = 60;
    }
};

TEST_F(StorageTest, R004_OpenWriteClose) {
    EXPECT_TRUE(storage.open("/tmp/test_output.mp4", config));
    EXPECT_TRUE(storage.is_open);
    EXPECT_EQ(storage.current_path, "/tmp/test_output.mp4");

    EncodedPacket pkt;
    pkt.data.resize(5000);
    EXPECT_TRUE(storage.writePacket(pkt));
    EXPECT_EQ(storage.packets_written, 1);
    EXPECT_EQ(storage.bytes_written, 5000);

    storage.close();
    EXPECT_FALSE(storage.is_open);
}

TEST_F(StorageTest, WriteWithoutOpen) {
    EncodedPacket pkt;
    pkt.data.resize(100);
    EXPECT_FALSE(storage.writePacket(pkt));
}

TEST_F(StorageTest, WriteFailure) {
    storage.open("/tmp/test.mp4", config);
    storage.fail_write = true;
    EncodedPacket pkt;
    pkt.data.resize(100);
    EXPECT_FALSE(storage.writePacket(pkt));
}

TEST_F(StorageTest, CannotOpenTwice) {
    EXPECT_TRUE(storage.open("/tmp/test1.mp4", config));
    EXPECT_FALSE(storage.open("/tmp/test2.mp4", config));
}

// Renderer test
TEST(RendererTest, InitRenderClose) {
    MockRenderer renderer;
    RenderConfig config;
    config.width = 320;
    config.height = 240;

    EXPECT_TRUE(renderer.init(config));

    Frame frame;
    frame.width = 1280;
    frame.height = 720;
    frame.data.resize(1280 * 720 * 3 / 2);
    renderer.renderFrame(frame);
    EXPECT_EQ(renderer.frames_rendered, 1);

    renderer.close();
    EXPECT_FALSE(renderer.initialized);
}

// Full puller pipeline test
TEST(PullerPipelineTest, ReceiveDecodeStore) {
    MockStreamReceiver receiver;
    MockDecoder decoder;
    MockStorage storage;

    DecoderConfig dec_config;
    dec_config.width = 1280;
    dec_config.height = 720;

    StorageConfig stor_config;
    stor_config.format = "mp4";
    stor_config.max_file_size_bytes = 100 * 1024 * 1024;

    ASSERT_TRUE(receiver.connect("ws://localhost:3000/signaling"));
    ASSERT_TRUE(decoder.init(dec_config));
    ASSERT_TRUE(storage.open("/tmp/recording.mp4", stor_config));

    // Enqueue 5 test packets
    for (int i = 0; i < 5; i++) {
        EncodedPacket pkt;
        pkt.data.resize(10000);
        pkt.pts = i * 33333;
        pkt.is_keyframe = (i == 0);
        receiver.enqueuePacket(pkt);
    }

    // Process pipeline
    int processed = 0;
    while (true) {
        EncodedPacket pkt = receiver.receivePacket();
        if (pkt.data.empty()) break;

        Frame frame = decoder.decode(pkt);
        ASSERT_GT(frame.data.size(), 0u);

        ASSERT_TRUE(storage.writePacket(pkt));
        processed++;
    }

    EXPECT_EQ(processed, 5);
    EXPECT_EQ(decoder.frames_decoded, 5);
    EXPECT_EQ(storage.packets_written, 5);
    EXPECT_EQ(storage.bytes_written, 50000);

    storage.close();
    decoder.flush();
    receiver.disconnect();
}
