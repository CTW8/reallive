/**
 * Pusher Configuration Parser Tests
 *
 * Tests JSON config file parsing for the pusher component.
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdlib>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Configuration structure matching pusher's expected config
struct PusherConfig {
    std::string server_url;
    std::string stream_key;
    std::string username;
    std::string password;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 4000000;  // 4 Mbps
    std::string camera_device = "/dev/video0";
    std::string audio_device = "default";
    bool enable_audio = true;

    static PusherConfig fromJson(const json& j) {
        PusherConfig config;
        config.server_url = j.at("server_url").get<std::string>();
        config.stream_key = j.at("stream_key").get<std::string>();

        if (j.contains("username")) config.username = j["username"].get<std::string>();
        if (j.contains("password")) config.password = j["password"].get<std::string>();
        if (j.contains("width")) config.width = j["width"].get<int>();
        if (j.contains("height")) config.height = j["height"].get<int>();
        if (j.contains("fps")) config.fps = j["fps"].get<int>();
        if (j.contains("bitrate")) config.bitrate = j["bitrate"].get<int>();
        if (j.contains("camera_device")) config.camera_device = j["camera_device"].get<std::string>();
        if (j.contains("audio_device")) config.audio_device = j["audio_device"].get<std::string>();
        if (j.contains("enable_audio")) config.enable_audio = j["enable_audio"].get<bool>();

        return config;
    }

    static PusherConfig fromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Cannot open config file: " + path);
        }
        json j = json::parse(f);
        return fromJson(j);
    }
};

class ConfigParserTest : public ::testing::Test {
protected:
    std::string temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / ("pusher_test_" + std::to_string(getpid()));
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    std::string writeConfigFile(const std::string& content, const std::string& name = "config.json") {
        std::string path = temp_dir + "/" + name;
        std::ofstream f(path);
        f << content;
        f.close();
        return path;
    }
};

// P-001: Parse valid JSON config file
TEST_F(ConfigParserTest, P001_ValidConfig) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000",
        "stream_key": "abc-123-def-456",
        "username": "testuser",
        "password": "testpass",
        "width": 1280,
        "height": 720,
        "fps": 25,
        "bitrate": 2000000,
        "camera_device": "/dev/video1",
        "audio_device": "hw:0,0",
        "enable_audio": false
    })");

    PusherConfig config = PusherConfig::fromFile(path);

    EXPECT_EQ(config.server_url, "http://localhost:3000");
    EXPECT_EQ(config.stream_key, "abc-123-def-456");
    EXPECT_EQ(config.username, "testuser");
    EXPECT_EQ(config.password, "testpass");
    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_EQ(config.fps, 25);
    EXPECT_EQ(config.bitrate, 2000000);
    EXPECT_EQ(config.camera_device, "/dev/video1");
    EXPECT_EQ(config.audio_device, "hw:0,0");
    EXPECT_FALSE(config.enable_audio);
}

// P-002: Parse config with missing optional fields (defaults used)
TEST_F(ConfigParserTest, P002_MissingOptionalFields) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000",
        "stream_key": "abc-123"
    })");

    PusherConfig config = PusherConfig::fromFile(path);

    EXPECT_EQ(config.server_url, "http://localhost:3000");
    EXPECT_EQ(config.stream_key, "abc-123");
    // Defaults
    EXPECT_EQ(config.width, 1920);
    EXPECT_EQ(config.height, 1080);
    EXPECT_EQ(config.fps, 30);
    EXPECT_EQ(config.bitrate, 4000000);
    EXPECT_EQ(config.camera_device, "/dev/video0");
    EXPECT_EQ(config.audio_device, "default");
    EXPECT_TRUE(config.enable_audio);
}

// P-002b: Missing required field throws
TEST_F(ConfigParserTest, P002b_MissingRequiredField) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000"
    })");

    EXPECT_THROW(PusherConfig::fromFile(path), json::out_of_range);
}

// P-003: Invalid JSON throws parse error
TEST_F(ConfigParserTest, P003_InvalidJson) {
    std::string path = writeConfigFile("{ this is not valid json }");
    EXPECT_THROW(PusherConfig::fromFile(path), json::parse_error);
}

// P-003b: Empty file throws parse error
TEST_F(ConfigParserTest, P003b_EmptyFile) {
    std::string path = writeConfigFile("");
    EXPECT_THROW(PusherConfig::fromFile(path), json::parse_error);
}

// File not found
TEST_F(ConfigParserTest, FileNotFound) {
    EXPECT_THROW(PusherConfig::fromFile("/nonexistent/path/config.json"), std::runtime_error);
}

// Type mismatch in config values
TEST_F(ConfigParserTest, TypeMismatch) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000",
        "stream_key": "abc",
        "width": "not_a_number"
    })");

    EXPECT_THROW(PusherConfig::fromFile(path), json::type_error);
}

// Validate resolution ranges
TEST_F(ConfigParserTest, ResolutionValues) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000",
        "stream_key": "abc",
        "width": 3840,
        "height": 2160
    })");

    PusherConfig config = PusherConfig::fromFile(path);
    EXPECT_EQ(config.width, 3840);
    EXPECT_EQ(config.height, 2160);
    EXPECT_GT(config.width, 0);
    EXPECT_GT(config.height, 0);
}
