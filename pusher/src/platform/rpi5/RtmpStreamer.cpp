#include "platform/rpi5/RtmpStreamer.h"

#include <iostream>
#include <cstring>
#include <chrono>

namespace reallive {

RtmpStreamer::RtmpStreamer() {
    avformat_network_init();
}

RtmpStreamer::~RtmpStreamer() {
    disconnect();
    avformat_network_deinit();
}

bool RtmpStreamer::connect(const StreamConfig& config) {
    // Build full URL with stream key
    std::string url = config.url;
    if (!config.streamKey.empty()) {
        if (url.back() != '/') url += '/';
        url += config.streamKey;
    }

    // Allocate output context for FLV (RTMP uses FLV container)
    int ret = avformat_alloc_output_context2(&formatCtx_, nullptr, "flv", url.c_str());
    if (ret < 0 || !formatCtx_) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RtmpStreamer] Failed to allocate output context: " << errbuf << std::endl;
        return false;
    }
    formatCtx_->max_interleave_delta = 0;
    formatCtx_->flags |= AVFMT_FLAG_FLUSH_PACKETS;

    // Add video stream (H.264)
    AVStream* videoStream = avformat_new_stream(formatCtx_, nullptr);
    if (!videoStream) {
        std::cerr << "[RtmpStreamer] Failed to create video stream" << std::endl;
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }
    videoStreamIdx_ = videoStream->index;
    videoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    videoStream->codecpar->codec_id = AV_CODEC_ID_H264;
    videoStream->time_base = {1, 1000}; // millisecond timebase for FLV

    // Set video dimensions and extradata (SPS/PPS) from encoder
    if (config.videoWidth > 0 && config.videoHeight > 0) {
        videoStream->codecpar->width = config.videoWidth;
        videoStream->codecpar->height = config.videoHeight;
    }
    if (config.videoExtraData && config.videoExtraDataSize > 0) {
        videoStream->codecpar->extradata = static_cast<uint8_t*>(
            av_mallocz(config.videoExtraDataSize + AV_INPUT_BUFFER_PADDING_SIZE));
        memcpy(videoStream->codecpar->extradata,
               config.videoExtraData, config.videoExtraDataSize);
        videoStream->codecpar->extradata_size = config.videoExtraDataSize;
    }

    if (config.enableAudio) {
        AVStream* audioStream = avformat_new_stream(formatCtx_, nullptr);
        if (!audioStream) {
            std::cerr << "[RtmpStreamer] Failed to create audio stream" << std::endl;
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }
        audioStreamIdx_ = audioStream->index;
        audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audioStream->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        audioStream->codecpar->sample_rate = 44100;
        av_channel_layout_default(&audioStream->codecpar->ch_layout, 1);
        audioStream->codecpar->format = AV_SAMPLE_FMT_S16;
        audioStream->time_base = {1, 1000};
    } else {
        audioStreamIdx_ = -1;
    }

    // Set low-latency options for FLV/RTMP output
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "flvflags", "no_duration_filesize+no_metadata", 0);
    av_dict_set(&opts, "flush_packets", "1", 0);
    
    // Open RTMP connection with low-latency flags
    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        AVDictionary* ioOpts = nullptr;
        av_dict_set(&ioOpts, "rtmp_live", "live", 0);
        av_dict_set(&ioOpts, "tcp_nodelay", "1", 0);
        ret = avio_open2(&formatCtx_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, &ioOpts);
        av_dict_free(&ioOpts);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[RtmpStreamer] Failed to open RTMP connection: " << errbuf << std::endl;
            av_dict_free(&opts);
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }
    }

    // Write FLV header with options
    ret = avformat_write_header(formatCtx_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RtmpStreamer] Failed to write header: " << errbuf << std::endl;
        avio_closep(&formatCtx_->pb);
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }

    headerWritten_ = true;
    connected_ = true;
    audioEnabled_ = config.enableAudio;
    videoStartPts_ = -1;
    audioStartPts_ = -1;

    std::cout << "[RtmpStreamer] Connected to: " << url << std::endl;
    return true;
}

