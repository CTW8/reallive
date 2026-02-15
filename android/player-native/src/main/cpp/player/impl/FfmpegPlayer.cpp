#include "player/impl/FfmpegPlayer.h"

#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define RL_TAG "RealLiveNativePlayer"
#define RL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, RL_TAG, __VA_ARGS__)
#define RL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, RL_TAG, __VA_ARGS__)

namespace reallive::player {

enum class PlayMode {
    None = 0,
    Live = 1,
    History = 2,
};

struct NativePlayerContext {
    std::mutex mutex;
    ANativeWindow* window = nullptr;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;

    GLuint glProgram = 0;
    GLuint glTexture = 0;
    GLint glAttrPos = -1;
    GLint glAttrTex = -1;
    GLint glUniTex = -1;
    int glTexWidth = 0;
    int glTexHeight = 0;

    std::thread renderThread;
    std::thread decodeThread;
    std::atomic<bool> running{false};
    std::atomic<bool> playing{false};

    std::atomic<uint64_t> playSerial{0};
    std::atomic<int64_t> pendingSeekMs{-1};
    std::atomic<bool> interruptRequested{false};

    std::string liveUrl;
    std::string historyUrl;
    int64_t historyStartMs = 0;
    PlayMode mode = PlayMode::None;

    std::mutex frameMutex;
    std::vector<uint8_t> frameRgba;
    int frameWidth = 0;
    int frameHeight = 0;
    uint64_t frameSerial = 0;
    bool frameReady = false;
};

namespace {

struct DecoderSession {
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgbaFrame = nullptr;
    uint8_t* rgbaBuffer = nullptr;
    int rgbaBufferSize = 0;
    int videoStreamIndex = -1;
    int swsSrcPixFmt = AV_PIX_FMT_NONE;
    int swsWidth = 0;
    int swsHeight = 0;

    std::string openedUrl;
    PlayMode openedMode = PlayMode::None;
    uint64_t openedSerial = 0;
};

int ffmpegInterruptCallback(void* opaque) {
    auto* ctx = reinterpret_cast<NativePlayerContext*>(opaque);
    if (!ctx) return 1;
    if (!ctx->running.load()) return 1;
    if (ctx->interruptRequested.load()) return 1;
    return 0;
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return shader;

    char logBuf[512] = {0};
    GLsizei logLen = 0;
    glGetShaderInfoLog(shader, sizeof(logBuf), &logLen, logBuf);
    RL_LOGE("shader compile failed: %s", logBuf);
    glDeleteShader(shader);
    return 0;
}

GLuint buildProgram() {
    static const char* kVertex =
        "attribute vec2 aPos;\n"
        "attribute vec2 aTex;\n"
        "varying vec2 vTex;\n"
        "void main() {\n"
        "  vTex = aTex;\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    static const char* kFragment =
        "precision mediump float;\n"
        "varying vec2 vTex;\n"
        "uniform sampler2D uTex;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(uTex, vTex);\n"
        "}\n";

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertex);
    if (!vs) return 0;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragment);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return program;

    char logBuf[512] = {0};
    GLsizei logLen = 0;
    glGetProgramInfoLog(program, sizeof(logBuf), &logLen, logBuf);
    RL_LOGE("program link failed: %s", logBuf);
    glDeleteProgram(program);
    return 0;
}

void destroyGl(NativePlayerContext* ctx) {
    if (ctx->glTexture) {
        glDeleteTextures(1, &ctx->glTexture);
        ctx->glTexture = 0;
    }
    if (ctx->glProgram) {
        glDeleteProgram(ctx->glProgram);
        ctx->glProgram = 0;
    }
    ctx->glAttrPos = -1;
    ctx->glAttrTex = -1;
    ctx->glUniTex = -1;
    ctx->glTexWidth = 0;
    ctx->glTexHeight = 0;
}

