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
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>
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
    GLuint glVertexBuffer = 0;
    GLint glAttrPos = -1;
    GLint glAttrTex = -1;
    GLint glUniTex = -1;
    int glTexWidth = 0;
    int glTexHeight = 0;

    std::thread renderThread;
    std::thread decodeThread;
    std::atomic<bool> running{false};
    std::atomic<bool> playing{false};
    std::atomic<PlaybackState> runtimeState{PlaybackState::Idle};

    std::atomic<uint64_t> playSerial{0};
    std::atomic<int64_t> pendingSeekMs{-1};
    std::atomic<bool> interruptRequested{false};

    std::atomic<uint64_t> decodedFrameCount{0};
    std::atomic<uint64_t> renderedFrameCount{0};
    std::atomic<uint64_t> demuxPacketCount{0};
    std::atomic<uint64_t> videoPacketCount{0};
    std::atomic<uint64_t> swsFrameCount{0};
    std::atomic<uint64_t> queuedFrameCount{0};
    std::atomic<uint64_t> swapCount{0};
    std::atomic<uint64_t> statsQueryCount{0};
    std::mutex statsMutex;
    std::chrono::steady_clock::time_point statsLastTs{};
    uint64_t statsLastDecoded = 0;
    uint64_t statsLastRendered = 0;
    double decodeFps = 0.0;
    double renderFps = 0.0;

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
    bool loggedFirstViewport = false;
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
    bool loggedFirstVideoPacket = false;
    bool loggedFirstDecodedFrame = false;
    bool loggedFirstRgbaSample = false;
    uint64_t packetsRead = 0;
    uint64_t videoPackets = 0;
    uint64_t videoBytes = 0;
    uint64_t scaledFrames = 0;
    uint64_t queuedFrames = 0;
    uint64_t decodedFrames = 0;
};

constexpr uint64_t kPacketLogInterval = 240;
constexpr uint64_t kFrameLogInterval = 120;

const char* toString(PlayMode mode) {
    switch (mode) {
        case PlayMode::Live:
            return "live";
        case PlayMode::History:
            return "history";
        case PlayMode::None:
        default:
            return "none";
    }
}

const char* toString(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle:
            return "idle";
        case PlaybackState::Connecting:
            return "connecting";
        case PlaybackState::Playing:
            return "playing";
        case PlaybackState::Buffering:
            return "buffering";
        case PlaybackState::Ended:
            return "ended";
        case PlaybackState::Error:
            return "error";
        default:
            return "unknown";
    }
}

void setRuntimeState(NativePlayerContext* ctx, PlaybackState state, const char* reason) {
    if (!ctx) return;
    const auto old = ctx->runtimeState.load();
    if (old != state) {
        ctx->runtimeState.store(state);
        RL_LOGI("state: %s -> %s reason=%s", toString(old), toString(state), reason ? reason : "-");
        return;
    }
    ctx->runtimeState.store(state);
}

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

