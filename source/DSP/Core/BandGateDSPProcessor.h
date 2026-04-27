#pragma once

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "MultiBandLinkwitzRileySplitter.h"
#include "SpectralGate.h"
#include "../Utils/DSPUtils.h"
#include "../Utils/ParameterSmoother.h"

namespace DSP {
    namespace Core {

        template <typename SampleType>
        class BandGateDSPProcessor
        {
        public:
            static constexpr int kMaxBands = 6;

            BandGateDSPProcessor() = default;

            void prepare (const juce::dsp::ProcessSpec& spec,
                          SampleType inputGain = SampleType { 0.0 },
                          SampleType outputGain = SampleType { 0.0 },
                          SampleType mix = SampleType { 100.0 },
                          int fftOrder = 11,
                          int numBands = 2,
                          const float* bandThresholdDb = nullptr,
                          const float* bandReductionDb = nullptr,
                          const float* bandSmoothingMs = nullptr,
                          const float* crossoverHz = nullptr)
            {
                sampleRate = spec.sampleRate;
                samplesPerBlock = static_cast<int> (spec.maximumBlockSize);
                numChannels = static_cast<int> (spec.numChannels);

                activeNumBands = juce::jlimit (2, kMaxBands, numBands);
                copyBandParams (bandThresholdDb, bandReductionDb, bandSmoothingMs, crossoverHz);

                splitter.prepare (spec);

                for (int sp = 0; sp < activeNumBands - 1; ++sp)
                    splitter.setCutoff (sp, static_cast<SampleType> (crossoverSorted[(size_t) sp]));

                for (int ch = 0; ch < 2; ++ch)
                {
                    for (int b = 0; b < kMaxBands; ++b)
                    {
                        spectralGate[(size_t) ch][(size_t) b].prepare (sampleRate, samplesPerBlock, fftOrder);
                        if (b < activeNumBands)
                            spectralGate[(size_t) ch][(size_t) b].updateParameters (thrDb[(size_t) b],
                                                                                  redDb[(size_t) b],
                                                                                  smoothMs[(size_t) b]);
                    }
                }

                recomputeBandBinRanges (fftOrder);
                prepareParameterSmoothers();

                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.snapToTargetValue();
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.snapToTargetValue();
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.snapToTargetValue();

                dryBuffer.setSize (numChannels, samplesPerBlock);

                currentLatency = spectralGate[0][0].getLatencySamples();
                currentFFTOrder = fftOrder;
                lastActiveNumBands = activeNumBands;
            }

