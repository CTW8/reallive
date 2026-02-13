/**
 * Puller Storage Tests
 *
 * Tests storage file rotation (R-005), disk space checks (R-006),
 * multiple segment writes, and close/reopen behavior using mock
 * implementations.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <algorithm>

// Use types from the project headers
#include "platform/IStreamReceiver.h"
#include "platform/IDecoder.h"
#include "platform/IStorage.h"

using namespace reallive::puller;

// ============================================================
// Mock Storage with rotation and disk space simulation
// ============================================================

class MockRotatingStorage : public IStorage {
public:
    bool is_open = false;
    std::string current_path;
    StreamInfo current_info;
    int64_t bytes_written = 0;
    int packets_written = 0;
    int files_created = 0;
    int64_t max_file_size_bytes = 500 * 1024 * 1024;  // 500 MB default

    // Disk space simulation
    int64_t available_disk_bytes = -1;  // -1 means unlimited
    bool fail_open = false;

    // History tracking
    std::vector<std::string> opened_paths;
    std::vector<int64_t> file_sizes;

    bool open(const std::string& filepath, const StreamInfo& info) override {
        if (is_open) return false;
        if (fail_open) return false;
        if (available_disk_bytes >= 0 && available_disk_bytes < 1024) {
            return false;  // Not enough disk space to open a new file
        }
        current_path = filepath;
        current_info = info;
        bytes_written = 0;
        is_open = true;
        files_created++;
        opened_paths.push_back(filepath);
        return true;
    }

    bool writePacket(const EncodedPacket& packet) override {
        if (!is_open) return false;
        int64_t pkt_size = static_cast<int64_t>(packet.data.size());
        if (available_disk_bytes >= 0 && pkt_size > available_disk_bytes) {
            return false;  // Disk full
        }
        bytes_written += pkt_size;
        packets_written++;
        if (available_disk_bytes >= 0) {
            available_disk_bytes -= pkt_size;
        }
        return true;
    }

    bool writeFrame(const Frame& /*frame*/) override {
        if (!is_open) return false;
        return true;
    }

    void close() override {
        if (is_open) {
            file_sizes.push_back(bytes_written);
        }
        is_open = false;
    }

    bool needsRotation() const {
        return bytes_written >= max_file_size_bytes;
    }

    int64_t totalBytesAcrossFiles() const {
        int64_t total = 0;
        for (auto sz : file_sizes) total += sz;
        if (is_open) total += bytes_written;
        return total;
    }
};

// Helper to create an encoded packet of a given size
static EncodedPacket makePacket(size_t size, int64_t pts = 0, bool keyframe = false) {
    EncodedPacket pkt;
    pkt.type = MediaType::Video;
    pkt.data.resize(size, 0xAA);
    pkt.pts = pts;
    pkt.dts = pts;
    pkt.isKeyFrame = keyframe;
    return pkt;
}

// Helper to generate a segment file path
static std::string segmentPath(const std::string& base, int index) {
    return base + "/segment_" + std::to_string(index) + ".mp4";
}

// ============================================================
// Test fixture
// ============================================================

class StorageRotationTest : public ::testing::Test {
protected:
    MockRotatingStorage storage;
    StreamInfo info;

    void SetUp() override {
        info.width = 1920;
        info.height = 1080;
        info.fps = 30;
        info.videoCodec = CodecType::H264;
        storage.max_file_size_bytes = 1024 * 1024;  // 1 MB for fast testing
    }
};

// ============================================================
// R-005: Storage file rotation on size limit
// ============================================================

TEST_F(StorageRotationTest, R005_RotatesWhenSizeLimitExceeded) {
    // Simulate a rotation controller: write packets until rotation needed,
    // then close and open a new file.
    const int64_t packet_size = 256 * 1024;  // 256 KB per packet
    const int total_packets = 12;  // 12 * 256 KB = 3 MB -> should cause ~3 rotations

    int segment = 0;
    ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", segment), info));

    for (int i = 0; i < total_packets; i++) {
        EncodedPacket pkt = makePacket(packet_size, i * 33333, i % 4 == 0);
        ASSERT_TRUE(storage.writePacket(pkt));

        if (storage.needsRotation()) {
            storage.close();
            segment++;
            ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", segment), info));
        }
    }
    storage.close();

    // We should have created more than 1 file
    EXPECT_GT(storage.files_created, 1);
    // Each file (except possibly the last) should have been at or above the limit
    for (size_t i = 0; i < storage.file_sizes.size() - 1; i++) {
        EXPECT_GE(storage.file_sizes[i], storage.max_file_size_bytes);
    }
    // Total data across all files should match
    EXPECT_EQ(storage.totalBytesAcrossFiles(),
              static_cast<int64_t>(total_packets) * packet_size);
    EXPECT_EQ(storage.packets_written, total_packets);
}

