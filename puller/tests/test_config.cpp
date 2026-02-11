/**
 * Puller Configuration Parser Tests
 *
 * Tests JSON config file parsing for the puller component.
 */

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

struct PullerConfig {
    std::string server_url;
    std::string stream_key;
    std::string username;
    std::string password;
    std::string output_dir = "./recordings";
    std::string output_format = "mp4";
    int64_t max_file_size_mb = 500;      // Max file size before rotation (MB)
    int max_file_duration_sec = 3600;     // Max file duration (seconds)
    bool enable_preview = false;
    int preview_width = 640;
    int preview_height = 480;

    static PullerConfig fromJson(const json& j) {
        PullerConfig config;
        config.server_url = j.at("server_url").get<std::string>();
        config.stream_key = j.at("stream_key").get<std::string>();

        if (j.contains("username")) config.username = j["username"].get<std::string>();
        if (j.contains("password")) config.password = j["password"].get<std::string>();
        if (j.contains("output_dir")) config.output_dir = j["output_dir"].get<std::string>();
        if (j.contains("output_format")) config.output_format = j["output_format"].get<std::string>();
        if (j.contains("max_file_size_mb")) config.max_file_size_mb = j["max_file_size_mb"].get<int64_t>();
        if (j.contains("max_file_duration_sec")) config.max_file_duration_sec = j["max_file_duration_sec"].get<int>();
        if (j.contains("enable_preview")) config.enable_preview = j["enable_preview"].get<bool>();
        if (j.contains("preview_width")) config.preview_width = j["preview_width"].get<int>();
        if (j.contains("preview_height")) config.preview_height = j["preview_height"].get<int>();

        return config;
    }

    static PullerConfig fromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Cannot open config file: " + path);
        }
        json j = json::parse(f);
        return fromJson(j);
    }
};

class PullerConfigTest : public ::testing::Test {
protected:
    std::string temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / ("puller_test_" + std::to_string(getpid()));
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    std::string writeConfigFile(const std::string& content) {
        std::string path = temp_dir + "/config.json";
        std::ofstream f(path);
        f << content;
        f.close();
        return path;
    }
};

// R-001: Parse valid JSON config file
TEST_F(PullerConfigTest, R001_ValidConfig) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://192.168.1.100:3000",
        "stream_key": "cam-abc-123",
        "username": "admin",
        "password": "secret",
        "output_dir": "/mnt/storage/recordings",
        "output_format": "mp4",
        "max_file_size_mb": 1024,
        "max_file_duration_sec": 1800,
        "enable_preview": true,
        "preview_width": 320,
        "preview_height": 240
    })");

    PullerConfig config = PullerConfig::fromFile(path);

    EXPECT_EQ(config.server_url, "http://192.168.1.100:3000");
    EXPECT_EQ(config.stream_key, "cam-abc-123");
    EXPECT_EQ(config.username, "admin");
    EXPECT_EQ(config.password, "secret");
    EXPECT_EQ(config.output_dir, "/mnt/storage/recordings");
    EXPECT_EQ(config.output_format, "mp4");
    EXPECT_EQ(config.max_file_size_mb, 1024);
    EXPECT_EQ(config.max_file_duration_sec, 1800);
    EXPECT_TRUE(config.enable_preview);
    EXPECT_EQ(config.preview_width, 320);
    EXPECT_EQ(config.preview_height, 240);
}

// Defaults used for missing optional fields
TEST_F(PullerConfigTest, DefaultValues) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000",
        "stream_key": "key-1"
    })");

    PullerConfig config = PullerConfig::fromFile(path);

    EXPECT_EQ(config.output_dir, "./recordings");
    EXPECT_EQ(config.output_format, "mp4");
    EXPECT_EQ(config.max_file_size_mb, 500);
    EXPECT_EQ(config.max_file_duration_sec, 3600);
    EXPECT_FALSE(config.enable_preview);
}

// Missing required fields
TEST_F(PullerConfigTest, MissingRequired) {
    std::string path = writeConfigFile(R"({
        "server_url": "http://localhost:3000"
    })");

    EXPECT_THROW(PullerConfig::fromFile(path), json::out_of_range);
}

// Invalid JSON
TEST_F(PullerConfigTest, InvalidJson) {
    std::string path = writeConfigFile("{broken json!!!");
    EXPECT_THROW(PullerConfig::fromFile(path), json::parse_error);
}

// File not found
TEST_F(PullerConfigTest, FileNotFound) {
    EXPECT_THROW(PullerConfig::fromFile("/no/such/file.json"), std::runtime_error);
}
