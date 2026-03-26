#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace DSP {
namespace Utils {

class DSPUtils
{
public:
    static inline float dbToGain (float db)
    {
        return std::pow (10.0f, db * 0.05f);
    }

    static inline float gainToDb (float gain)
    {
        return 20.0f * std::log10 (std::max (gain, 1e-6f));
    }

    static inline float percentageToNormalized (float percentage)
    {
        return juce::jlimit (0.0f, 1.0f, percentage * 0.01f);
    }

    static inline float normalizedToPercentage (float normalized)
    {
        return juce::jlimit (0.0f, 100.0f, normalized * 100.0f);
    }

    static inline float softClip (float input)
    {
        return std::tanh (input);
    }

    static inline float hardClip (float input, float threshold = 1.0f)
    {
        return juce::jlimit (-threshold, threshold, input);
    }

    static inline float lerp (float a, float b, float t)
    {
        return a + t * (b - a);
    }

    static inline float flushDenormalToZero (float input)
    {
        return std::abs (input) < 1e-30f ? 0.0f : input;
    }
};

} // namespace Utils
} // namespace DSP
