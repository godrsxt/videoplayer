#ifndef MEDIA_ENGINE_H
#define MEDIA_ENGINE_H

#include <jni.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace mediaengine {

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual bool initialize(int sampleRate, int channels, int bufferSize) = 0;
    virtual void process(float* input, float* output, size_t frames) = 0;
    virtual void release() = 0;
};

class LoudnessEnhancer {
public:
    virtual ~LoudnessEnhancer() = default;
    virtual bool initialize(int sampleRate, int channels) = 0;
    virtual void setTargetGain(float gainMillibels) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual void process(float* buffer, size_t frames) = 0;
    virtual void release() = 0;
};

class OfflineBuffer {
public:
    virtual ~OfflineBuffer() = default;
    virtual bool open(const char* path, int* outSampleRate, int* outChannels, int64_t* outDurationUs) = 0;
    virtual int64_t read(float* buffer, int64_t maxFrames) = 0;
    virtual bool seek(int64_t positionUs) = 0;
    virtual void close() = 0;
};

class MediaEngine {
public:
    static MediaEngine& getInstance();
    
    bool initialize(int sampleRate, int channels, int bufferSize);
    void setDataSource(const char* path);
    void prepare();
    void start();
    void pause();
    void stop();
    void seekTo(int64_t positionMs);
    int64_t getCurrentPosition();
    int64_t getDuration();
    void setVolume(float volume);
    void setPlaybackSpeed(float speed);
    void enableLoudnessEnhancer(bool enabled, float gainMillibels);
    void release();
    
    // Callback interface for Java
    using CompletionCallback = void(*)(void* context, int event, int arg1, int arg2);
    void setCallback(CompletionCallback callback, void* context);

private:
    MediaEngine() = default;
    ~MediaEngine() = default;
    MediaEngine(const MediaEngine&) = delete;
    MediaEngine& operator=(const MediaEngine&) = delete;
    
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace mediaengine

extern "C" {

JNIEXPORT jlong JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeCreate(JNIEnv*, jobject);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeDestroy(JNIEnv*, jobject, jlong);
JNIEXPORT jboolean JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeInitialize(JNIEnv*, jobject, jlong, jint, jint, jint);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSetDataSource(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativePrepare(JNIEnv*, jobject, jlong);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeStart(JNIEnv*, jobject, jlong);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativePause(JNIEnv*, jobject, jlong);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeStop(JNIEnv*, jobject, jlong);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSeekTo(JNIEnv*, jobject, jlong, jlong);
JNIEXPORT jlong JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeGetCurrentPosition(JNIEnv*, jobject, jlong);
JNIEXPORT jlong JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeGetDuration(JNIEnv*, jobject, jlong);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSetVolume(JNIEnv*, jobject, jlong, jfloat);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSetPlaybackSpeed(JNIEnv*, jobject, jlong, jfloat);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeEnableLoudnessEnhancer(JNIEnv*, jobject, jlong, jboolean, jfloat);
JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeRelease(JNIEnv*, jobject, jlong);

} // extern "C"

#endif // MEDIA_ENGINE_H