bool RtmpStreamer::sendVideoPacket(const EncodedPacket& packet) {
    if (!connected_ || !formatCtx_ || !headerWritten_) return false;
    if (videoStreamIdx_ < 0) return false;
    std::lock_guard<std::mutex> lock(writeMutex_);

    auto sendStart = std::chrono::steady_clock::now();

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt) return false;

    avpkt->data = const_cast<uint8_t*>(packet.data.data());
    avpkt->size = static_cast<int>(packet.data.size());
    avpkt->stream_index = videoStreamIdx_;

    // Rebase PTS relative to first frame
    int64_t pts = packet.pts;
    int64_t dts = packet.dts;
    if (videoStartPts_ < 0) {
        videoStartPts_ = pts;
    }
    pts -= videoStartPts_;
    dts -= videoStartPts_;
    if (dts < 0) dts = 0;

    // Convert from microseconds to milliseconds (FLV timebase)
    AVStream* stream = formatCtx_->streams[videoStreamIdx_];
    avpkt->pts = av_rescale_q(pts, {1, 1000000}, stream->time_base);
    avpkt->dts = av_rescale_q(dts, {1, 1000000}, stream->time_base);
    avpkt->duration = 0;

    if (packet.isKeyframe) {
        avpkt->flags |= AV_PKT_FLAG_KEY;
    }

    int ret = av_interleaved_write_frame(formatCtx_, avpkt);
    av_packet_free(&avpkt);

    auto sendEnd = std::chrono::steady_clock::now();
    auto sendTime = std::chrono::duration_cast<std::chrono::microseconds>(sendEnd - sendStart);

    static uint64_t sendCount = 0;
    static uint64_t totalSendTime = 0;
    sendCount++;
    totalSendTime += sendTime.count();
    
    // Log every 30 frames (approx 1 second at 30fps)
    if (sendCount % 30 == 0) {
        std::cout << "[RTMP Send] Avg: " << (totalSendTime / sendCount / 1000.0) << "ms, "
                  << "Last: " << (sendTime.count() / 1000.0) << "ms, "
                  << "Size: " << packet.data.size() << "B"
                  << (packet.isKeyframe ? " [KEY]" : "") << std::endl;
        totalSendTime = 0;
        sendCount = 0;
    }

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RtmpStreamer] Failed to send video packet: " << errbuf << std::endl;
        connected_ = false;
        return false;
    }

    return true;
}

bool RtmpStreamer::sendAudioPacket(const AudioFrame& frame) {
    if (!audioEnabled_) return true;
    if (!connected_ || !formatCtx_ || !headerWritten_) return false;
    if (audioStreamIdx_ < 0) return false;
    std::lock_guard<std::mutex> lock(writeMutex_);

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt) return false;

    avpkt->data = const_cast<uint8_t*>(frame.data.data());
    avpkt->size = static_cast<int>(frame.data.size());
    avpkt->stream_index = audioStreamIdx_;

    // Rebase PTS relative to first audio frame
    int64_t pts = frame.pts;
    if (audioStartPts_ < 0) {
        audioStartPts_ = pts;
    }
    pts -= audioStartPts_;
    if (pts < 0) pts = 0;

    AVStream* stream = formatCtx_->streams[audioStreamIdx_];
    avpkt->pts = av_rescale_q(pts, {1, 1000000}, stream->time_base);
    avpkt->dts = avpkt->pts;
    avpkt->duration = 0;

    int ret = av_interleaved_write_frame(formatCtx_, avpkt);
    av_packet_free(&avpkt);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[RtmpStreamer] Failed to send audio packet: " << errbuf << std::endl;
        return false;
    }

    return true;
}

void RtmpStreamer::disconnect() {
    if (formatCtx_) {
        if (headerWritten_) {
            av_write_trailer(formatCtx_);
            headerWritten_ = false;
        }

        if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&formatCtx_->pb);
        }

        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
    }

    connected_ = false;
    audioEnabled_ = false;
    videoStreamIdx_ = -1;
    audioStreamIdx_ = -1;
}

bool RtmpStreamer::isConnected() const {
    return connected_;
}

std::string RtmpStreamer::getName() const {
    return "RTMP Streamer (FFmpeg/libavformat)";
}

} // namespace reallive
