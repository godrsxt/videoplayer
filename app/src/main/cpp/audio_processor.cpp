#include "media_engine.h"
#include <algorithm>
#include <cmath>

namespace mediaengine {

class AudioProcessorImpl : public AudioProcessor {
public:
    bool initialize(int sampleRate, int channels, int bufferSize) override {
        mSampleRate = sampleRate;
        mChannels = channels;
        mBufferSize = bufferSize;
        mTempBuffer.resize(bufferSize * channels);
        return true;
    }
    
    void process(float* input, float* output, size_t frames) override {
        const size_t totalSamples = frames * mChannels;
        for (size_t i = 0; i < totalSamples; ++i) {
            float sample = input[i] * mGain;
            sample = std::tanh(sample * 0.5f) * 2.0f;
            output[i] = std::clamp(sample, -1.0f, 1.0f);
        }
    }
    
    void release() override {
        mTempBuffer.clear();
    }
    
    void setGain(float gain) { mGain = gain; }
    
private:
    int mSampleRate = 48000;
    int mChannels = 2;
    int mBufferSize = 1024;
    float mGain = 1.0f;
    std::vector<float> mTempBuffer;
};

} // namespace mediaengine