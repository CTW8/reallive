#include "platform/rpi5/Mp4Storage.h"

#include <iostream>

namespace reallive {
namespace puller {

Mp4Storage::Mp4Storage() = default;

Mp4Storage::~Mp4Storage() {
    close();
}

bool Mp4Storage::open(const std::string& filepath, const StreamInfo& info) {
    // Close any previous context
    close();

    int ret = avformat_alloc_output_context2(&formatCtx_, nullptr, "mp4", filepath.c_str());
    if (ret < 0 || !formatCtx_) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[Mp4Storage] Failed to allocate output context: " << errbuf << std::endl;
        return false;
    }

    // Add video stream
    if (info.videoCodec != CodecType::Unknown) {
        AVStream* vs = avformat_new_stream(formatCtx_, nullptr);
        if (!vs) {
            std::cerr << "[Mp4Storage] Failed to create video stream." << std::endl;
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }

        videoStreamIdx_ = vs->index;
        vs->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        vs->codecpar->codec_id = mapCodecType(info.videoCodec);
        vs->codecpar->width = info.width;
        vs->codecpar->height = info.height;
        vs->codecpar->bit_rate = info.bitrate;
        vs->time_base = {1, 90000}; // common timebase for video

        // Copy extradata (SPS/PPS)
        if (!info.videoExtradata.empty()) {
            vs->codecpar->extradata_size = static_cast<int>(info.videoExtradata.size());
            vs->codecpar->extradata = static_cast<uint8_t*>(
                av_mallocz(info.videoExtradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            memcpy(vs->codecpar->extradata, info.videoExtradata.data(),
                   info.videoExtradata.size());
        }
    }

    // Add audio stream
    if (info.audioCodec != CodecType::Unknown) {
        AVStream* as = avformat_new_stream(formatCtx_, nullptr);
        if (!as) {
            std::cerr << "[Mp4Storage] Failed to create audio stream." << std::endl;
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }

        audioStreamIdx_ = as->index;
        as->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        as->codecpar->codec_id = mapCodecType(info.audioCodec);
        as->codecpar->sample_rate = info.sampleRate;
        av_channel_layout_default(&as->codecpar->ch_layout, info.channels);
        as->time_base = {1, info.sampleRate > 0 ? info.sampleRate : 44100};

        if (!info.audioExtradata.empty()) {
            as->codecpar->extradata_size = static_cast<int>(info.audioExtradata.size());
            as->codecpar->extradata = static_cast<uint8_t*>(
                av_mallocz(info.audioExtradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            memcpy(as->codecpar->extradata, info.audioExtradata.data(),
                   info.audioExtradata.size());
        }
    }

    // Open output file
    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&formatCtx_->pb, filepath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "[Mp4Storage] Failed to open file: " << errbuf << std::endl;
            avformat_free_context(formatCtx_);
            formatCtx_ = nullptr;
            return false;
        }
    }

    // Write header
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);

    ret = avformat_write_header(formatCtx_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[Mp4Storage] Failed to write header: " << errbuf << std::endl;
        avio_closep(&formatCtx_->pb);
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }

    headerWritten_ = true;
    startPts_ = -1;
    std::cout << "[Mp4Storage] Opened: " << filepath << std::endl;
    return true;
}

bool Mp4Storage::writePacket(const EncodedPacket& packet) {
    if (!formatCtx_ || !headerWritten_) return false;

    // Determine target stream index
    int targetIdx = -1;
    if (packet.type == MediaType::Video) {
        targetIdx = videoStreamIdx_;
    } else if (packet.type == MediaType::Audio) {
        targetIdx = audioStreamIdx_;
    }

    if (targetIdx < 0) return false;

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt) return false;

    avpkt->data = const_cast<uint8_t*>(packet.data.data());
    avpkt->size = static_cast<int>(packet.data.size());
    avpkt->stream_index = targetIdx;

    // Convert timestamps from microseconds to output stream timebase
    AVStream* outStream = formatCtx_->streams[targetIdx];
    AVRational srcTb = {packet.timebaseNum, packet.timebaseDen};
    if (srcTb.num == 0 || srcTb.den == 0) {
        srcTb = {1, 1000000}; // default: microseconds
    }

    // Rebase PTS relative to start for clean timestamps
    int64_t pts = packet.pts;
    int64_t dts = packet.dts;

    if (startPts_ < 0 && pts > 0) {
        startPts_ = pts;
    }
    if (startPts_ > 0) {
        pts -= startPts_;
        dts -= startPts_;
        if (dts < 0) dts = 0;
    }

    avpkt->pts = av_rescale_q(pts, {1, 1000000}, outStream->time_base);
    avpkt->dts = av_rescale_q(dts, {1, 1000000}, outStream->time_base);
    avpkt->duration = 0;

    if (packet.isKeyFrame) {
        avpkt->flags |= AV_PKT_FLAG_KEY;
    }

    int ret = av_interleaved_write_frame(formatCtx_, avpkt);
    av_packet_free(&avpkt);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[Mp4Storage] Write frame failed: " << errbuf << std::endl;
        return false;
    }

    return true;
}

bool Mp4Storage::writeFrame(const Frame& /*frame*/) {
    // Writing decoded frames requires re-encoding; not implemented in
    // direct-storage mode. This would be needed for transcoding workflows.
    std::cerr << "[Mp4Storage] writeFrame not supported in direct storage mode." << std::endl;
    return false;
}

void Mp4Storage::close() {
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

    videoStreamIdx_ = -1;
    audioStreamIdx_ = -1;
    startPts_ = -1;
}

AVCodecID Mp4Storage::mapCodecType(CodecType type) {
    switch (type) {
        case CodecType::H264: return AV_CODEC_ID_H264;
        case CodecType::H265: return AV_CODEC_ID_HEVC;
        case CodecType::AAC:  return AV_CODEC_ID_AAC;
        case CodecType::OPUS: return AV_CODEC_ID_OPUS;
        default:              return AV_CODEC_ID_NONE;
    }
}

} // namespace puller
} // namespace reallive