TEST_F(StorageRotationTest, R005_NoRotationWhenUnderLimit) {
    ASSERT_TRUE(storage.open("/tmp/rec/small.mp4", info));

    // Write small packets that stay under the limit
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(storage.writePacket(makePacket(100, i * 33333)));
    }

    EXPECT_FALSE(storage.needsRotation());
    storage.close();

    EXPECT_EQ(storage.files_created, 1);
    EXPECT_EQ(storage.file_sizes.size(), 1u);
    EXPECT_EQ(storage.file_sizes[0], 300);
}

TEST_F(StorageRotationTest, R005_RotationResetsWriteCounter) {
    ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", 0), info));

    // Write enough to trigger rotation
    ASSERT_TRUE(storage.writePacket(makePacket(storage.max_file_size_bytes)));
    EXPECT_TRUE(storage.needsRotation());
    EXPECT_EQ(storage.bytes_written, storage.max_file_size_bytes);

    // Rotate
    storage.close();
    ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", 1), info));

    // After rotation, bytes_written should be reset
    EXPECT_EQ(storage.bytes_written, 0);
    EXPECT_FALSE(storage.needsRotation());
}

TEST_F(StorageRotationTest, R005_SegmentPathsAreTracked) {
    storage.max_file_size_bytes = 500;  // Tiny limit

    int segment = 0;
    ASSERT_TRUE(storage.open(segmentPath("/recordings", segment), info));

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(storage.writePacket(makePacket(200, i * 33333)));
        if (storage.needsRotation()) {
            storage.close();
            segment++;
            ASSERT_TRUE(storage.open(segmentPath("/recordings", segment), info));
        }
    }
    storage.close();

    // Verify all paths are unique and sequential
    for (int i = 0; i < static_cast<int>(storage.opened_paths.size()); i++) {
        EXPECT_EQ(storage.opened_paths[i], segmentPath("/recordings", i));
    }
}

// ============================================================
// R-006: Storage disk space check
// ============================================================

class StorageDiskSpaceTest : public ::testing::Test {
protected:
    MockRotatingStorage storage;
    StreamInfo info;

    void SetUp() override {
        info.width = 1280;
        info.height = 720;
        info.fps = 30;
        info.videoCodec = CodecType::H264;
    }
};

TEST_F(StorageDiskSpaceTest, R006_WriteFailsWhenDiskFull) {
    storage.available_disk_bytes = 5000;  // Only 5 KB free

    ASSERT_TRUE(storage.open("/tmp/rec/segment_0.mp4", info));

    // Small write succeeds
    EXPECT_TRUE(storage.writePacket(makePacket(1000)));
    EXPECT_EQ(storage.available_disk_bytes, 4000);

    // Large write exceeds remaining space
    EXPECT_FALSE(storage.writePacket(makePacket(5000)));
    EXPECT_EQ(storage.packets_written, 1);  // Only first packet written

    storage.close();
}

TEST_F(StorageDiskSpaceTest, R006_OpenFailsWhenNoDiskSpace) {
    storage.available_disk_bytes = 500;  // Less than 1 KB minimum

    EXPECT_FALSE(storage.open("/tmp/rec/segment_0.mp4", info));
    EXPECT_FALSE(storage.is_open);
    EXPECT_EQ(storage.files_created, 0);
}

TEST_F(StorageDiskSpaceTest, R006_WritesUntilDiskExhausted) {
    storage.available_disk_bytes = 10000;  // 10 KB

    ASSERT_TRUE(storage.open("/tmp/rec/segment_0.mp4", info));

    int successful_writes = 0;
    for (int i = 0; i < 20; i++) {
        if (storage.writePacket(makePacket(1000))) {
            successful_writes++;
        } else {
            break;
        }
    }

    // Should succeed exactly 10 times (10000 / 1000)
    EXPECT_EQ(successful_writes, 10);
    EXPECT_EQ(storage.available_disk_bytes, 0);

    storage.close();
}

