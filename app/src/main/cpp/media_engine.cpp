#include "media_engine.h"
#include "audio_processor.cpp"
#include "loudness_enhancer.cpp"
#include "offline_buffer.cpp"
#include <android/log.h>
#include <oboe/Oboe.h>
#include <memory>
#include <mutex>
#include <condition_variable>

#define LOG_TAG "MediaEngineNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mediaengine {

struct MediaEngine::Impl {
    std::unique_ptr<AudioProcessor> audioProcessor;
    std::unique_ptr<LoudnessEnhancer> loudnessEnhancer;
    std::unique_ptr<OfflineBuffer> offlineBuffer;
    std::unique_ptr<oboe::AudioStream> audioStream;
    
    int sampleRate = 48000;
    int channels = 2;
    int bufferSize = 1024;
    float volume = 1.0f;
    float playbackSpeed = 1.0f;
    bool isPlaying = false;
    bool isPrepared = false;
    int64_t currentPositionUs = 0;
    int64_t durationUs = 0;
    CompletionCallback callback = nullptr;
    void* callbackContext = nullptr;
    std::mutex mutex;
    std::condition_variable cv;
    std::string dataSource;
    
    void notifyEvent(int event, int arg1, int arg2) {
        if (callback) {
            callback(callbackContext, event, arg1, arg2);
        }
    }
};

MediaEngine& MediaEngine::getInstance() {
    static MediaEngine instance;
    return instance;
}

bool MediaEngine::initialize(int sampleRate, int channels, int bufferSize) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->sampleRate = sampleRate;
    pImpl->channels = channels;
    pImpl->bufferSize = bufferSize;
    
    pImpl->audioProcessor = std::make_unique<AudioProcessorImpl>();
    if (!pImpl->audioProcessor->initialize(sampleRate, channels, bufferSize)) {
        LOGE("Failed to initialize audio processor");
        return false;
    }
    
    pImpl->loudnessEnhancer = std::make_unique<LoudnessEnhancerImpl>();
    if (!pImpl->loudnessEnhancer->initialize(sampleRate, channels)) {
        LOGE("Failed to initialize loudness enhancer");
        return false;
    }
    
    pImpl->offlineBuffer = std::make_unique<OfflineBufferImpl>();
    
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           .setPerformanceMode(oboe::PerformanceMode::LowLatency)
           .setSharingMode(oboe::SharingMode::Shared)
           .setFormat(oboe::AudioFormat::Float)
           .setChannelCount(channels)
           .setSampleRate(sampleRate)
           .setCallback(oboe::DataCallbackResult::Continue)
           .setFramesPerCallback(bufferSize);
    
    oboe::Result result = builder.openStream(pImpl->audioStream);
    if (result != oboe::Result::OK) {
        LOGE("Failed to open audio stream: %s", oboe::convertToText(result));
        return false;
    }
    
    return true;
}

void MediaEngine::setDataSource(const char* path) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->dataSource = path;
    pImpl->isPrepared = false;
}

void MediaEngine::prepare() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    if (pImpl->dataSource.empty()) {
        LOGE("No data source set");
        return;
    }
    
    int sampleRate, channels;
    int64_t durationUs;
    if (!pImpl->offlineBuffer->open(pImpl->dataSource.c_str(), &sampleRate, &channels, &durationUs)) {
        LOGE("Failed to open media file: %s", pImpl->dataSource.c_str());
        notifyEvent(100, -1, 0); // ERROR
        return;
    }
    
    pImpl->durationUs = durationUs;
    pImpl->currentPositionUs = 0;
    pImpl->isPrepared = true;
    notifyEvent(101, 0, 0); // PREPARED
}

void MediaEngine::start() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    if (!pImpl->isPrepared || pImpl->isPlaying) return;
    
    pImpl->isPlaying = true;
    pImpl->audioStream->requestStart();
    notifyEvent(102, 0, 0); // STARTED
}