void logGlError(const char* stage) {
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        RL_LOGE("gl error at %s: 0x%x", stage, static_cast<unsigned>(err));
    }
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
        "  vec4 c = texture2D(uTex, vTex);\n"
        "  gl_FragColor = vec4(c.rgb, 1.0);\n"
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
    if (ctx->glVertexBuffer) {
        glDeleteBuffers(1, &ctx->glVertexBuffer);
        ctx->glVertexBuffer = 0;
    }
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
    static const GLfloat kQuadVertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };

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

    glGenBuffers(1, &ctx->glVertexBuffer);
    if (!ctx->glVertexBuffer) {
        destroyGl(ctx);
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, ctx->glVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void destroyEgl(NativePlayerContext* ctx) {
    if (ctx->display != EGL_NO_DISPLAY) {
        RL_LOGI(
            "destroyEgl begin display=%p surface=%p context=%p window=%p",
            static_cast<void*>(ctx->display),
            static_cast<void*>(ctx->surface),
            static_cast<void*>(ctx->context),
            static_cast<void*>(ctx->window)
        );
        bool hasGlContext = false;
        if (ctx->context != EGL_NO_CONTEXT && ctx->surface != EGL_NO_SURFACE) {
            if (eglGetCurrentDisplay() == ctx->display && eglGetCurrentContext() == ctx->context) {
                hasGlContext = true;
            } else if (eglMakeCurrent(ctx->display, ctx->surface, ctx->surface, ctx->context)) {
                hasGlContext = true;
            } else {
                const EGLint eglErr = eglGetError();
                RL_LOGE("destroyEgl eglMakeCurrent failed: 0x%x", static_cast<unsigned>(eglErr));
            }
        }
        if (hasGlContext) {
            destroyGl(ctx);
        } else {
            ctx->glVertexBuffer = 0;
            ctx->glTexture = 0;
            ctx->glProgram = 0;
            ctx->glAttrPos = -1;
            ctx->glAttrTex = -1;
            ctx->glUniTex = -1;
            ctx->glTexWidth = 0;
            ctx->glTexHeight = 0;
        }
        eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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
        RL_LOGI("destroyEgl done");
    }
}

bool initEgl(NativePlayerContext* ctx) {
    if (!ctx->window) return false;

    const int winW = ANativeWindow_getWidth(ctx->window);
    const int winH = ANativeWindow_getHeight(ctx->window);
    RL_LOGI("initEgl begin window=%p size=%dx%d", static_cast<void*>(ctx->window), winW, winH);

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

    RL_LOGI(
        "initEgl ok display=%p surface=%p context=%p",
        static_cast<void*>(ctx->display),
        static_cast<void*>(ctx->surface),
        static_cast<void*>(ctx->context)
    );

    return true;
}

void renderColor(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void computeAspectFitViewport(
    int surfaceW,
    int surfaceH,
    int frameW,
    int frameH,
    int& outX,
    int& outY,
    int& outW,
    int& outH
) {
    outX = 0;
    outY = 0;
    outW = surfaceW;
    outH = surfaceH;
    if (surfaceW <= 0 || surfaceH <= 0 || frameW <= 0 || frameH <= 0) return;

    const double surfaceAspect = static_cast<double>(surfaceW) / static_cast<double>(surfaceH);
    const double frameAspect = static_cast<double>(frameW) / static_cast<double>(frameH);
    if (surfaceAspect > frameAspect) {
        outH = surfaceH;
        outW = static_cast<int>(std::round(static_cast<double>(outH) * frameAspect));
        outX = (surfaceW - outW) / 2;
        outY = 0;
    } else {
        outW = surfaceW;
        outH = static_cast<int>(std::round(static_cast<double>(outW) / frameAspect));
        outX = 0;
        outY = (surfaceH - outH) / 2;
    }
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
    glDisable(GL_BLEND);

    glEnableVertexAttribArray(static_cast<GLuint>(ctx->glAttrPos));
    glEnableVertexAttribArray(static_cast<GLuint>(ctx->glAttrTex));
    glBindBuffer(GL_ARRAY_BUFFER, ctx->glVertexBuffer);

    glVertexAttribPointer(
        static_cast<GLuint>(ctx->glAttrPos),
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(GLfloat),
        reinterpret_cast<void*>(0)
    );
    glVertexAttribPointer(
        static_cast<GLuint>(ctx->glAttrTex),
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(GLfloat),
        reinterpret_cast<void*>(2 * sizeof(GLfloat))
    );

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(static_cast<GLuint>(ctx->glAttrPos));
    glDisableVertexAttribArray(static_cast<GLuint>(ctx->glAttrTex));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    logGlError("renderFrame");
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
    RL_LOGI("renderLoop started");
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
                const bool needMakeCurrent =
                    eglGetCurrentDisplay() != ctx->display ||
                    eglGetCurrentSurface(EGL_DRAW) != ctx->surface ||
                    eglGetCurrentContext() != ctx->context;
                if (needMakeCurrent) {
                    if (!eglMakeCurrent(ctx->display, ctx->surface, ctx->surface, ctx->context)) {
                        const EGLint eglErr = eglGetError();
                        RL_LOGE("renderLoop eglMakeCurrent failed: 0x%x", static_cast<unsigned>(eglErr));
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        continue;
                    }
                }

                const int surfaceW = ANativeWindow_getWidth(ctx->window);
                const int surfaceH = ANativeWindow_getHeight(ctx->window);
                if (surfaceW > 0 && surfaceH > 0) {
                    glViewport(0, 0, surfaceW, surfaceH);
                    if (hasFrame) {
                        renderColor(0.0f, 0.0f, 0.0f);
                        int vpX = 0;
                        int vpY = 0;
                        int vpW = surfaceW;
                        int vpH = surfaceH;
                        computeAspectFitViewport(surfaceW, surfaceH, localW, localH, vpX, vpY, vpW, vpH);
                        glViewport(vpX, vpY, vpW, vpH);
                        if (!ctx->loggedFirstViewport) {
                            ctx->loggedFirstViewport = true;
                            RL_LOGI(
                                "viewport fit: surface=%dx%d frame=%dx%d viewport=%d,%d %dx%d",
                                surfaceW,
                                surfaceH,
                                localW,
                                localH,
                                vpX,
                                vpY,
                                vpW,
                                vpH
                            );
                        }
                        renderFrame(ctx, localFrame.data(), localW, localH);
                        const auto rendered = ctx->renderedFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (rendered == 1) {
                            RL_LOGI("first rendered frame: %dx%d onto surface=%dx%d", localW, localH, surfaceW, surfaceH);
                        }
                    } else {
                        if (isPlaying) {
                            renderColor(0.02f, 0.08f, 0.16f);
                        } else {
                            renderColor(0.06f, 0.04f, 0.04f);
                        }
                    }
                    if (!eglSwapBuffers(ctx->display, ctx->surface)) {
                        const EGLint eglErr = eglGetError();
                        RL_LOGE("eglSwapBuffers failed: 0x%x", static_cast<unsigned>(eglErr));
                    } else {
                        const auto swaps = ctx->swapCount.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (swaps % kFrameLogInterval == 0) {
                            RL_LOGI(
                                "render progress: swaps=%llu rendered=%llu hasFrame=%d surface=%dx%d frame=%dx%d",
                                static_cast<unsigned long long>(swaps),
                                static_cast<unsigned long long>(ctx->renderedFrameCount.load(std::memory_order_relaxed)),
                                hasFrame ? 1 : 0,
                                surfaceW,
                                surfaceH,
                                localW,
                                localH
                            );
                        }
                    }
                    drew = true;
                } else {
                    renderColor(0.01f, 0.01f, 0.01f);
                    if (!eglSwapBuffers(ctx->display, ctx->surface)) {
                        const EGLint eglErr = eglGetError();
                        RL_LOGE("eglSwapBuffers failed: 0x%x", static_cast<unsigned>(eglErr));
                    }
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
    RL_LOGI("renderLoop exited");
}

void closeDecoder(DecoderSession& s) {
    if (!s.openedUrl.empty()) {
        RL_LOGI(
            "decoder close summary: mode=%s url=%s packets=%llu videoPackets=%llu videoBytes=%llu scaled=%llu queued=%llu decoded=%llu",
            toString(s.openedMode),
            s.openedUrl.c_str(),
            static_cast<unsigned long long>(s.packetsRead),
            static_cast<unsigned long long>(s.videoPackets),
            static_cast<unsigned long long>(s.videoBytes),
            static_cast<unsigned long long>(s.scaledFrames),
            static_cast<unsigned long long>(s.queuedFrames),
            static_cast<unsigned long long>(s.decodedFrames)
        );
    }
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
    s.loggedFirstVideoPacket = false;
    s.loggedFirstDecodedFrame = false;
    s.loggedFirstRgbaSample = false;
    s.packetsRead = 0;
    s.videoPackets = 0;
    s.videoBytes = 0;
    s.scaledFrames = 0;
    s.queuedFrames = 0;
    s.decodedFrames = 0;

    RL_LOGI("decoder opened: mode=%s serial=%llu url=%s", toString(mode), static_cast<unsigned long long>(serial), url.c_str());
    RL_LOGI(
        "stream info: codec_id=%d width=%d height=%d time_base=%d/%d",
        static_cast<int>(stream->codecpar->codec_id),
        stream->codecpar->width,
        stream->codecpar->height,
        stream->time_base.num,
        stream->time_base.den
    );
    const double avgFps = av_q2d(stream->avg_frame_rate);
    RL_LOGI(
        "stream metrics: avgFps=%.2f codecBitrate=%lld streamBitrate=%lld formatBitrate=%lld",
        avgFps,
        static_cast<long long>(s.codecCtx ? s.codecCtx->bit_rate : 0),
        static_cast<long long>(stream->codecpar->bit_rate),
        static_cast<long long>(s.fmtCtx ? s.fmtCtx->bit_rate : 0)
    );
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

    const char* fmtName = av_get_pix_fmt_name(srcFmt);
    RL_LOGI(
        "sws reconfigure: src=%dx%d fmt=%s(%d) -> dst=rgba",
        srcW,
        srcH,
        fmtName ? fmtName : "unknown",
        static_cast<int>(srcFmt)
    );

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
    const auto queued = ctx->queuedFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
    s.queuedFrames += 1;
    if (queued % kFrameLogInterval == 0) {
        RL_LOGI(
            "queue progress: queued=%llu frameSerial=%llu size=%dx%d",
            static_cast<unsigned long long>(queued),
            static_cast<unsigned long long>(ctx->frameSerial),
            width,
            height
        );
    }
}

void decodeLoop(NativePlayerContext* ctx) {
    RL_LOGI("decodeLoop started");
    DecoderSession session;

    while (ctx->running.load()) {
        if (!ctx->playing.load()) {
            closeDecoder(session);
            setRuntimeState(ctx, PlaybackState::Idle, "not-playing");
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
            setRuntimeState(ctx, PlaybackState::Connecting, "open-decoder");
            if (!openDecoder(ctx, session, url, mode, serial)) {
                if (mode == PlayMode::History) {
                    setRuntimeState(ctx, PlaybackState::Error, "open-decoder-failed-history");
                } else {
                    setRuntimeState(ctx, PlaybackState::Buffering, "open-decoder-failed-live");
                }
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
                    setRuntimeState(ctx, PlaybackState::Ended, "history-eof");
                    clearFrameCache(ctx);
                    closeDecoder(session);
                } else {
                    setRuntimeState(ctx, PlaybackState::Buffering, "live-eof-reopen");
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
                    setRuntimeState(ctx, PlaybackState::Buffering, "av-read-failed-live");
                    closeDecoder(session);
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                } else {
                    setRuntimeState(ctx, PlaybackState::Error, "av-read-failed-history");
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            continue;
        }

        const auto demuxCount = ctx->demuxPacketCount.fetch_add(1, std::memory_order_relaxed) + 1;
        session.packetsRead += 1;
        if (demuxCount % kPacketLogInterval == 0) {
            RL_LOGI(
                "demux progress: packets=%llu mode=%s serial=%llu",
                static_cast<unsigned long long>(demuxCount),
                toString(mode),
                static_cast<unsigned long long>(serial)
            );
        }

        if (session.packet->stream_index == session.videoStreamIndex) {
            const auto videoPackets = ctx->videoPacketCount.fetch_add(1, std::memory_order_relaxed) + 1;
            session.videoPackets += 1;
            session.videoBytes += static_cast<uint64_t>(session.packet->size > 0 ? session.packet->size : 0);
            if (videoPackets % kPacketLogInterval == 0) {
                const double avgPacket = session.videoPackets > 0
                    ? static_cast<double>(session.videoBytes) / static_cast<double>(session.videoPackets)
                    : 0.0;
                RL_LOGI(
                    "video packet progress: packets=%llu pts=%lld dts=%lld size=%d avgPacket=%.1fB",
                    static_cast<unsigned long long>(videoPackets),
                    static_cast<long long>(session.packet->pts),
                    static_cast<long long>(session.packet->dts),
                    session.packet->size,
                    avgPacket
                );
            }
            if (!session.loggedFirstVideoPacket) {
                session.loggedFirstVideoPacket = true;
                RL_LOGI(
                    "first video packet: size=%d pts=%lld dts=%lld flags=0x%x",
                    session.packet->size,
                    static_cast<long long>(session.packet->pts),
                    static_cast<long long>(session.packet->dts),
                    session.packet->flags
                );
            }
            ret = avcodec_send_packet(session.codecCtx, session.packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                RL_LOGE("avcodec_send_packet failed: %d", ret);
            }
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
                    const auto scaled = ctx->swsFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
                    session.scaledFrames += 1;
                    if (scaled % kFrameLogInterval == 0) {
                        RL_LOGI(
                            "sws progress: scaled=%llu src=%dx%d fmt=%d",
                            static_cast<unsigned long long>(scaled),
                            session.frame->width,
                            session.frame->height,
                            session.frame->format
                        );
                    }
                    if (!session.loggedFirstRgbaSample &&
                        session.rgbaFrame->data[0] != nullptr &&
                        session.rgbaFrame->linesize[0] > 0) {
                        session.loggedFirstRgbaSample = true;
                        const int sampleRows = 8;
                        const int sampleCols = 8;
                        uint64_t sumR = 0;
                        uint64_t sumG = 0;
                        uint64_t sumB = 0;
                        int samples = 0;
                        for (int ry = 0; ry < sampleRows; ry += 1) {
                            const int y = (session.swsHeight - 1) * ry / (sampleRows - 1);
                            const uint8_t* row = session.rgbaFrame->data[0] + y * session.rgbaFrame->linesize[0];
                            for (int rx = 0; rx < sampleCols; rx += 1) {
                                const int x = (session.swsWidth - 1) * rx / (sampleCols - 1);
                                const uint8_t* px = row + x * 4;
                                sumR += px[0];
                                sumG += px[1];
                                sumB += px[2];
                                samples += 1;
                            }
                        }
                        if (samples > 0) {
                            RL_LOGI(
                                "first rgba sample avg: r=%.1f g=%.1f b=%.1f samples=%d",
                                static_cast<double>(sumR) / static_cast<double>(samples),
                                static_cast<double>(sumG) / static_cast<double>(samples),
                                static_cast<double>(sumB) / static_cast<double>(samples),
                                samples
                            );
                        }
                    }

                    pushFrameToRender(ctx, session);
                    ctx->decodedFrameCount.fetch_add(1, std::memory_order_relaxed);
                    session.decodedFrames += 1;
                    setRuntimeState(ctx, PlaybackState::Playing, "decoded-frame-ready");
                    if (session.decodedFrames % kFrameLogInterval == 0) {
                        RL_LOGI(
                            "decode progress: decoded=%llu globalDecoded=%llu queued=%llu rendered=%llu",
                            static_cast<unsigned long long>(session.decodedFrames),
                            static_cast<unsigned long long>(ctx->decodedFrameCount.load(std::memory_order_relaxed)),
                            static_cast<unsigned long long>(ctx->queuedFrameCount.load(std::memory_order_relaxed)),
                            static_cast<unsigned long long>(ctx->renderedFrameCount.load(std::memory_order_relaxed))
                        );
                    }
                    if (!session.loggedFirstDecodedFrame) {
                        session.loggedFirstDecodedFrame = true;
                        RL_LOGI(
                            "first decoded frame: %dx%d format=%d pts=%lld",
                            session.frame->width,
                            session.frame->height,
                            session.frame->format,
                            static_cast<long long>(session.frame->pts)
                        );
                    }
                }
            }
        }

        av_packet_unref(session.packet);
    }

    closeDecoder(session);
    RL_LOGI("decodeLoop exited");
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
    RL_LOGI("FfmpegPlayer created ctx=%p", static_cast<void*>(ctx_));
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
    RL_LOGI(
        "play request type=%s startMs=%lld url=%s",
        source.type == SourceType::Live ? "live" : "history",
        static_cast<long long>(source.startMs),
        source.url.c_str()
    );

    if (source.type == SourceType::Live) {
        {
            std::lock_guard<std::mutex> lock(ctx_->mutex);
            ctx_->liveUrl = source.url;
            ctx_->mode = PlayMode::Live;
            ctx_->historyStartMs = 0;
        }
        ctx_->pendingSeekMs.store(-1);
        ctx_->playing.store(true);
        setRuntimeState(ctx_, PlaybackState::Connecting, "play-live");
        ctx_->interruptRequested.store(true);
        const auto serial = ctx_->playSerial.fetch_add(1) + 1;
        clearFrameCache(ctx_);
        RL_LOGI("play live: serial=%llu %s", static_cast<unsigned long long>(serial), source.url.c_str());
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
    setRuntimeState(ctx_, PlaybackState::Connecting, "play-history");
    ctx_->interruptRequested.store(true);
    const auto serial = ctx_->playSerial.fetch_add(1) + 1;
    clearFrameCache(ctx_);
    RL_LOGI(
        "play history: serial=%llu %s start=%lld",
        static_cast<unsigned long long>(serial),
        source.url.c_str(),
        static_cast<long long>(source.startMs)
    );
    return true;
}

void FfmpegPlayer::seekTo(int64_t positionMs) {
    if (!ctx_) return;
    if (positionMs < 0) positionMs = 0;
    RL_LOGI("seek request: %lld", static_cast<long long>(positionMs));
    ctx_->pendingSeekMs.store(positionMs);
    ctx_->interruptRequested.store(true);
}

void FfmpegPlayer::stop() {
    if (!ctx_) return;
    RL_LOGI("stop request");
    {
        std::lock_guard<std::mutex> lock(ctx_->mutex);
        ctx_->mode = PlayMode::None;
        ctx_->historyStartMs = 0;
    }
    ctx_->playing.store(false);
    setRuntimeState(ctx_, PlaybackState::Idle, "stop");
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

    RL_LOGI("setSurface request newWindow=%p", static_cast<void*>(newWindow));
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
        } else {
            ctx_->loggedFirstViewport = false;
            const int winW = ANativeWindow_getWidth(ctx_->window);
            const int winH = ANativeWindow_getHeight(ctx_->window);
            RL_LOGI(
                "setSurface attached window=%p size=%dx%d",
                static_cast<void*>(ctx_->window),
                winW,
                winH
            );
            // Release ownership from caller thread; render thread binds before drawing.
            eglMakeCurrent(ctx_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
    } else {
        RL_LOGI("setSurface detached");
    }
}

PlayerStats FfmpegPlayer::stats() const {
    PlayerStats s{};
    if (!ctx_) return s;
    s.state = ctx_->runtimeState.load();

    const uint64_t decodedNow = ctx_->decodedFrameCount.load(std::memory_order_relaxed);
    const uint64_t renderedNow = ctx_->renderedFrameCount.load(std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(ctx_->statsMutex);
        if (ctx_->statsLastTs.time_since_epoch().count() != 0) {
            const auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx_->statsLastTs).count();
            if (dtMs > 0) {
                const double dt = static_cast<double>(dtMs) / 1000.0;
                ctx_->decodeFps = static_cast<double>(decodedNow - ctx_->statsLastDecoded) / dt;
                ctx_->renderFps = static_cast<double>(renderedNow - ctx_->statsLastRendered) / dt;
            }
        }
        ctx_->statsLastTs = now;
        ctx_->statsLastDecoded = decodedNow;
        ctx_->statsLastRendered = renderedNow;
        s.decodeFps = ctx_->decodeFps;
        s.renderFps = ctx_->renderFps;
    }

    std::lock_guard<std::mutex> lock(ctx_->frameMutex);
    s.videoWidth = ctx_->frameWidth;
    s.videoHeight = ctx_->frameHeight;
    s.bufferedFrames = ctx_->frameReady ? 1 : 0;
    const auto query = ctx_->statsQueryCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (query % 10 == 0) {
        RL_LOGI(
            "stats snapshot: state=%s size=%dx%d decodeFps=%.2f renderFps=%.2f queued=%llu rendered=%llu swaps=%llu",
            toString(s.state),
            s.videoWidth,
            s.videoHeight,
            s.decodeFps,
            s.renderFps,
            static_cast<unsigned long long>(ctx_->queuedFrameCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ctx_->renderedFrameCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ctx_->swapCount.load(std::memory_order_relaxed))
        );
    }
    return s;
}

}  // namespace reallive::player