            void updateParameters (SampleType inputGainDb, SampleType outputGainDb,
                                   SampleType mixPercent, int fftOrder, int numBands,
                                   const float* bandThresholdDb, const float* bandReductionDb,
                                   const float* bandSmoothingMs, const float* crossoverHz)
            {
                const int nb = juce::jlimit (2, kMaxBands, numBands);
                if (nb != lastActiveNumBands)
                {
                    lastActiveNumBands = nb;
                    splitter.reset();
                    for (int ch = 0; ch < 2; ++ch)
                        for (int b = 0; b < kMaxBands; ++b)
                            spectralGate[(size_t) ch][(size_t) b].reset();
                }

                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGainDb));
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGainDb));
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mixPercent));

                activeNumBands = nb;
                copyBandParams (bandThresholdDb, bandReductionDb, bandSmoothingMs, crossoverHz);

                for (int sp = 0; sp < activeNumBands - 1; ++sp)
                    splitter.setCutoff (sp, static_cast<SampleType> (crossoverSorted[(size_t) sp]));

                for (int ch = 0; ch < 2; ++ch)
                {
                    for (int b = 0; b < activeNumBands; ++b)
                    {
                        spectralGate[(size_t) ch][(size_t) b].updateParameters (thrDb[(size_t) b],
                                                                               redDb[(size_t) b],
                                                                               smoothMs[(size_t) b]);
                    }
                }

                if (fftOrder != currentFFTOrder)
                {
                    currentFFTOrder = fftOrder;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        for (int b = 0; b < kMaxBands; ++b)
                        {
                            spectralGate[(size_t) ch][(size_t) b].setFFTOrder (fftOrder);
                            if (b < activeNumBands)
                                spectralGate[(size_t) ch][(size_t) b].updateParameters (thrDb[(size_t) b],
                                                                                      redDb[(size_t) b],
                                                                                      smoothMs[(size_t) b]);
                        }
                    }
                    currentLatency = spectralGate[0][0].getLatencySamples();
                }

                recomputeBandBinRanges (fftOrder);
            }

            void processBlock (juce::AudioBuffer<SampleType>& buffer)
            {
                jassert (buffer.getNumChannels() >= 1);

                const int numSamples = buffer.getNumSamples();

                if (dryBuffer.getNumSamples() != numSamples)
                    dryBuffer.setSize (numChannels, numSamples, false, false, true);

                dryBuffer.makeCopyOf (buffer);

                std::array<SampleType, kMaxBands> bandSamps {};

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto inputGain = inputGainSmoother.getNextValue();
                    const auto mix = mixSmoother.getNextValue();
                    const auto outputGain = outputGainSmoother.getNextValue();

                    const int nCh = juce::jmin (buffer.getNumChannels(), 2);
                    for (int channel = 0; channel < nCh; ++channel)
                    {
                        auto* channelData = buffer.getWritePointer (channel);
                        auto sample = channelData[i] * static_cast<float> (inputGain);

                        splitter.processSample (channel, static_cast<SampleType> (sample),
                                                bandSamps.data(), activeNumBands);

                        SampleType wetSum { 0.0 };
                        for (int b = 0; b < activeNumBands; ++b)
                            wetSum += static_cast<SampleType> (spectralGate[(size_t) channel][(size_t) b].processSample (static_cast<float> (bandSamps[(size_t) b])));

                        auto drySample = dryBuffer.getSample (channel, i);
                        channelData[i] = static_cast<SampleType> (
                            (static_cast<float> (drySample) * (1.0f - static_cast<float> (mix))
                             + static_cast<float> (wetSum) * static_cast<float> (mix))
                            * static_cast<float> (outputGain));
                    }
                }
            }

            void reset (SampleType inputGain = SampleType { 0.0 },
                        SampleType outputGain = SampleType { 0.0 },
                        SampleType mix = SampleType { 100.0 })
            {
                splitter.reset();
                for (int ch = 0; ch < 2; ++ch)
                    for (int b = 0; b < kMaxBands; ++b)
                        spectralGate[(size_t) ch][(size_t) b].reset();

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

            void fetchSpectralVisualData (std::vector<float>& magDbOut,
                                          std::vector<float>& gainOut,
                                          int& fftSizeOut,
                                          double& sampleRateOut) const
            {
                sampleRateOut = sampleRate;
                fftSizeOut = spectralGate[0][0].getFftSize();

                magDbOut.clear();
                gainOut.clear();

                for (int b = 0; b < activeNumBands; ++b)
                {
                    std::vector<float> mb, gb;
                    int nb = 0;
                    spectralGate[0][(size_t) b].copyVisualSnapshot (mb, gb, nb);
                    if (nb <= 0 || mb.empty())
                        continue;

                    if (numChannels > 1)
                    {
                        std::vector<float> m1, g1;
                        int n1 = 0;
                        spectralGate[1][(size_t) b].copyVisualSnapshot (m1, g1, n1);
                        const int use = juce::jmin (nb, n1);
                        for (int i = 0; i < use; ++i)
                        {
                            const float l0 = std::pow (10.0f, mb[(size_t) i] * 0.05f);
                            const float l1 = std::pow (10.0f, m1[(size_t) i] * 0.05f);
                            const float lavg = 0.5f * (l0 + l1);
                            mb[(size_t) i] = juce::Decibels::gainToDecibels (juce::jmax (lavg, 1.0e-12f), -120.0f);
                            gb[(size_t) i] = juce::jmin (gb[(size_t) i], g1[(size_t) i]);
                        }
                        nb = use;
                    }

                    if (magDbOut.empty())
                    {
                        magDbOut = std::move (mb);
                        gainOut = std::move (gb);
                    }
                    else
                    {
                        const int use = juce::jmin ((int) magDbOut.size(), nb);
                        for (int i = 0; i < use; ++i)
                        {
                            const float l0 = std::pow (10.0f, magDbOut[(size_t) i] * 0.05f);
                            const float l1 = std::pow (10.0f, mb[(size_t) i] * 0.05f);
                            const float lmax = juce::jmax (l0, l1);
                            magDbOut[(size_t) i] = juce::Decibels::gainToDecibels (juce::jmax (lmax, 1.0e-12f), -120.0f);
                            gainOut[(size_t) i] = juce::jmin (gainOut[(size_t) i], gb[(size_t) i]);
                        }
                    }
                }
            }

        private:
            void copyBandParams (const float* bandThresholdDb, const float* bandReductionDb,
                                 const float* bandSmoothingMs, const float* crossoverHz)
            {
                for (int i = 0; i < kMaxBands; ++i)
                {
                    thrDb[(size_t) i] = bandThresholdDb != nullptr ? bandThresholdDb[i] : -60.0f;
                    redDb[(size_t) i] = bandReductionDb != nullptr ? bandReductionDb[i] : -80.0f;
                    smoothMs[(size_t) i] = bandSmoothingMs != nullptr ? bandSmoothingMs[i] : 20.0f;
                }

                const int nCross = activeNumBands > 1 ? activeNumBands - 1 : 0;
                for (int i = 0; i < nCross; ++i)
                    crossoverSorted[(size_t) i] = crossoverHz != nullptr ? crossoverHz[i] : 1000.0f * (float) (i + 1);

                for (int a = 0; a < nCross - 1; ++a)
                    for (int b = 0; b < nCross - 1 - a; ++b)
                        if (crossoverSorted[(size_t) b] > crossoverSorted[(size_t) b + 1])
                            std::swap (crossoverSorted[(size_t) b], crossoverSorted[(size_t) b + 1]);
            }

            void recomputeBandBinRanges (int fftOrder)
            {
                const int fftSize = 1 << fftOrder;
                const int numBins = fftSize / 2 + 1;

                auto hzToInclusiveLastBin = [&] (float hz) -> int
                {
                    const int k = (int) std::floor ((double) hz * (double) fftSize / sampleRate);
                    return juce::jlimit (0, numBins - 1, k);
                };

                for (int ch = 0; ch < 2; ++ch)
                {
                    for (int b = 0; b < kMaxBands; ++b)
                    {
                        if (b >= activeNumBands || activeNumBands <= 1)
                        {
                            spectralGate[(size_t) ch][(size_t) b].setBandBinRange (0, numBins - 1);
                            continue;
                        }

                        if (b == 0)
                        {
                            const int last = hzToInclusiveLastBin (crossoverSorted[0]);
                            spectralGate[(size_t) ch][(size_t) b].setBandBinRange (0, juce::jmax (0, last));
                        }
                        else if (b == activeNumBands - 1)
                        {
                            const int first = hzToInclusiveLastBin (crossoverSorted[(size_t) (b - 1)]) + 1;
                            spectralGate[(size_t) ch][(size_t) b].setBandBinRange (juce::jmin (first, numBins - 1), numBins - 1);
                        }
                        else
                        {
                            const int lo = hzToInclusiveLastBin (crossoverSorted[(size_t) (b - 1)]) + 1;
                            const int hi = hzToInclusiveLastBin (crossoverSorted[(size_t) b]);
                            spectralGate[(size_t) ch][(size_t) b].setBandBinRange (juce::jmin (lo, hi), juce::jmax (lo, hi));
                        }
                    }
                }
            }

            void prepareParameterSmoothers()
            {
                inputGainSmoother.prepare (sampleRate, 1.0);
                outputGainSmoother.prepare (sampleRate, 1.0);
                mixSmoother.prepare (sampleRate, 5.0);
            }

            MultiBandLinkwitzRileySplitter<SampleType> splitter;
            std::array<std::array<SpectralGate, kMaxBands>, 2> spectralGate {};

            Utils::ParameterSmoother<SampleType> inputGainSmoother;
            Utils::ParameterSmoother<SampleType> outputGainSmoother;
            Utils::ParameterSmoother<SampleType> mixSmoother;

            juce::AudioBuffer<SampleType> dryBuffer;

            double sampleRate = 44100.0;
            int samplesPerBlock = 512;
            int numChannels = 2;
            int currentLatency = 2048;
            int currentFFTOrder = 11;
            int activeNumBands = 2;

            std::array<float, kMaxBands> thrDb {};
            std::array<float, kMaxBands> redDb {};
            std::array<float, kMaxBands> smoothMs {};
            std::array<float, kMaxBands - 1> crossoverSorted {};
            int lastActiveNumBands = -1;
        };

    } // namespace Core
} // namespace DSP
