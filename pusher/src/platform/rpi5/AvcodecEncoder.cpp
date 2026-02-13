#include "platform/rpi5/AvcodecEncoder.h"

#include <iostream>
#include <cstring>

namespace reallive {

AvcodecEncoder::AvcodecEncoder() = default;

AvcodecEncoder::~AvcodecEncoder() {
    if (avPacket_) av_packet_free(&avPacket_);
    if (avFrame_) av_frame_free(&avFrame_);
    if (ctx_) avcodec_free_context(&ctx_);
}

bool AvcodecEncoder::init(const EncoderConfig& config) {
    config_ = config;

    // Use libx264 software encoder (no hardware encoder on Pi 5)
    codec_ = avcodec_find_encoder_by_name("libx264");
    if (!codec_) {
        // Last resort: find any H.264 encoder
        codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (codec_) {
            encoderName_ = codec_->name;
        } else {
            std::cerr << "[AvcodecEncoder] No H.264 encoder found" << std::endl;
            return false;
        }
    } else {
        encoderName_ = "libx264";
        std::cout << "[AvcodecEncoder] Using encoder: " << encoderName_ << std::endl;
    }

    ctx_ = avcodec_alloc_context3(codec_);
    if (!ctx_) {
        std::cerr << "[AvcodecEncoder] Failed to allocate codec context" << std::endl;
        return false;
    }

    ctx_->width = config.width;
    ctx_->height = config.height;
    ctx_->time_base = {1, config.fps};
    ctx_->framerate = {config.fps, 1};
    ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx_->bit_rate = config.bitrate;
    ctx_->gop_size = config.gopSize;
    ctx_->max_b_frames = 0;  // no B-frames for low latency

    // Set profile
    if (config.profile == "baseline") {
        ctx_->profile = FF_PROFILE_H264_BASELINE;
    } else if (config.profile == "high") {
        ctx_->profile = FF_PROFILE_H264_HIGH;
    } else {
        ctx_->profile = FF_PROFILE_H264_MAIN;
    }

    // Put SPS/PPS in extradata for the muxer
    ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Low-latency tuning for libx264
    if (encoderName_ == "libx264") {
        av_opt_set(ctx_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(ctx_->priv_data, "tune", "zerolatency", 0);
    }

    int ret = avcodec_open2(ctx_, codec_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[AvcodecEncoder] Failed to open encoder " << encoderName_
                  << ": " << errbuf << std::endl;
        avcodec_free_context(&ctx_);
        return false;
    }

    std::cout << "[AvcodecEncoder] Opened encoder: " << encoderName_
              << " (" << ctx_->width << "x" << ctx_->height
              << " @" << config.fps << "fps, " << config.bitrate / 1000 << " kbps)" << std::endl;

    // Allocate frame and packet
    avFrame_ = av_frame_alloc();
    avFrame_->format = ctx_->pix_fmt;
    avFrame_->width = ctx_->width;
    avFrame_->height = ctx_->height;

    ret = av_frame_get_buffer(avFrame_, 0);
    if (ret < 0) {
        std::cerr << "[AvcodecEncoder] Failed to allocate frame buffer" << std::endl;
        return false;
    }

    avPacket_ = av_packet_alloc();

    initialized_ = true;
    frameCount_ = 0;
    return true;
}

EncodedPacket AvcodecEncoder::encode(const Frame& frame) {
    EncodedPacket result;
    if (!initialized_ || !ctx_) return result;

    int ret = av_frame_make_writable(avFrame_);
    if (ret < 0) return result;

    // Convert NV12 input to YUV420P expected by the encoder.
    // NV12: Y plane (width*height) + interleaved UV plane (width*height/2)
    // YUV420P: Y plane + U plane (width/2 * height/2) + V plane (width/2 * height/2)
    int w = ctx_->width;
    int h = ctx_->height;
    const uint8_t* src = frame.data.data();
    size_t ySize = w * h;
    size_t uvSize = ySize / 2;

    if (frame.data.size() < ySize + uvSize) {
        return result;  // not enough data
    }

    // Copy Y plane
    const uint8_t* srcY = src;
    for (int row = 0; row < h; row++) {
        std::memcpy(avFrame_->data[0] + row * avFrame_->linesize[0],
                    srcY + row * w, w);
    }

    // De-interleave UV plane: NV12 UVUVUV... → U plane + V plane
    const uint8_t* srcUV = src + ySize;
    int uvH = h / 2;
    int uvW = w / 2;
    for (int row = 0; row < uvH; row++) {
        const uint8_t* uvRow = srcUV + row * w;  // NV12 UV row stride = width
        uint8_t* uRow = avFrame_->data[1] + row * avFrame_->linesize[1];
        uint8_t* vRow = avFrame_->data[2] + row * avFrame_->linesize[2];
        for (int col = 0; col < uvW; col++) {
            uRow[col] = uvRow[col * 2];
            vRow[col] = uvRow[col * 2 + 1];
        }
    }

    avFrame_->pts = frameCount_++;

    // Send frame to encoder
    ret = avcodec_send_frame(ctx_, avFrame_);
    if (ret < 0) {
        return result;
    }

    // Receive encoded packet
    ret = avcodec_receive_packet(ctx_, avPacket_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return result;  // no output yet
    }
    if (ret < 0) {
        return result;
    }

    // Copy to our EncodedPacket
    result.data.assign(avPacket_->data, avPacket_->data + avPacket_->size);

    // Convert pts from encoder timebase to microseconds
    result.pts = av_rescale_q(avPacket_->pts, ctx_->time_base, {1, 1000000});
    result.dts = av_rescale_q(avPacket_->dts, ctx_->time_base, {1, 1000000});
    result.isKeyframe = (avPacket_->flags & AV_PKT_FLAG_KEY) != 0;

    av_packet_unref(avPacket_);
    return result;
}

void AvcodecEncoder::flush() {
    if (!initialized_ || !ctx_) return;
    avcodec_send_frame(ctx_, nullptr);  // flush
    // Drain remaining packets
    while (avcodec_receive_packet(ctx_, avPacket_) == 0) {
        av_packet_unref(avPacket_);
    }
}

std::string AvcodecEncoder::getName() const {
    return "AvcodecEncoder (" + encoderName_ + ")";
}

const uint8_t* AvcodecEncoder::getExtraData() const {
    return ctx_ ? ctx_->extradata : nullptr;
}

int AvcodecEncoder::getExtraDataSize() const {
    return ctx_ ? ctx_->extradata_size : 0;
}

} // namespace reallive
