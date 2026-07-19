#include "media_engine.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mediaengine {

class OfflineBufferImpl : public OfflineBuffer {
public:
    bool open(const char* path, int* outSampleRate, int* outChannels, int64_t* outDurationUs) override {
        close();
        
        if (strncmp(path, "asset://", 8) == 0) {
            return openAsset(path + 8, outSampleRate, outChannels, outDurationUs);
        } else {
            return openFile(path, outSampleRate, outChannels, outDurationUs);
        }
    }
    
    int64_t read(float* buffer, int64_t maxFrames) override {
        if (!mFile && !mAsset) return -1;
        
        size_t bytesToRead = maxFrames * mChannels * sizeof(float);
        size_t bytesRead = 0;
        
        if (mFile) {
            bytesRead = fread(buffer, 1, bytesToRead, mFile.get());
        } else if (mAsset) {
            bytesRead = AAsset_read(mAsset, buffer, bytesToRead);
        }
        
        if (bytesRead == 0) return 0;
        
        int64_t framesRead = bytesRead / (mChannels * sizeof(float));
        mCurrentPositionUs += (framesRead * 1000000) / mSampleRate;
        return framesRead;
    }
    
    bool seek(int64_t positionUs) override {
        if (!mFile && !mAsset) return false;
        
        int64_t byteOffset = (positionUs * mSampleRate * mChannels * sizeof(float)) / 1000000;
        byteOffset = (byteOffset / (mChannels * sizeof(float))) * (mChannels * sizeof(float));
        
        if (mFile) {
            if (fseek(mFile.get(), byteOffset, SEEK_SET) != 0) return false;
        } else if (mAsset) {
            if (AAsset_seek(mAsset, byteOffset, SEEK_SET) < 0) return false;
        }
        
        mCurrentPositionUs = positionUs;
        return true;
    }
    
    void close() override {
        mFile.reset();
        if (mAsset) {
            AAsset_close(mAsset);
            mAsset = nullptr;
        }
        mSampleRate = 0;
        mChannels = 0;
        mDurationUs = 0;
        mCurrentPositionUs = 0;
    }
    
private:
    bool openFile(const char* path, int* outSampleRate, int* outChannels, int64_t* outDurationUs) {
        FILE* file = fopen(path, "rb");
        if (!file) return false;
        
        mFile.reset(file, fclose);
        mSampleRate = 48000;
        mChannels = 2;
        
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        mDurationUs = (fileSize * 1000000) / (mSampleRate * mChannels * sizeof(float));
        mCurrentPositionUs = 0;
        
        *outSampleRate = mSampleRate;
        *outChannels = mChannels;
        *outDurationUs = mDurationUs;
        return true;
    }
    
    bool openAsset(const char* assetPath, int* outSampleRate, int* outChannels, int64_t* outDurationUs) {
        extern JavaVM* gJavaVM;
        JNIEnv* env;
        gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
        
        jclass activityClass = env->FindClass("android/app/Activity");
        jmethodID getAssets = env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
        
        jobject context = getApplicationContext(env);
        jobject assetManager = env->CallObjectMethod(context, getAssets);
        
        mAsset = AAssetManager_open(AAssetManager_fromJava(env, assetManager), assetPath, AASSET_MODE_STREAMING);
        if (!mAsset) return false;
        
        off_t start, length;
        int fd = AAsset_openFileDescriptor(mAsset, &start, &length);
        if (fd >= 0) {
            mFile.reset(fdopen(fd, "rb"), fclose);
            lseek(fd, start, SEEK_SET);
        }
        
        mSampleRate = 48000;
        mChannels = 2;
        mDurationUs = (length * 1000000) / (mSampleRate * mChannels * sizeof(float));
        mCurrentPositionUs = 0;
        
        *outSampleRate = mSampleRate;
        *outChannels = mChannels;
        *outDurationUs = mDurationUs;
        return true;
    }
    
    jobject getApplicationContext(JNIEnv* env) {
        jclass activityThread = env->FindClass("android/app/ActivityThread");
        jmethodID currentApplication = env->GetStaticMethodID(activityThread, "currentApplication", "()Landroid/app/Application;");
        return env->CallStaticObjectMethod(activityThread, currentApplication);
    }
    
    std::unique_ptr<FILE, decltype(&fclose)> mFile{nullptr, fclose};
    AAsset* mAsset = nullptr;
    int mSampleRate = 0;
    int mChannels = 0;
    int64_t mDurationUs = 0;
    int64_t mCurrentPositionUs = 0;
};

} // namespace mediaengine