bool initGl(NativePlayerContext* ctx) {
    ctx->glProgram = buildProgram();
    if (!ctx->glProgram) return false;

    ctx->glAttrPos = glGetAttribLocation(ctx->glProgram, "aPos");
    ctx->glAttrTex = glGetAttribLocation(ctx->glProgram, "aTex");
    ctx->glUniTex = glGetUniformLocation(ctx->glProgram, "uTex");
    if (ctx->glAttrPos < 0 || ctx->glAttrTex < 0 || ctx->glUniTex < 0) {
        destroyGl(ctx);
        return false;
    }

    glGenTextures(1, &ctx->glTexture);
    if (!ctx->glTexture) {
        destroyGl(ctx);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, ctx->glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void destroyEgl(NativePlayerContext* ctx) {
    if (ctx->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        destroyGl(ctx);
        if (ctx->surface != EGL_NO_SURFACE) {
            eglDestroySurface(ctx->display, ctx->surface);
            ctx->surface = EGL_NO_SURFACE;
        }
        if (ctx->context != EGL_NO_CONTEXT) {
            eglDestroyContext(ctx->display, ctx->context);
            ctx->context = EGL_NO_CONTEXT;
        }
        eglTerminate(ctx->display);
        ctx->display = EGL_NO_DISPLAY;
    }
}

bool initEgl(NativePlayerContext* ctx) {
    if (!ctx->window) return false;

    ctx->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (ctx->display == EGL_NO_DISPLAY) return false;
    if (!eglInitialize(ctx->display, nullptr, nullptr)) return false;

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(ctx->display, configAttribs, &config, 1, &numConfigs) || numConfigs <= 0) {
        return false;
    }

    ctx->surface = eglCreateWindowSurface(ctx->display, config, ctx->window, nullptr);
    if (ctx->surface == EGL_NO_SURFACE) return false;

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    ctx->context = eglCreateContext(ctx->display, config, EGL_NO_CONTEXT, contextAttribs);
    if (ctx->context == EGL_NO_CONTEXT) return false;

    if (!eglMakeCurrent(ctx->display, ctx->surface, ctx->surface, ctx->context)) {
        return false;
    }

    if (!initGl(ctx)) {
        return false;
    }

    return true;
}

void renderColor(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void renderFrame(NativePlayerContext* ctx, const uint8_t* rgba, int width, int height) {
    if (!ctx->glProgram || !ctx->glTexture || !rgba || width <= 0 || height <= 0) {
        renderColor(0.02f, 0.02f, 0.02f);
        return;
    }

    glUseProgram(ctx->glProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->glTexture);

    if (ctx->glTexWidth != width || ctx->glTexHeight != height) {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            rgba
        );
        ctx->glTexWidth = width;
        ctx->glTexHeight = height;
    } else {
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            width,
            height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            rgba
        );
    }

    glUniform1i(ctx->glUniTex, 0);

    static const GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };

    glEnableVertexAttribArray(static_cast<GLuint>(ctx->glAttrPos));
    glEnableVertexAttribArray(static_cast<GLuint>(ctx->glAttrTex));

    glVertexAttribPointer(
        static_cast<GLuint>(ctx->glAttrPos),
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(GLfloat),
        vertices
    );
    glVertexAttribPointer(
        static_cast<GLuint>(ctx->glAttrTex),
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(GLfloat),
        vertices + 2
    );

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(static_cast<GLuint>(ctx->glAttrPos));
    glDisableVertexAttribArray(static_cast<GLuint>(ctx->glAttrTex));
    glBindTexture(GL_TEXTURE_2D, 0);
}

void clearFrameCache(NativePlayerContext* ctx) {
    std::lock_guard<std::mutex> lock(ctx->frameMutex);
    ctx->frameRgba.clear();
    ctx->frameWidth = 0;
    ctx->frameHeight = 0;
    ctx->frameReady = false;
    ctx->frameSerial += 1;
}

