#include "media_engine.h"
#include <algorithm>
#include <cmath>

namespace mediaengine {

class LoudnessEnhancerImpl : public LoudnessEnhancer {
public:
    bool initialize(int sampleRate, int channels) override {
        mSampleRate = sampleRate;
        mChannels = channels;
        mEnabled = false;
        mTargetGainDb = 0.0f;
        mCurrentGainDb = 0.0f;
        mSmoothingFactor = 0.001f;
        initializeFilters();
        return true;
    }
    
    void setTargetGain(float gainMillibels) override {
        mTargetGainDb = gainMillibels / 1000.0f;
        mTargetGainDb = std::clamp(mTargetGainDb, -20.0f, 20.0f);
    }
    
    void setEnabled(bool enabled) override {
        mEnabled = enabled;
        if (!enabled) {
            mCurrentGainDb = 0.0f;
        }
    }
    
    void process(float* buffer, size_t frames) override {
        if (!mEnabled) return;
        
        mCurrentGainDb += (mTargetGainDb - mCurrentGainDb) * mSmoothingFactor;
        float linearGain = std::pow(10.0f, mCurrentGainDb / 20.0f);
        
        for (size_t i = 0; i < frames * mChannels; ++i) {
            float sample = buffer[i];
            
            sample = applyDynamicRangeCompression(sample);
            sample = applySaturation(sample);
            sample *= linearGain;
            
            buffer[i] = std::clamp(sample, -1.0f, 1.0f);
        }
    }
    
    void release() override {}
    
private:
    void initializeFilters() {
        float wc = 2.0f * M_PI * 100.0f / mSampleRate;
        mLowpassCoeff = wc / (1.0f + wc);
        mEnvelope = 0.0f;
    }
    
    float applyDynamicRangeCompression(float sample) {
        float absSample = std::abs(sample);
        mEnvelope = mLowpassCoeff * absSample + (1.0f - mLowpassCoeff) * mEnvelope;
        
        float threshold = 0.3f;
        float ratio = 4.0f;
        float knee = 0.1f;
        
        if (mEnvelope > threshold) {
            float excess = mEnvelope - threshold;
            float compressedExcess = excess / ratio;
            float gainReduction = (threshold + compressedExcess) / mEnvelope;
            return sample * gainReduction;
        }
        return sample;
    }
    
    float applySaturation(float sample) {
        float drive = 1.5f;
        sample = std::tanh(sample * drive) / std::tanh(drive);
        return sample;
    }
    
    int mSampleRate = 48000;
    int mChannels = 2;
    bool mEnabled = false;
    float mTargetGainDb = 0.0f;
    float mCurrentGainDb = 0.0f;
    float mSmoothingFactor = 0.001f;
    float mLowpassCoeff = 0.0f;
    float mEnvelope = 0.0f;
};

} // namespace mediaengine