void MediaEngine::pause() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    if (!pImpl->isPlaying) return;
    
    pImpl->isPlaying = false;
    pImpl->audioStream->requestPause();
    notifyEvent(103, 0, 0); // PAUSED
}

void MediaEngine::stop() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->isPlaying = false;
    pImpl->audioStream->requestStop();
    pImpl->currentPositionUs = 0;
    pImpl->offlineBuffer->seek(0);
    notifyEvent(104, 0, 0); // STOPPED
}

void MediaEngine::seekTo(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    int64_t positionUs = positionMs * 1000;
    if (positionUs > pImpl->durationUs) positionUs = pImpl->durationUs;
    pImpl->offlineBuffer->seek(positionUs);
    pImpl->currentPositionUs = positionUs;
    notifyEvent(105, (int)(positionUs / 1000), 0); // SEEK_COMPLETE
}

int64_t MediaEngine::getCurrentPosition() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    return pImpl->currentPositionUs / 1000;
}

int64_t MediaEngine::getDuration() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    return pImpl->durationUs / 1000;
}

void MediaEngine::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->volume = std::clamp(volume, 0.0f, 2.0f);
}

void MediaEngine::setPlaybackSpeed(float speed) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->playbackSpeed = std::clamp(speed, 0.5f, 2.0f);
}

void MediaEngine::enableLoudnessEnhancer(bool enabled, float gainMillibels) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    if (pImpl->loudnessEnhancer) {
        pImpl->loudnessEnhancer->setTargetGain(gainMillibels);
        pImpl->loudnessEnhancer->setEnabled(enabled);
    }
}

void MediaEngine::release() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->isPlaying = false;
    if (pImpl->audioStream) {
        pImpl->audioStream->close();
    }
    if (pImpl->audioProcessor) {
        pImpl->audioProcessor->release();
    }
    if (pImpl->loudnessEnhancer) {
        pImpl->loudnessEnhancer->release();
    }
    if (pImpl->offlineBuffer) {
        pImpl->offlineBuffer->close();
    }
}

void MediaEngine::setCallback(CompletionCallback callback, void* context) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->callback = callback;
    pImpl->callbackContext = context;
}

} // namespace mediaengine

// JNI Implementation
extern "C" {

JNIEXPORT jlong JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new mediaengine::MediaEngine());
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<mediaengine::MediaEngine*>(handle);
}

JNIEXPORT jboolean JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeInitialize(JNIEnv*, jobject, jlong handle, jint sampleRate, jint channels, jint bufferSize) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    return engine->initialize(sampleRate, channels, bufferSize) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSetDataSource(JNIEnv* env, jobject, jlong handle, jstring path) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    const char* nativePath = env->GetStringUTFChars(path, nullptr);
    engine->setDataSource(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativePrepare(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->prepare();
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeStart(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->start();
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativePause(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->pause();
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeStop(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->stop();
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSeekTo(JNIEnv*, jobject, jlong handle, jlong positionMs) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->seekTo(positionMs);
}

JNIEXPORT jlong JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeGetCurrentPosition(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    return engine->getCurrentPosition();
}

JNIEXPORT jlong JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeGetDuration(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    return engine->getDuration();
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSetVolume(JNIEnv*, jobject, jlong handle, jfloat volume) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->setVolume(volume);
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeSetPlaybackSpeed(JNIEnv*, jobject, jlong handle, jfloat speed) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->setPlaybackSpeed(speed);
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeEnableLoudnessEnhancer(JNIEnv*, jobject, jlong handle, jboolean enabled, jfloat gainMillibels) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->enableLoudnessEnhancer(enabled, gainMillibels);
}

JNIEXPORT void JNICALL Java_com_example_mediaframework_engine_MediaEngine_nativeRelease(JNIEnv*, jobject, jlong handle) {
    auto* engine = reinterpret_cast<mediaengine::MediaEngine*>(handle);
    engine->release();
}

} // extern "C"