void renderLoop(NativePlayerContext* ctx) {
    uint64_t lastSerial = 0;
    std::vector<uint8_t> localFrame;
    int localW = 0;
    int localH = 0;

    while (ctx->running.load()) {
        bool isPlaying = ctx->playing.load();

        bool hasFrame = false;
        {
            std::lock_guard<std::mutex> lock(ctx->frameMutex);
            if (ctx->frameSerial != lastSerial) {
                lastSerial = ctx->frameSerial;
                if (ctx->frameReady && !ctx->frameRgba.empty() && ctx->frameWidth > 0 && ctx->frameHeight > 0) {
                    localFrame = ctx->frameRgba;
                    localW = ctx->frameWidth;
                    localH = ctx->frameHeight;
                } else {
                    localFrame.clear();
                    localW = 0;
                    localH = 0;
                }
            }
            hasFrame = !localFrame.empty() && localW > 0 && localH > 0;
        }

        bool drew = false;
        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            const bool canRender = (ctx->display != EGL_NO_DISPLAY && ctx->surface != EGL_NO_SURFACE && ctx->context != EGL_NO_CONTEXT);
            if (canRender && ctx->window) {
                const int surfaceW = ANativeWindow_getWidth(ctx->window);
                const int surfaceH = ANativeWindow_getHeight(ctx->window);
                if (surfaceW > 0 && surfaceH > 0) {
                    glViewport(0, 0, surfaceW, surfaceH);
                    if (hasFrame) {
                        renderFrame(ctx, localFrame.data(), localW, localH);
                    } else {
                        if (isPlaying) {
                            renderColor(0.02f, 0.08f, 0.16f);
                        } else {
                            renderColor(0.06f, 0.04f, 0.04f);
                        }
                    }
                    eglSwapBuffers(ctx->display, ctx->surface);
                    drew = true;
                } else {
                    renderColor(0.01f, 0.01f, 0.01f);
                    eglSwapBuffers(ctx->display, ctx->surface);
                    drew = true;
                }
            }
        }
        if (!drew) {
            if (isPlaying) {
                std::this_thread::sleep_for(std::chrono::milliseconds(12));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
}

void closeDecoder(DecoderSession& s) {
    if (s.packet) {
        av_packet_free(&s.packet);
    }
    if (s.frame) {
        av_frame_free(&s.frame);
    }
    if (s.rgbaFrame) {
        av_frame_free(&s.rgbaFrame);
    }
    if (s.rgbaBuffer) {
        av_free(s.rgbaBuffer);
        s.rgbaBuffer = nullptr;
    }
    if (s.swsCtx) {
        sws_freeContext(s.swsCtx);
        s.swsCtx = nullptr;
    }
    if (s.codecCtx) {
        avcodec_free_context(&s.codecCtx);
    }
    if (s.fmtCtx) {
        avformat_close_input(&s.fmtCtx);
    }
    s.rgbaBufferSize = 0;
    s.videoStreamIndex = -1;
    s.swsSrcPixFmt = AV_PIX_FMT_NONE;
    s.swsWidth = 0;
    s.swsHeight = 0;
    s.openedUrl.clear();
    s.openedMode = PlayMode::None;
    s.openedSerial = 0;
}

bool openDecoder(NativePlayerContext* ctx, DecoderSession& s, const std::string& url, PlayMode mode, uint64_t serial) {
    closeDecoder(s);

    s.fmtCtx = avformat_alloc_context();
    if (!s.fmtCtx) {
        RL_LOGE("avformat_alloc_context failed");
        return false;
    }
    s.fmtCtx->interrupt_callback.callback = ffmpegInterruptCallback;
    s.fmtCtx->interrupt_callback.opaque = ctx;

    int ret = avformat_open_input(&s.fmtCtx, url.c_str(), nullptr, nullptr);
    if (ret < 0) {
        RL_LOGE("avformat_open_input failed: %d, url=%s", ret, url.c_str());
        closeDecoder(s);
        return false;
    }

    ret = avformat_find_stream_info(s.fmtCtx, nullptr);
    if (ret < 0) {
        RL_LOGE("avformat_find_stream_info failed: %d", ret);
        closeDecoder(s);
        return false;
    }

    s.videoStreamIndex = av_find_best_stream(s.fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s.videoStreamIndex < 0) {
        RL_LOGE("video stream not found");
        closeDecoder(s);
        return false;
    }

    AVStream* stream = s.fmtCtx->streams[s.videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        RL_LOGE("decoder not found for codec_id=%d", stream->codecpar->codec_id);
        closeDecoder(s);
        return false;
    }

    s.codecCtx = avcodec_alloc_context3(codec);
    if (!s.codecCtx) {
        RL_LOGE("avcodec_alloc_context3 failed");
        closeDecoder(s);
        return false;
    }

    ret = avcodec_parameters_to_context(s.codecCtx, stream->codecpar);
    if (ret < 0) {
        RL_LOGE("avcodec_parameters_to_context failed: %d", ret);
        closeDecoder(s);
        return false;
    }

    ret = avcodec_open2(s.codecCtx, codec, nullptr);
    if (ret < 0) {
        RL_LOGE("avcodec_open2 failed: %d", ret);
        closeDecoder(s);
        return false;
    }

    s.packet = av_packet_alloc();
    s.frame = av_frame_alloc();
    s.rgbaFrame = av_frame_alloc();
    if (!s.packet || !s.frame || !s.rgbaFrame) {
        RL_LOGE("av_packet/frame alloc failed");
        closeDecoder(s);
        return false;
    }

    s.openedUrl = url;
    s.openedMode = mode;
    s.openedSerial = serial;

    RL_LOGI("decoder opened: mode=%d url=%s", static_cast<int>(mode), url.c_str());
    return true;
}

bool seekDecoder(DecoderSession& s, int64_t seekMs) {
    if (!s.fmtCtx || !s.codecCtx || s.videoStreamIndex < 0 || seekMs < 0) return false;

    AVStream* stream = s.fmtCtx->streams[s.videoStreamIndex];
    int64_t target = av_rescale_q(seekMs, AVRational{1, 1000}, stream->time_base);

    avcodec_flush_buffers(s.codecCtx);
    int ret = av_seek_frame(s.fmtCtx, s.videoStreamIndex, target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        int64_t targetUs = av_rescale_q(seekMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
        ret = av_seek_frame(s.fmtCtx, -1, targetUs, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(s.codecCtx);

    if (ret < 0) {
        RL_LOGE("seek failed: %d ms=%lld", ret, static_cast<long long>(seekMs));
        return false;
    }

    RL_LOGI("seek ok: %lld ms", static_cast<long long>(seekMs));
    return true;
}

bool ensureScaler(DecoderSession& s, AVFrame* srcFrame) {
    const int srcW = srcFrame->width;
    const int srcH = srcFrame->height;
    const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(srcFrame->format);

    if (srcW <= 0 || srcH <= 0 || srcFmt == AV_PIX_FMT_NONE) return false;

    if (s.swsCtx && s.swsWidth == srcW && s.swsHeight == srcH && s.swsSrcPixFmt == srcFmt) {
        return true;
    }

    s.swsCtx = sws_getCachedContext(
        s.swsCtx,
        srcW,
        srcH,
        srcFmt,
        srcW,
        srcH,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    if (!s.swsCtx) return false;

    const int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, srcW, srcH, 1);
    if (bufferSize <= 0) return false;

    if (bufferSize != s.rgbaBufferSize) {
        if (s.rgbaBuffer) {
            av_free(s.rgbaBuffer);
            s.rgbaBuffer = nullptr;
        }
        s.rgbaBuffer = static_cast<uint8_t*>(av_malloc(static_cast<size_t>(bufferSize)));
        if (!s.rgbaBuffer) {
            s.rgbaBufferSize = 0;
            return false;
        }
        s.rgbaBufferSize = bufferSize;
    }

    int ret = av_image_fill_arrays(
        s.rgbaFrame->data,
        s.rgbaFrame->linesize,
        s.rgbaBuffer,
        AV_PIX_FMT_RGBA,
        srcW,
        srcH,
        1
    );
    if (ret < 0) return false;

    s.swsWidth = srcW;
    s.swsHeight = srcH;
    s.swsSrcPixFmt = srcFmt;

    return true;
}

void pushFrameToRender(NativePlayerContext* ctx, DecoderSession& s) {
    const int width = s.swsWidth;
    const int height = s.swsHeight;
    if (width <= 0 || height <= 0) return;

    const int srcStride = s.rgbaFrame->linesize[0];
    const int dstStride = width * 4;
    if (srcStride <= 0 || dstStride <= 0) return;

    const size_t dstSize = static_cast<size_t>(dstStride) * static_cast<size_t>(height);

    std::lock_guard<std::mutex> lock(ctx->frameMutex);
    ctx->frameRgba.resize(dstSize);

    for (int y = 0; y < height; y += 1) {
        const uint8_t* src = s.rgbaFrame->data[0] + y * srcStride;
        uint8_t* dst = ctx->frameRgba.data() + static_cast<size_t>(y) * static_cast<size_t>(dstStride);
        std::memcpy(dst, src, static_cast<size_t>(dstStride));
    }

    ctx->frameWidth = width;
    ctx->frameHeight = height;
    ctx->frameReady = true;
    ctx->frameSerial += 1;
}

void decodeLoop(NativePlayerContext* ctx) {
    DecoderSession session;

    while (ctx->running.load()) {
        if (!ctx->playing.load()) {
            closeDecoder(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        uint64_t serial = ctx->playSerial.load();
        PlayMode mode = PlayMode::None;
        std::string url;
        int64_t initialSeekMs = -1;

        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            mode = ctx->mode;
            if (mode == PlayMode::Live) {
                url = ctx->liveUrl;
            } else if (mode == PlayMode::History) {
                url = ctx->historyUrl;
                initialSeekMs = ctx->historyStartMs;
            }
        }

        if (mode == PlayMode::None || url.empty()) {
            closeDecoder(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (!session.fmtCtx || session.openedSerial != serial || session.openedUrl != url || session.openedMode != mode) {
            ctx->interruptRequested.store(false);
            if (!openDecoder(ctx, session, url, mode, serial)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            int64_t seekMs = ctx->pendingSeekMs.exchange(-1);
            if (seekMs < 0 && mode == PlayMode::History) {
                seekMs = initialSeekMs;
            }
            if (seekMs >= 0) {
                seekDecoder(session, seekMs);
            }
        }

        int64_t seekReq = ctx->pendingSeekMs.exchange(-1);
        if (seekReq >= 0) {
            seekDecoder(session, seekReq);
        }

        ctx->interruptRequested.store(false);

        int ret = av_read_frame(session.fmtCtx, session.packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (mode == PlayMode::History) {
                    ctx->playing.store(false);
                    clearFrameCache(ctx);
                    closeDecoder(session);
                } else {
                    closeDecoder(session);
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                }
            } else {
                if (ctx->interruptRequested.load()) {
                    ctx->interruptRequested.store(false);
                } else {
                    RL_LOGE("av_read_frame failed: %d", ret);
                }
                if (mode == PlayMode::Live) {
                    closeDecoder(session);
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            continue;
        }

        if (session.packet->stream_index == session.videoStreamIndex) {
            ret = avcodec_send_packet(session.codecCtx, session.packet);
            if (ret >= 0) {
                while (ret >= 0) {
                    ret = avcodec_receive_frame(session.codecCtx, session.frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    if (ret < 0) {
                        RL_LOGE("avcodec_receive_frame failed: %d", ret);
                        break;
                    }

                    if (!ensureScaler(session, session.frame)) {
                        RL_LOGE("ensureScaler failed");
                        continue;
                    }

                    sws_scale(
                        session.swsCtx,
                        session.frame->data,
                        session.frame->linesize,
                        0,
                        session.frame->height,
                        session.rgbaFrame->data,
                        session.rgbaFrame->linesize
                    );

                    pushFrameToRender(ctx, session);
                }
            }
        }

        av_packet_unref(session.packet);
    }

    closeDecoder(session);
}

}  // namespace

FfmpegPlayer::FfmpegPlayer() {
    static std::once_flag ffmpegOnce;
    std::call_once(ffmpegOnce, [] {
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
    });

    ctx_ = new NativePlayerContext();
    ctx_->running = true;
    ctx_->renderThread = std::thread(renderLoop, ctx_);
    ctx_->decodeThread = std::thread(decodeLoop, ctx_);
    RL_LOGI("FfmpegPlayer created");
}

FfmpegPlayer::~FfmpegPlayer() {
    stop();
    if (!ctx_) return;

    ctx_->running.store(false);
    ctx_->interruptRequested.store(true);

    if (ctx_->decodeThread.joinable()) {
        ctx_->decodeThread.join();
    }
    if (ctx_->renderThread.joinable()) {
        ctx_->renderThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(ctx_->mutex);
        destroyEgl(ctx_);
        if (ctx_->window) {
            ANativeWindow_release(ctx_->window);
            ctx_->window = nullptr;
        }
    }

    delete ctx_;
    ctx_ = nullptr;
    RL_LOGI("FfmpegPlayer released");
}

bool FfmpegPlayer::play(const PlayerSource& source) {
    if (!ctx_) return false;
    if (source.url.empty()) return false;

    if (source.type == SourceType::Live) {
        {
            std::lock_guard<std::mutex> lock(ctx_->mutex);
            ctx_->liveUrl = source.url;
            ctx_->mode = PlayMode::Live;
            ctx_->historyStartMs = 0;
        }
        ctx_->pendingSeekMs.store(-1);
        ctx_->playing.store(true);
        ctx_->interruptRequested.store(true);
        ctx_->playSerial.fetch_add(1);
        clearFrameCache(ctx_);
        RL_LOGI("play live: %s", source.url.c_str());
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(ctx_->mutex);
        ctx_->historyUrl = source.url;
        ctx_->historyStartMs = source.startMs;
        ctx_->mode = PlayMode::History;
    }
    ctx_->pendingSeekMs.store(source.startMs);
    ctx_->playing.store(true);
    ctx_->interruptRequested.store(true);
    ctx_->playSerial.fetch_add(1);
    clearFrameCache(ctx_);
    RL_LOGI("play history: %s start=%lld", source.url.c_str(), static_cast<long long>(source.startMs));
    return true;
}

void FfmpegPlayer::seekTo(int64_t positionMs) {
    if (!ctx_) return;
    if (positionMs < 0) positionMs = 0;
    ctx_->pendingSeekMs.store(positionMs);
    ctx_->interruptRequested.store(true);
}

void FfmpegPlayer::stop() {
    if (!ctx_) return;
    {
        std::lock_guard<std::mutex> lock(ctx_->mutex);
        ctx_->mode = PlayMode::None;
        ctx_->historyStartMs = 0;
    }
    ctx_->playing.store(false);
    ctx_->pendingSeekMs.store(-1);
    ctx_->interruptRequested.store(true);
    ctx_->playSerial.fetch_add(1);
    clearFrameCache(ctx_);
}

void FfmpegPlayer::setSurface(ANativeWindow* window) {
    if (!ctx_) return;

    ANativeWindow* newWindow = window;
    if (newWindow) {
        ANativeWindow_acquire(newWindow);
    }

    std::lock_guard<std::mutex> lock(ctx_->mutex);

    destroyEgl(ctx_);

    if (ctx_->window) {
        ANativeWindow_release(ctx_->window);
        ctx_->window = nullptr;
    }

    ctx_->window = newWindow;
    if (ctx_->window) {
        if (!initEgl(ctx_)) {
            RL_LOGE("initEgl failed");
            ANativeWindow_release(ctx_->window);
            ctx_->window = nullptr;
        }
    }
}

PlayerStats FfmpegPlayer::stats() const {
    PlayerStats s{};
    if (!ctx_) return s;
    std::lock_guard<std::mutex> lock(ctx_->frameMutex);
    s.videoWidth = ctx_->frameWidth;
    s.videoHeight = ctx_->frameHeight;
    s.bufferedFrames = ctx_->frameReady ? 1 : 0;
    return s;
}

}  // namespace reallive::player
