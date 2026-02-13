#include "platform/rpi5/AlsaCapture.h"

#include <iostream>
#include <chrono>

namespace reallive {

AlsaCapture::AlsaCapture() = default;

AlsaCapture::~AlsaCapture() {
    stop();
}

bool AlsaCapture::open(const AudioConfig& config) {
    config_ = config;

    std::string device = config.device.empty() ? "default" : config.device;

    int ret = snd_pcm_open(&pcm_, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to open device '" << device
                  << "': " << snd_strerror(ret) << std::endl;
        return false;
    }

    // Set hardware parameters
    snd_pcm_hw_params_t* hwParams = nullptr;
    snd_pcm_hw_params_alloca(&hwParams);
    snd_pcm_hw_params_any(pcm_, hwParams);

    ret = snd_pcm_hw_params_set_access(pcm_, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to set access: " << snd_strerror(ret) << std::endl;
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Set format to S16_LE (16-bit signed little-endian)
    ret = snd_pcm_hw_params_set_format(pcm_, hwParams, SND_PCM_FORMAT_S16_LE);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to set format S16_LE: " << snd_strerror(ret) << std::endl;
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Set sample rate
    unsigned int rate = static_cast<unsigned int>(config.sampleRate);
    ret = snd_pcm_hw_params_set_rate_near(pcm_, hwParams, &rate, nullptr);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to set sample rate: " << snd_strerror(ret) << std::endl;
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }
    if (static_cast<int>(rate) != config.sampleRate) {
        std::cout << "[AlsaCapture] Sample rate adjusted to " << rate << std::endl;
    }

    // Set channels
    ret = snd_pcm_hw_params_set_channels(pcm_, hwParams, static_cast<unsigned int>(config.channels));
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to set channels: " << snd_strerror(ret) << std::endl;
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Set period size
    snd_pcm_uframes_t periodSize = periodSize_;
    ret = snd_pcm_hw_params_set_period_size_near(pcm_, hwParams, &periodSize, nullptr);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to set period size: " << snd_strerror(ret) << std::endl;
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }
    periodSize_ = periodSize;

    // Apply hardware parameters
    ret = snd_pcm_hw_params(pcm_, hwParams);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to apply hw params: " << snd_strerror(ret) << std::endl;
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    // Allocate buffer for one period
    int bytesPerSample = config.bitsPerSample / 8;
    int frameSize = bytesPerSample * config.channels;
    buffer_.resize(periodSize_ * frameSize);

    opened_ = true;
    std::cout << "[AlsaCapture] Opened device '" << device
              << "' rate=" << rate
              << " channels=" << config.channels
              << " period=" << periodSize_ << std::endl;
    return true;
}

bool AlsaCapture::start() {
    if (!opened_ || !pcm_) return false;
    if (started_) return true;

    int ret = snd_pcm_prepare(pcm_);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to prepare PCM: " << snd_strerror(ret) << std::endl;
        return false;
    }

    ret = snd_pcm_start(pcm_);
    if (ret < 0) {
        std::cerr << "[AlsaCapture] Failed to start PCM: " << snd_strerror(ret) << std::endl;
        return false;
    }

    sampleCount_ = 0;
    started_ = true;
    return true;
}

bool AlsaCapture::stop() {
    if (!pcm_) return true;

    if (started_) {
        snd_pcm_drop(pcm_);
        started_ = false;
    }

    snd_pcm_close(pcm_);
    pcm_ = nullptr;
    opened_ = false;
    return true;
}

AudioFrame AlsaCapture::captureFrame() {
    AudioFrame frame;

    if (!started_ || !pcm_) {
        return frame;
    }

    snd_pcm_sframes_t framesRead = snd_pcm_readi(pcm_, buffer_.data(),
                                                   static_cast<snd_pcm_uframes_t>(periodSize_));
    if (framesRead < 0) {
        // Try to recover from overrun/underrun
        framesRead = snd_pcm_recover(pcm_, static_cast<int>(framesRead), 1);
        if (framesRead < 0) {
            std::cerr << "[AlsaCapture] Read failed: " << snd_strerror(static_cast<int>(framesRead))
                      << std::endl;
            return frame;
        }
        // Retry the read after recovery
        framesRead = snd_pcm_readi(pcm_, buffer_.data(),
                                    static_cast<snd_pcm_uframes_t>(periodSize_));
        if (framesRead < 0) {
            return frame;
        }
    }

    if (framesRead == 0) {
        return frame;
    }

    int bytesPerSample = config_.bitsPerSample / 8;
    int frameSize = bytesPerSample * config_.channels;
    size_t dataSize = static_cast<size_t>(framesRead) * frameSize;

    frame.data.assign(buffer_.data(), buffer_.data() + dataSize);
    frame.samples = static_cast<int>(framesRead);
    frame.sampleRate = config_.sampleRate;
    frame.channels = config_.channels;

    // Compute PTS based on cumulative sample count
    frame.pts = sampleCount_ * 1000000LL / config_.sampleRate;
    sampleCount_ += framesRead;

    return frame;
}

bool AlsaCapture::isOpen() const {
    return opened_;
}

std::string AlsaCapture::getName() const {
    return "ALSA Audio Capture (RPi5)";
}

} // namespace reallive