TEST_F(StorageDiskSpaceTest, R006_UnlimitedDiskByDefault) {
    // available_disk_bytes == -1 means unlimited
    EXPECT_EQ(storage.available_disk_bytes, -1);

    ASSERT_TRUE(storage.open("/tmp/rec/segment_0.mp4", info));

    // Large writes should succeed
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(storage.writePacket(makePacket(100000)));
    }

    EXPECT_EQ(storage.packets_written, 100);
    storage.close();
}

// ============================================================
// Multiple segment write tests
// ============================================================

class StorageMultiSegmentTest : public ::testing::Test {
protected:
    MockRotatingStorage storage;
    StreamInfo info;

    void SetUp() override {
        info.width = 1920;
        info.height = 1080;
        info.fps = 30;
        info.videoCodec = CodecType::H264;
        storage.max_file_size_bytes = 2048;  // 2 KB for fast testing
    }
};

TEST_F(StorageMultiSegmentTest, WriteVariousSizePackets) {
    ASSERT_TRUE(storage.open("/tmp/rec/seg_0.mp4", info));

    size_t sizes[] = {100, 500, 1024, 50, 2048, 300};
    int segment = 0;

    for (auto sz : sizes) {
        EncodedPacket pkt = makePacket(sz);
        ASSERT_TRUE(storage.writePacket(pkt));
        if (storage.needsRotation()) {
            storage.close();
            segment++;
            ASSERT_TRUE(storage.open("/tmp/rec/seg_" + std::to_string(segment) + ".mp4", info));
        }
    }
    storage.close();

    // Verify total data matches sum of all packet sizes
    int64_t expected_total = 0;
    for (auto sz : sizes) expected_total += static_cast<int64_t>(sz);
    EXPECT_EQ(storage.totalBytesAcrossFiles(), expected_total);
}

TEST_F(StorageMultiSegmentTest, ManySmallPacketsAcrossSegments) {
    const int num_packets = 100;
    const size_t pkt_size = 50;
    int segment = 0;

    ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", segment), info));

    for (int i = 0; i < num_packets; i++) {
        ASSERT_TRUE(storage.writePacket(makePacket(pkt_size, i * 33333, i % 30 == 0)));
        if (storage.needsRotation()) {
            storage.close();
            segment++;
            ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", segment), info));
        }
    }
    storage.close();

    EXPECT_EQ(storage.packets_written, num_packets);
    EXPECT_EQ(storage.totalBytesAcrossFiles(),
              static_cast<int64_t>(num_packets) * static_cast<int64_t>(pkt_size));
}

TEST_F(StorageMultiSegmentTest, SingleLargePacketTriggersRotation) {
    ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", 0), info));

    // One packet exceeds file size limit
    ASSERT_TRUE(storage.writePacket(makePacket(3000)));
    EXPECT_TRUE(storage.needsRotation());

    storage.close();
    ASSERT_TRUE(storage.open(segmentPath("/tmp/rec", 1), info));

    // Next small packet fits
    ASSERT_TRUE(storage.writePacket(makePacket(100)));
    EXPECT_FALSE(storage.needsRotation());

    storage.close();
    EXPECT_EQ(storage.files_created, 2);
}

// ============================================================
// Close and reopen tests
// ============================================================

class StorageCloseReopenTest : public ::testing::Test {
protected:
    MockRotatingStorage storage;
    StreamInfo info;

    void SetUp() override {
        info.width = 1920;
        info.height = 1080;
        info.fps = 30;
        info.videoCodec = CodecType::H264;
    }
};

TEST_F(StorageCloseReopenTest, CloseAndReopenSamePath) {
    ASSERT_TRUE(storage.open("/tmp/rec/output.mp4", info));
    ASSERT_TRUE(storage.writePacket(makePacket(1000)));
    EXPECT_EQ(storage.bytes_written, 1000);

    storage.close();
    EXPECT_FALSE(storage.is_open);
    EXPECT_EQ(storage.file_sizes.size(), 1u);

    // Reopen same path
    ASSERT_TRUE(storage.open("/tmp/rec/output.mp4", info));
    EXPECT_TRUE(storage.is_open);
    EXPECT_EQ(storage.bytes_written, 0);  // Reset on open

    ASSERT_TRUE(storage.writePacket(makePacket(500)));
    EXPECT_EQ(storage.bytes_written, 500);

    storage.close();
    EXPECT_EQ(storage.file_sizes.size(), 2u);
}

