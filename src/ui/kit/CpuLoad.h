#pragma once

// The CPU meter of the top bar: how much of the time a block lasts the plugin takes to process it.
// The processor owns one and measures each block with one line at the top of processBlock:
//
//     const bidoo::kit::CpuLoad::Scope cpu (cpuLoad, *this, buffer.getNumSamples());
//
// and the top bar reads it (getLoad, any thread).

#include <juce_audio_processors/juce_audio_processors.h>

namespace bidoo::kit
{
class CpuLoad
{
public:
    // Times the block it lives in. Audio thread; follows the sample rate and block size by itself.
    class Scope
    {
    public:
        Scope (CpuLoad& load, const juce::AudioProcessor& p, int numSamples)
            : owner (load.prepared (p.getSampleRate(), p.getBlockSize())), timer (owner.measurer, numSamples)
        {
        }

    private:
        CpuLoad& owner;
        juce::AudioProcessLoadMeasurer::ScopedTimer timer;
        JUCE_DECLARE_NON_COPYABLE (Scope)
    };

    // 0..1 (and more when the plugin is late), smoothed by JUCE's measurer.
    float getLoad() const { return (float) measurer.getLoadAsProportion(); }

private:
    CpuLoad& prepared (double sampleRate, int blockSize)
    {
        if (sampleRate > 0.0 && blockSize > 0 && (sampleRate != rate || blockSize != block))
        {
            rate = sampleRate;
            block = blockSize;
            measurer.reset (sampleRate, blockSize);
        }
        return *this;
    }

    juce::AudioProcessLoadMeasurer measurer;
    double rate = 0.0;
    int block = 0;
};
} // namespace bidoo::kit
