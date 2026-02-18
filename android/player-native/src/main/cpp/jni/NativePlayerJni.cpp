#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include <string>

#include "player/core/PlayerController.h"

#define RL_JNI_TAG "RealLiveNativePlayerJNI"
#define RL_JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, RL_JNI_TAG, __VA_ARGS__)

namespace {

reallive::player::PlayerController* fromHandle(jlong handle) {
    return reinterpret_cast<reallive::player::PlayerController*>(handle);
}

std::string toUtf8(JNIEnv* env, jstring value) {
    if (!env || !value) return {};
    const char* cstr = env->GetStringUTFChars(value, nullptr);
    if (!cstr) return {};
    std::string out(cstr);
    env->ReleaseStringUTFChars(value, cstr);
    return out;
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_reallive_player_NativePlayer_nativeCreate(JNIEnv*, jobject) {
    auto* controller = new reallive::player::PlayerController();
    RL_JNI_LOGI("nativeCreate controller=%p", static_cast<void*>(controller));
    return reinterpret_cast<jlong>(controller);
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeSetSurface(JNIEnv* env, jobject, jlong handle, jobject surfaceObj) {
    auto* controller = fromHandle(handle);
    if (!controller) return;

    ANativeWindow* window = nullptr;
    if (surfaceObj) {
        window = ANativeWindow_fromSurface(env, surfaceObj);
    }

    const int width = window ? ANativeWindow_getWidth(window) : 0;
    const int height = window ? ANativeWindow_getHeight(window) : 0;
    RL_JNI_LOGI(
        "nativeSetSurface controller=%p window=%p size=%dx%d",
        static_cast<void*>(controller),
        static_cast<void*>(window),
        width,
        height
    );

    controller->setSurface(window);

    if (window) {
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativePlayLive(JNIEnv* env, jobject, jlong handle, jstring url) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    const auto u = toUtf8(env, url);
    RL_JNI_LOGI("nativePlayLive controller=%p url=%s", static_cast<void*>(controller), u.c_str());
    controller->playLive(u);
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativePlayHistory(JNIEnv* env, jobject, jlong handle, jstring url, jlong startMs) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    const auto u = toUtf8(env, url);
    RL_JNI_LOGI(
        "nativePlayHistory controller=%p startMs=%lld url=%s",
        static_cast<void*>(controller),
        static_cast<long long>(startMs),
        u.c_str()
    );
    controller->playHistory(u, static_cast<int64_t>(startMs));
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeSeek(JNIEnv*, jobject, jlong handle, jlong tsMs) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    RL_JNI_LOGI("nativeSeek controller=%p tsMs=%lld", static_cast<void*>(controller), static_cast<long long>(tsMs));
    controller->seekTo(static_cast<int64_t>(tsMs));
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeStop(JNIEnv*, jobject, jlong handle) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    RL_JNI_LOGI("nativeStop controller=%p", static_cast<void*>(controller));
    controller->stop();
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_reallive_player_NativePlayer_nativeGetStats(JNIEnv* env, jobject, jlong handle) {
    auto* controller = fromHandle(handle);
    reallive::player::PlayerStats stats{};
    if (controller) {
        stats = controller->stats();
    }
    const jdouble values[6] = {
        static_cast<jdouble>(stats.videoWidth),
        static_cast<jdouble>(stats.videoHeight),
        static_cast<jdouble>(stats.decodeFps),
        static_cast<jdouble>(stats.renderFps),
        static_cast<jdouble>(stats.bufferedFrames),
        static_cast<jdouble>(static_cast<int>(stats.state)),
    };
    jdoubleArray arr = env->NewDoubleArray(6);
    if (!arr) return nullptr;
    env->SetDoubleArrayRegion(arr, 0, 6, values);
    return arr;
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeRelease(JNIEnv*, jobject, jlong handle) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    RL_JNI_LOGI("nativeRelease controller=%p", static_cast<void*>(controller));
    delete controller;
}
