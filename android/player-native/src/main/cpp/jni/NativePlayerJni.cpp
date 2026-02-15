#include <jni.h>
#include <android/native_window_jni.h>

#include <string>

#include "player/core/PlayerController.h"

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

    controller->setSurface(window);

    if (window) {
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativePlayLive(JNIEnv* env, jobject, jlong handle, jstring url) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    controller->playLive(toUtf8(env, url));
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativePlayHistory(JNIEnv* env, jobject, jlong handle, jstring url, jlong startMs) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    controller->playHistory(toUtf8(env, url), static_cast<int64_t>(startMs));
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeSeek(JNIEnv*, jobject, jlong handle, jlong tsMs) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    controller->seekTo(static_cast<int64_t>(tsMs));
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeStop(JNIEnv*, jobject, jlong handle) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    controller->stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_reallive_player_NativePlayer_nativeRelease(JNIEnv*, jobject, jlong handle) {
    auto* controller = fromHandle(handle);
    if (!controller) return;
    delete controller;
}
