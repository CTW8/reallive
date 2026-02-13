#pragma once

#include <cstdint>
#include <vector>
#include <chrono>

namespace reallive {

struct PipelineTimestamps {
    int64_t capture_ms = 0;
    int64_t encode_start_ms = 0;
    int64_t encode_end_ms = 0;
};

// Creates H.264 SEI NALUs (Annex B) carrying wall-clock timestamps.
// UUID "RealLiveTimeSEI1" identifies the payload.
class SeiTimestamp {
public:
    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Build an Annex-B SEI NALU and prepend it to |packet|.
    static void inject(std::vector<uint8_t>& packet, const PipelineTimestamps& ts) {
        // --- build raw RBSP (before emulation-prevention) ---
        //  SEI message: type=5 (user_data_unregistered), size=40
        //  Payload: 16-byte UUID + 3x int64 LE = 40 bytes
        uint8_t rbsp[44]; // NAL hdr(1) + type(1) + size(1) + payload(40) + stop(1)
        rbsp[0] = 0x06;   // NAL type = SEI
        rbsp[1] = 0x05;   // payload type = user_data_unregistered
        rbsp[2] = 40;     // payload size

        // UUID "RealLiveTimeSEI1"
        static const uint8_t kUUID[16] = {
            0x52,0x65,0x61,0x6C, 0x4C,0x69,0x76,0x65,
            0x54,0x69,0x6D,0x65, 0x53,0x45,0x49,0x31
        };
        std::memcpy(rbsp + 3, kUUID, 16);

        // 3 timestamps, little-endian
        writeLE64(rbsp + 19, ts.capture_ms);
        writeLE64(rbsp + 27, ts.encode_start_ms);
        writeLE64(rbsp + 35, ts.encode_end_ms);

        rbsp[43] = 0x80;  // RBSP stop bit

        // --- emit Annex-B NALU with emulation-prevention ---
        std::vector<uint8_t> nalu;
        nalu.reserve(52);
        nalu.push_back(0x00); nalu.push_back(0x00);
        nalu.push_back(0x00); nalu.push_back(0x01); // start code

        int zeros = 0;
        for (int i = 0; i < 44; i++) {
            uint8_t b = rbsp[i];
            if (zeros >= 2 && b <= 0x03) {
                nalu.push_back(0x03); // emulation prevention
                zeros = 0;
            }
            nalu.push_back(b);
            zeros = (b == 0x00) ? zeros + 1 : 0;
        }

        // Prepend SEI before existing slice NALUs
        packet.insert(packet.begin(), nalu.begin(), nalu.end());
    }

private:
    static void writeLE64(uint8_t* p, int64_t v) {
        for (int i = 0; i < 8; i++)
            p[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
    }
};

} // namespace reallive
