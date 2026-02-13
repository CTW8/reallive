#include "platform/rpi5/FFmpegReceiver.h"

#include <iostream>

namespace reallive {
namespace puller {

FFmpegReceiver::FFmpegReceiver() {
    // avformat_network_init() is deprecated in newer FFmpeg but still needed
    // for RTMP support in some builds.
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 0, 0)
    av_register_all();
#endif
    avformat_network_init();
}

FFmpegReceiver::~FFmpegReceiver() {
    stop();
    avformat_network_deinit();
}

bool FFmpegReceiver::connect(const std::string& url) {
    url_ = url;

    AVDictionary* opts = nullptr;

    // Set protocol-specific options based on URL scheme
    if (url.find("rtmp://") == 0) {
        av_dict_set(&opts, "rtmp_live", "live", 0);
        av_dict_set(&opts, "timeout", "5000000", 0);
    } else if (url.find("http://") == 0 || url.find("https://") == 0) {
        // HTTP-FLV: reconnect on error, set timeouts
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&opts, "timeout", "5000000", 0);
        av_dict_set(&opts, "rw_timeout", "5000000", 0);
    }

    int ret = avformat_open_input(&formatCtx_, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[FFmpegReceiver] Failed to open input: " << errbuf << std::endl;
        return false;
    }

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[FFmpegReceiver] Failed to find stream info: " << errbuf << std::endl;
        avformat_close_input(&formatCtx_);
        return false;
    }

    // Find video and audio streams
    for (unsigned i = 0; i < formatCtx_->nb_streams; i++) {
        AVCodecParameters* codecpar = formatCtx_->streams[i]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            videoStreamIndex_ = static_cast<int>(i);
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            audioStreamIndex_ = static_cast<int>(i);
        }
    }

    if (videoStreamIndex_ < 0) {
        std::cerr << "[FFmpegReceiver] No video stream found." << std::endl;
        avformat_close_input(&formatCtx_);
        return false;
    }

    connected_ = true;
    std::cout << "[FFmpegReceiver] Connected to: " << url << std::endl;
    std::cout << "[FFmpegReceiver] Video stream index: " << videoStreamIndex_ << std::endl;
    if (audioStreamIndex_ >= 0) {
        std::cout << "[FFmpegReceiver] Audio stream index: " << audioStreamIndex_ << std::endl;
    }

    return true;
}

bool FFmpegReceiver::start() {
    if (!connected_) {
        std::cerr << "[FFmpegReceiver] Not connected." << std::endl;
        return false;
    }
    // Receiving is driven by receivePacket(), no separate start needed.
    return true;
}

bool FFmpegReceiver::stop() {
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
    connected_ = false;
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
    return true;
}

EncodedPacket FFmpegReceiver::receivePacket() {
    EncodedPacket result;

    if (!formatCtx_) return result;

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt) return result;

    while (true) {
        int ret = av_read_frame(formatCtx_, avpkt);
        if (ret < 0) {
            // EOF or error
            if (ret != AVERROR_EOF) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                std::cerr << "[FFmpegReceiver] Read error: " << errbuf << std::endl;
            }
            av_packet_free(&avpkt);
            return result;
        }

        // Only process video and audio streams
        if (avpkt->stream_index != videoStreamIndex_ &&
            avpkt->stream_index != audioStreamIndex_) {
            av_packet_unref(avpkt);
            continue;
        }

        // Fill result
        result.data.assign(avpkt->data, avpkt->data + avpkt->size);
        result.streamIndex = avpkt->stream_index;

        AVStream* stream = formatCtx_->streams[avpkt->stream_index];
        result.timebaseNum = stream->time_base.num;
        result.timebaseDen = stream->time_base.den;

        // Convert timestamps to microseconds
        if (avpkt->pts != AV_NOPTS_VALUE) {
            result.pts = av_rescale_q(avpkt->pts, stream->time_base, {1, 1000000});
        }
        if (avpkt->dts != AV_NOPTS_VALUE) {
            result.dts = av_rescale_q(avpkt->dts, stream->time_base, {1, 1000000});
        }

        if (avpkt->stream_index == videoStreamIndex_) {
            result.type = MediaType::Video;
            result.isKeyFrame = (avpkt->flags & AV_PKT_FLAG_KEY) != 0;
        } else {
            result.type = MediaType::Audio;
            result.isKeyFrame = false;
        }

        av_packet_unref(avpkt);
        break;
    }

    av_packet_free(&avpkt);
    return result;
}

StreamInfo FFmpegReceiver::getStreamInfo() {
    StreamInfo info;
    if (!formatCtx_) return info;

    if (videoStreamIndex_ >= 0) {
        AVStream* vs = formatCtx_->streams[videoStreamIndex_];
        AVCodecParameters* vpar = vs->codecpar;
        info.width = vpar->width;
        info.height = vpar->height;
        info.bitrate = static_cast<int>(vpar->bit_rate);
        info.videoCodec = mapCodecId(vpar->codec_id);

        if (vs->avg_frame_rate.den > 0) {
            info.fps = vs->avg_frame_rate.num / vs->avg_frame_rate.den;
        }

        // Copy extradata (SPS/PPS etc.)
        if (vpar->extradata && vpar->extradata_size > 0) {
            info.videoExtradata.assign(
                vpar->extradata, vpar->extradata + vpar->extradata_size);
        }
    }

    if (audioStreamIndex_ >= 0) {
        AVStream* as = formatCtx_->streams[audioStreamIndex_];
        AVCodecParameters* apar = as->codecpar;
        info.sampleRate = apar->sample_rate;
        info.channels = apar->ch_layout.nb_channels;
        info.audioCodec = mapCodecId(apar->codec_id);

        if (apar->extradata && apar->extradata_size > 0) {
            info.audioExtradata.assign(
                apar->extradata, apar->extradata + apar->extradata_size);
        }
    }

    return info;
}

CodecType FFmpegReceiver::mapCodecId(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264: return CodecType::H264;
        case AV_CODEC_ID_H265:
        case AV_CODEC_ID_HEVC: return CodecType::H265;
        case AV_CODEC_ID_AAC:  return CodecType::AAC;
        case AV_CODEC_ID_OPUS: return CodecType::OPUS;
        default:               return CodecType::Unknown;
    }
}

} // namespace puller
} // namespace reallive