TEST_F(StorageCloseReopenTest, CannotWriteAfterClose) {
    ASSERT_TRUE(storage.open("/tmp/rec/output.mp4", info));
    storage.close();

    EXPECT_FALSE(storage.writePacket(makePacket(100)));
    EXPECT_EQ(storage.packets_written, 0);
}

TEST_F(StorageCloseReopenTest, CannotOpenWhileAlreadyOpen) {
    ASSERT_TRUE(storage.open("/tmp/rec/a.mp4", info));
    EXPECT_FALSE(storage.open("/tmp/rec/b.mp4", info));
    EXPECT_EQ(storage.current_path, "/tmp/rec/a.mp4");
    storage.close();
}

TEST_F(StorageCloseReopenTest, MultipleCloseCallsAreSafe) {
    ASSERT_TRUE(storage.open("/tmp/rec/output.mp4", info));
    ASSERT_TRUE(storage.writePacket(makePacket(100)));

    storage.close();
    storage.close();  // Should not crash or add duplicate file_sizes

    // Only one entry in file_sizes (second close does nothing since is_open is false)
    EXPECT_EQ(storage.file_sizes.size(), 1u);
}

TEST_F(StorageCloseReopenTest, ReopenWithDifferentStreamInfo) {
    info.width = 1920;
    info.height = 1080;
    ASSERT_TRUE(storage.open("/tmp/rec/hd.mp4", info));
    EXPECT_EQ(storage.current_info.width, 1920);
    storage.close();

    // Reopen with different resolution
    StreamInfo info2;
    info2.width = 1280;
    info2.height = 720;
    info2.fps = 25;
    info2.videoCodec = CodecType::H265;
    ASSERT_TRUE(storage.open("/tmp/rec/720p.mp4", info2));
    EXPECT_EQ(storage.current_info.width, 1280);
    EXPECT_EQ(storage.current_info.height, 720);
    storage.close();
}

TEST_F(StorageCloseReopenTest, StateFullyResetOnReopen) {
    storage.max_file_size_bytes = 1000;

    ASSERT_TRUE(storage.open("/tmp/rec/seg_0.mp4", info));
    ASSERT_TRUE(storage.writePacket(makePacket(1000)));
    EXPECT_TRUE(storage.needsRotation());

    storage.close();
    ASSERT_TRUE(storage.open("/tmp/rec/seg_1.mp4", info));

    // After reopen, should not need rotation
    EXPECT_FALSE(storage.needsRotation());
    EXPECT_EQ(storage.bytes_written, 0);
    EXPECT_EQ(storage.current_path, "/tmp/rec/seg_1.mp4");

    storage.close();
}

// ============================================================
// Edge cases
// ============================================================

TEST(StorageEdgeCases, OpenFailure) {
    MockRotatingStorage storage;
    storage.fail_open = true;
    StreamInfo info;
    info.width = 1920;
    info.height = 1080;

    EXPECT_FALSE(storage.open("/tmp/rec/output.mp4", info));
    EXPECT_FALSE(storage.is_open);
    EXPECT_EQ(storage.files_created, 0);
}

TEST(StorageEdgeCases, WriteEmptyPacket) {
    MockRotatingStorage storage;
    StreamInfo info;
    info.width = 1920;
    info.height = 1080;

    ASSERT_TRUE(storage.open("/tmp/rec/output.mp4", info));

    EncodedPacket empty_pkt;
    empty_pkt.type = MediaType::Video;
    // data is empty -> size 0
    EXPECT_TRUE(storage.writePacket(empty_pkt));
    EXPECT_EQ(storage.bytes_written, 0);
    EXPECT_EQ(storage.packets_written, 1);

    storage.close();
}

TEST(StorageEdgeCases, ZeroSizeFileLimit) {
    MockRotatingStorage storage;
    storage.max_file_size_bytes = 0;
    StreamInfo info;

    ASSERT_TRUE(storage.open("/tmp/rec/output.mp4", info));

    // Even empty data triggers rotation since bytes_written (0) >= max (0)
    EXPECT_TRUE(storage.needsRotation());

    storage.close();
}
