#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace DSP {
namespace Utils {

template <typename SampleType>
class ParameterSmoother
{
public:
    ParameterSmoother() = default;

    void prepare (double newSampleRate, double newSmoothingTimeMs)
    {
        jassert (newSampleRate > 0.0 && newSmoothingTimeMs >= 0.0);

        _sampleRate = newSampleRate;
        _smoothingTimeMs = newSmoothingTimeMs;

        if (_smoothingTimeMs > 0.0)
        {
            auto samplesForSmoothingTime = _smoothingTimeMs * 0.001 * _sampleRate;
            smoothingCoeff = static_cast<SampleType> (1.0 - std::exp (-1.0 / samplesForSmoothingTime));
        }
        else
        {
            smoothingCoeff = static_cast<SampleType> (1.0);
        }
    }

    void setTargetValue (SampleType newTargetValue)
    {
        targetValue = newTargetValue;
    }

    SampleType getNextValue()
    {
        currentValue += smoothingCoeff * (targetValue - currentValue);
        return currentValue;
    }

    void snapToTargetValue()
    {
        currentValue = targetValue;
    }

    SampleType getCurrentValue() const { return currentValue; }
    SampleType getTargetValue() const { return targetValue; }

    void reset (SampleType initialValue = SampleType { 0 })
    {
        currentValue = targetValue = initialValue;
    }

private:
    double _sampleRate = 44100.0;
    double _smoothingTimeMs = 0.0;
    SampleType smoothingCoeff = SampleType { 1 };
    SampleType currentValue = SampleType { 0 };
    SampleType targetValue = SampleType { 0 };
};

} // namespace Utils
} // namespace DSP
