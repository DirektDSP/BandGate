#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SpectralGate.h"
#include "../Utils/DSPUtils.h"
#include "../Utils/ParameterSmoother.h"

namespace DSP {
    namespace Core {

        template <typename SampleType>
        class BandGateDSPProcessor
        {
        public:
            BandGateDSPProcessor() = default;

            void prepare (const juce::dsp::ProcessSpec& spec,
                          SampleType inputGain = SampleType { 0.0 },
                          SampleType outputGain = SampleType { 0.0 },
                          SampleType mix = SampleType { 100.0 },
                          SampleType threshold = SampleType { -60.0 },
                          SampleType reduction = SampleType { -80.0 },
                          SampleType smoothing = SampleType { 20.0 },
                          int fftOrder = 11)
            {
                sampleRate = spec.sampleRate;
                samplesPerBlock = static_cast<int> (spec.maximumBlockSize);
                numChannels = static_cast<int> (spec.numChannels);

                // Prepare spectral gates (one per channel)
                for (int ch = 0; ch < 2; ++ch)
                {
                    spectralGate[ch].prepare (sampleRate, samplesPerBlock, fftOrder);
                    spectralGate[ch].updateParameters (threshold, reduction, smoothing);
                }

                // Prepare smoothers
                prepareParameterSmoothers();

                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.snapToTargetValue();
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.snapToTargetValue();
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.snapToTargetValue();

                dryBuffer.setSize (numChannels, samplesPerBlock);

                currentLatency = spectralGate[0].getLatencySamples();
            }

            void updateParameters (SampleType inputGainDb, SampleType outputGainDb,
                                   SampleType mixPercent,
                                   SampleType thresholdDb, SampleType reductionDb,
                                   SampleType smoothingMs, int fftOrder)
            {
                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGainDb));
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGainDb));
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mixPercent));

                for (int ch = 0; ch < 2; ++ch)
                    spectralGate[ch].updateParameters (thresholdDb, reductionDb, smoothingMs);

                // Check if FFT order changed
                if (fftOrder != currentFFTOrder)
                {
                    currentFFTOrder = fftOrder;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        spectralGate[ch].setFFTOrder (fftOrder);
                        spectralGate[ch].updateParameters (thresholdDb, reductionDb, smoothingMs);
                    }
                    currentLatency = spectralGate[0].getLatencySamples();
                }
            }

            void processBlock (juce::AudioBuffer<SampleType>& buffer)
            {
                jassert (buffer.getNumChannels() >= 1);

                const int numSamples = buffer.getNumSamples();

                if (dryBuffer.getNumSamples() != numSamples)
                    dryBuffer.setSize (numChannels, numSamples, false, false, true);

                dryBuffer.makeCopyOf (buffer);

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto inputGain = inputGainSmoother.getNextValue();
                    const auto mix = mixSmoother.getNextValue();
                    const auto outputGain = outputGainSmoother.getNextValue();

                    for (int channel = 0; channel < juce::jmin (buffer.getNumChannels(), 2); ++channel)
                    {
                        auto* channelData = buffer.getWritePointer (channel);
                        auto sample = channelData[i] * static_cast<float> (inputGain);

                        // Process through spectral gate
                        auto wetSample = spectralGate[channel].processSample (sample);

                        // Mix dry/wet
                        auto drySample = dryBuffer.getSample (channel, i);
                        channelData[i] = static_cast<SampleType> (
                            (static_cast<float> (drySample) * (1.0f - static_cast<float> (mix))
                             + wetSample * static_cast<float> (mix))
                            * static_cast<float> (outputGain));
                    }
                }
            }

            void reset (SampleType inputGain = SampleType { 0.0 },
                        SampleType outputGain = SampleType { 0.0 },
                        SampleType mix = SampleType { 100.0 })
            {
                for (int ch = 0; ch < 2; ++ch)
                    spectralGate[ch].reset();

                inputGainSmoother.reset (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.snapToTargetValue();
                outputGainSmoother.reset (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.snapToTargetValue();
                mixSmoother.reset (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.snapToTargetValue();
            }

            int getLatencySamples() const { return currentLatency; }

        private:
            void prepareParameterSmoothers()
            {
                inputGainSmoother.prepare (sampleRate, 1.0);
                outputGainSmoother.prepare (sampleRate, 1.0);
                mixSmoother.prepare (sampleRate, 5.0);
            }

            // Spectral gate per channel
            SpectralGate spectralGate[2];

            // Parameter smoothers
            Utils::ParameterSmoother<SampleType> inputGainSmoother;
            Utils::ParameterSmoother<SampleType> outputGainSmoother;
            Utils::ParameterSmoother<SampleType> mixSmoother;

            juce::AudioBuffer<SampleType> dryBuffer;

            double sampleRate = 44100.0;
            int samplesPerBlock = 512;
            int numChannels = 2;
            int currentLatency = 2048;
            int currentFFTOrder = 11;
        };

    } // namespace Core
} // namespace DSP
