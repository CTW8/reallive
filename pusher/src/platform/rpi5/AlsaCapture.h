#pragma once

#include "platform/IAudioCapture.h"

#include <alsa/asoundlib.h>

namespace reallive {

class AlsaCapture : public IAudioCapture {
public:
    AlsaCapture();
    ~AlsaCapture() override;

    bool open(const AudioConfig& config) override;
    bool start() override;
    bool stop() override;
    AudioFrame captureFrame() override;
    bool isOpen() const override;
    std::string getName() const override;

private:
    snd_pcm_t* pcm_ = nullptr;
    AudioConfig config_;
    bool opened_ = false;
    bool started_ = false;

    snd_pcm_uframes_t periodSize_ = 1024;
    std::vector<uint8_t> buffer_;
    int64_t sampleCount_ = 0;
};

} // namespace reallive
