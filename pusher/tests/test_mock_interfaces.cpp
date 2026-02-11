/**
 * Pusher Interface Mock Tests
 *
 * Tests the pusher interfaces (ICameraCapture, IEncoder, IStreamer)
 * using mock implementations to validate lifecycle and state management.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

// Minimal type definitions (matching architecture's interface design)

struct CaptureConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string device = "/dev/video0";
};

struct AudioConfig {
    std::string device = "default";
    int sample_rate = 48000;
    int channels = 1;
};

struct EncoderConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 4000000;
    std::string codec = "h264";
};

struct Frame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t timestamp_us = 0;
};

struct AudioFrame {
    std::vector<uint8_t> data;
    int sample_rate = 0;
    int channels = 0;
    int64_t timestamp_us = 0;
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    int64_t dts = 0;
    bool is_keyframe = false;
};

// Interface definitions (from architecture)

class ICameraCapture {
public:
    virtual ~ICameraCapture() = default;
    virtual bool open(const CaptureConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual Frame captureFrame() = 0;
};

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual bool open(const AudioConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual AudioFrame captureFrame() = 0;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool init(const EncoderConfig& config) = 0;
    virtual EncodedPacket encode(const Frame& frame) = 0;
    virtual void flush() = 0;
};

class IStreamer {
public:
    virtual ~IStreamer() = default;
    virtual bool connect(const std::string& url, const std::string& streamKey) = 0;
    virtual bool sendPacket(const EncodedPacket& packet) = 0;
    virtual void disconnect() = 0;
};

// Mock Implementations

class MockCameraCapture : public ICameraCapture {
public:
    enum State { CLOSED, OPENED, STARTED, STOPPED };
    State state = CLOSED;
    CaptureConfig last_config;
    int frames_captured = 0;

    bool open(const CaptureConfig& config) override {
        if (state != CLOSED) return false;
        last_config = config;
        state = OPENED;
        return true;
    }

    bool start() override {
        if (state != OPENED && state != STOPPED) return false;
        state = STARTED;
        return true;
    }

    bool stop() override {
        if (state != STARTED) return false;
        state = STOPPED;
        return true;
    }

    Frame captureFrame() override {
        Frame f;
        if (state == STARTED) {
            f.width = last_config.width;
            f.height = last_config.height;
            f.data.resize(f.width * f.height * 3 / 2);  // YUV420
            f.timestamp_us = frames_captured * (1000000 / last_config.fps);
            frames_captured++;
        }
        return f;
    }
};

class MockAudioCapture : public IAudioCapture {
public:
    enum State { CLOSED, OPENED, STARTED, STOPPED };
    State state = CLOSED;
    int frames_captured = 0;

    bool open(const AudioConfig& config) override {
        if (state != CLOSED) return false;
        state = OPENED;
        return true;
    }

    bool start() override {
        if (state != OPENED && state != STOPPED) return false;
        state = STARTED;
        return true;
    }

    bool stop() override {
        if (state != STARTED) return false;
        state = STOPPED;
        return true;
    }

    AudioFrame captureFrame() override {
        AudioFrame f;
        if (state == STARTED) {
            f.sample_rate = 48000;
            f.channels = 1;
            f.data.resize(960 * 2);  // 20ms at 48kHz, 16-bit
            f.timestamp_us = frames_captured * 20000;  // 20ms per frame
            frames_captured++;
        }
        return f;
    }
};

class MockEncoder : public IEncoder {
public:
    bool initialized = false;
    int frames_encoded = 0;
    int keyframe_interval = 30;
    bool flushed = false;

    bool init(const EncoderConfig& config) override {
        if (initialized) return false;
        initialized = true;
        return true;
    }

    EncodedPacket encode(const Frame& frame) override {
        EncodedPacket pkt;
        if (!initialized) return pkt;
        pkt.data.resize(frame.data.size() / 10);  // ~10x compression
        pkt.pts = frame.timestamp_us;
        pkt.dts = frame.timestamp_us;
        pkt.is_keyframe = (frames_encoded % keyframe_interval == 0);
        frames_encoded++;
        return pkt;
    }

    void flush() override {
        flushed = true;
    }
};

class MockStreamer : public IStreamer {
public:
    bool connected = false;
    std::string last_url;
    std::string last_stream_key;
    int packets_sent = 0;
    bool fail_connect = false;
    bool fail_send = false;

    bool connect(const std::string& url, const std::string& streamKey) override {
        if (fail_connect) return false;
        if (connected) return false;
        last_url = url;
        last_stream_key = streamKey;
        connected = true;
        return true;
    }

    bool sendPacket(const EncodedPacket& packet) override {
        if (!connected || fail_send) return false;
        packets_sent++;
        return true;
    }

    void disconnect() override {
        connected = false;
    }
};

// ============================================================
// Tests
// ============================================================

// P-004: Mock camera lifecycle
class CameraCaptureTest : public ::testing::Test {
protected:
    MockCameraCapture camera;
    CaptureConfig config;

    void SetUp() override {
        config.width = 1280;
        config.height = 720;
        config.fps = 30;
        config.device = "/dev/video0";
    }
};

TEST_F(CameraCaptureTest, P004_OpenStartStopLifecycle) {
    EXPECT_EQ(camera.state, MockCameraCapture::CLOSED);

    EXPECT_TRUE(camera.open(config));
    EXPECT_EQ(camera.state, MockCameraCapture::OPENED);
    EXPECT_EQ(camera.last_config.width, 1280);

    EXPECT_TRUE(camera.start());
    EXPECT_EQ(camera.state, MockCameraCapture::STARTED);

    EXPECT_TRUE(camera.stop());
    EXPECT_EQ(camera.state, MockCameraCapture::STOPPED);
}

TEST_F(CameraCaptureTest, CannotOpenTwice) {
    EXPECT_TRUE(camera.open(config));
    EXPECT_FALSE(camera.open(config));
}

TEST_F(CameraCaptureTest, CannotStartWithoutOpen) {
    EXPECT_FALSE(camera.start());
}

TEST_F(CameraCaptureTest, CannotStopWithoutStart) {
    EXPECT_TRUE(camera.open(config));
    EXPECT_FALSE(camera.stop());
}

TEST_F(CameraCaptureTest, CaptureFrameWhenStarted) {
    camera.open(config);
    camera.start();

    Frame f = camera.captureFrame();
    EXPECT_EQ(f.width, 1280);
    EXPECT_EQ(f.height, 720);
    EXPECT_GT(f.data.size(), 0u);
    EXPECT_EQ(camera.frames_captured, 1);
}

TEST_F(CameraCaptureTest, CaptureFrameWhenNotStarted) {
    Frame f = camera.captureFrame();
    EXPECT_EQ(f.width, 0);
    EXPECT_EQ(f.data.size(), 0u);
}

TEST_F(CameraCaptureTest, RestartAfterStop) {
    camera.open(config);
    camera.start();
    camera.stop();
    EXPECT_TRUE(camera.start());
    EXPECT_EQ(camera.state, MockCameraCapture::STARTED);
}

// Audio capture tests
TEST(AudioCaptureTest, Lifecycle) {
    MockAudioCapture audio;
    AudioConfig config;
    config.device = "hw:0,0";

    EXPECT_TRUE(audio.open(config));
    EXPECT_TRUE(audio.start());

    AudioFrame f = audio.captureFrame();
    EXPECT_GT(f.data.size(), 0u);
    EXPECT_EQ(f.sample_rate, 48000);

    EXPECT_TRUE(audio.stop());
}

// P-005: Mock encoder lifecycle
class EncoderTest : public ::testing::Test {
protected:
    MockEncoder encoder;
    EncoderConfig config;

    void SetUp() override {
        config.width = 1920;
        config.height = 1080;
        config.fps = 30;
        config.bitrate = 4000000;
    }
};

TEST_F(EncoderTest, P005_InitEncodeFlush) {
    EXPECT_TRUE(encoder.init(config));
    EXPECT_TRUE(encoder.initialized);

    Frame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.data.resize(1920 * 1080 * 3 / 2);
    frame.timestamp_us = 0;

    EncodedPacket pkt = encoder.encode(frame);
    EXPECT_GT(pkt.data.size(), 0u);
    EXPECT_TRUE(pkt.is_keyframe);  // First frame is keyframe
    EXPECT_EQ(pkt.pts, 0);

    encoder.flush();
    EXPECT_TRUE(encoder.flushed);
}

TEST_F(EncoderTest, CannotInitTwice) {
    EXPECT_TRUE(encoder.init(config));
    EXPECT_FALSE(encoder.init(config));
}

TEST_F(EncoderTest, EncodeWithoutInit) {
    Frame frame;
    frame.data.resize(100);
    EncodedPacket pkt = encoder.encode(frame);
    EXPECT_EQ(pkt.data.size(), 0u);
}

TEST_F(EncoderTest, KeyframeInterval) {
    encoder.init(config);
    encoder.keyframe_interval = 5;

    Frame frame;
    frame.data.resize(1920 * 1080);

    for (int i = 0; i < 10; i++) {
        frame.timestamp_us = i * 33333;
        EncodedPacket pkt = encoder.encode(frame);
        if (i % 5 == 0) {
            EXPECT_TRUE(pkt.is_keyframe) << "Frame " << i << " should be keyframe";
        } else {
            EXPECT_FALSE(pkt.is_keyframe) << "Frame " << i << " should not be keyframe";
        }
    }
}

// P-006: Mock streamer lifecycle
class StreamerTest : public ::testing::Test {
protected:
    MockStreamer streamer;
};

TEST_F(StreamerTest, P006_ConnectSendDisconnect) {
    EXPECT_TRUE(streamer.connect("http://localhost:3000", "stream-key-123"));
    EXPECT_TRUE(streamer.connected);
    EXPECT_EQ(streamer.last_url, "http://localhost:3000");
    EXPECT_EQ(streamer.last_stream_key, "stream-key-123");

    EncodedPacket pkt;
    pkt.data.resize(1000);
    EXPECT_TRUE(streamer.sendPacket(pkt));
    EXPECT_EQ(streamer.packets_sent, 1);

    streamer.disconnect();
    EXPECT_FALSE(streamer.connected);
}

TEST_F(StreamerTest, CannotConnectTwice) {
    EXPECT_TRUE(streamer.connect("http://localhost:3000", "key"));
    EXPECT_FALSE(streamer.connect("http://localhost:3000", "key"));
}

TEST_F(StreamerTest, SendWithoutConnect) {
    EncodedPacket pkt;
    pkt.data.resize(100);
    EXPECT_FALSE(streamer.sendPacket(pkt));
}

TEST_F(StreamerTest, ConnectionFailure) {
    streamer.fail_connect = true;
    EXPECT_FALSE(streamer.connect("http://localhost:3000", "key"));
    EXPECT_FALSE(streamer.connected);
}

TEST_F(StreamerTest, SendFailure) {
    streamer.connect("http://localhost:3000", "key");
    streamer.fail_send = true;

    EncodedPacket pkt;
    pkt.data.resize(100);
    EXPECT_FALSE(streamer.sendPacket(pkt));
}

TEST_F(StreamerTest, ReconnectAfterDisconnect) {
    streamer.connect("http://localhost:3000", "key");
    streamer.disconnect();
    EXPECT_TRUE(streamer.connect("http://localhost:3000", "key2"));
    EXPECT_EQ(streamer.last_stream_key, "key2");
}

// Full pipeline mock test
TEST(PipelineTest, CaptureEncodeStream) {
    MockCameraCapture camera;
    MockEncoder encoder;
    MockStreamer streamer;

    CaptureConfig cap_config;
    cap_config.width = 1280;
    cap_config.height = 720;
    cap_config.fps = 30;

    EncoderConfig enc_config;
    enc_config.width = 1280;
    enc_config.height = 720;
    enc_config.fps = 30;

    ASSERT_TRUE(camera.open(cap_config));
    ASSERT_TRUE(camera.start());
    ASSERT_TRUE(encoder.init(enc_config));
    ASSERT_TRUE(streamer.connect("http://localhost:3000", "test-key"));

    // Simulate 10 frames through pipeline
    for (int i = 0; i < 10; i++) {
        Frame frame = camera.captureFrame();
        ASSERT_GT(frame.data.size(), 0u);

        EncodedPacket pkt = encoder.encode(frame);
        ASSERT_GT(pkt.data.size(), 0u);

        ASSERT_TRUE(streamer.sendPacket(pkt));
    }

    EXPECT_EQ(camera.frames_captured, 10);
    EXPECT_EQ(encoder.frames_encoded, 10);
    EXPECT_EQ(streamer.packets_sent, 10);

    camera.stop();
    encoder.flush();
    streamer.disconnect();
}
