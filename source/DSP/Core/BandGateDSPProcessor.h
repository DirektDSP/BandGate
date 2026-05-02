#pragma once

#include <array>
#include <type_traits>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "MultiBandLinkwitzRileySplitter.h"
#include "RelayDelayCore.h"
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
                          SampleType parallelGain = SampleType { 0.0 },
                          SampleType mix = SampleType { 100.0 },
                          int fftOrder = 11,
                          int numBands = 1,
                          const float* bandThresholdDb = nullptr,
                          const float* bandReductionDb = nullptr,
                          const float* bandSmoothingMs = nullptr,
                          const bool* bandFlip = nullptr,
                          const bool* bandSolo = nullptr,
                          const bool* bandMute = nullptr,
                          const float* crossoverHz = nullptr)
            {
                sampleRate = spec.sampleRate;
                samplesPerBlock = static_cast<int> (spec.maximumBlockSize);
                numChannels = static_cast<int> (spec.numChannels);

                activeNumBands = juce::jlimit (1, kMaxBands, numBands);
                copyBandParams (bandThresholdDb, bandReductionDb, bandSmoothingMs, bandFlip, bandSolo, bandMute, crossoverHz);

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
                                                                                  smoothMs[(size_t) b],
                                                                                  flipByBand[(size_t) b]);
                    }
                }

                recomputeBandBinRanges (fftOrder);
                prepareParameterSmoothers();

                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.snapToTargetValue();
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.snapToTargetValue();
                parallelGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (parallelGain));
                parallelGainSmoother.snapToTargetValue();
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.snapToTargetValue();

                dryBuffer.setSize (numChannels, samplesPerBlock);

                currentLatency = spectralGate[0][0].getLatencySamples();
                currentFFTOrder = fftOrder;
                lastActiveNumBands = activeNumBands;
                configureDryDelayCompensation();

                for (auto& r : relayBands)
                    r.prepare (spec);
            }

            void clearRelayFeedback()
            {
                for (auto& r : relayBands)
                    r.reset();
            }

            void updateRelayParameters (const std::array<RelayRuntimeParams, kMaxBands>& perBand) noexcept
            {
                for (size_t b = 0; b < relayBands.size(); ++b)
                    relayBands[b].setTargets (perBand[b]);
            }

            float getRelayRoundTripMsEstimate() const noexcept
            {
                const int nb = juce::jmax (1, activeNumBands);

                float s = 0.f;

                for (int b = 0; b < activeNumBands; ++b)
                    s += relayBands[(size_t) b].getEstimatedRoundTripMs();

                return s / float (nb);
            }

            void updateParameters (SampleType inputGainDb, SampleType outputGainDb,
                                   SampleType parallelGainDb,
                                   SampleType mixPercent, int fftOrder, int numBands,
                                   const float* bandThresholdDb, const float* bandReductionDb,
                                   const float* bandSmoothingMs, const bool* bandFlip,
                                   const bool* bandSolo, const bool* bandMute, const float* crossoverHz)
            {
                const int nb = juce::jlimit (1, kMaxBands, numBands);
                if (nb != lastActiveNumBands)
                {
                    lastActiveNumBands = nb;
                    splitter.reset();
                    for (int ch = 0; ch < 2; ++ch)
                        for (int b = 0; b < kMaxBands; ++b)
                            spectralGate[(size_t) ch][(size_t) b].reset();

                    for (auto& r : relayBands)
                        r.reset();
                }

                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGainDb));
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGainDb));
                parallelGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (parallelGainDb));
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mixPercent));

                activeNumBands = nb;
                copyBandParams (bandThresholdDb, bandReductionDb, bandSmoothingMs, bandFlip, bandSolo, bandMute, crossoverHz);

                for (int sp = 0; sp < activeNumBands - 1; ++sp)
                    splitter.setCutoff (sp, static_cast<SampleType> (crossoverSorted[(size_t) sp]));

                for (int ch = 0; ch < 2; ++ch)
                {
                    for (int b = 0; b < activeNumBands; ++b)
                    {
                        spectralGate[(size_t) ch][(size_t) b].updateParameters (thrDb[(size_t) b],
                                                                               redDb[(size_t) b],
                                                                               smoothMs[(size_t) b],
                                                                               flipByBand[(size_t) b]);
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
                                                                                      smoothMs[(size_t) b],
                                                                                      flipByBand[(size_t) b]);
                        }
                    }
                    currentLatency = spectralGate[0][0].getLatencySamples();
                    configureDryDelayCompensation();
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

                std::array<SampleType, kMaxBands> bandSampsL {};
                std::array<SampleType, kMaxBands> bandSampsR {};

                auto* wl = buffer.getNumChannels() >= 1 ? buffer.getWritePointer (0) : nullptr;
                auto* wr = buffer.getNumChannels() >= 2 ? buffer.getWritePointer (1) : nullptr;

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto inputGain = inputGainSmoother.getNextValue();
                    const auto parallelGain = parallelGainSmoother.getNextValue();
                    const auto mix = mixSmoother.getNextValue();
                    const auto outputGain = outputGainSmoother.getNextValue();
                    const auto effectiveInputGain = inputGain * parallelGain;
                    const auto effectiveOutputGain = outputGain / juce::jmax (parallelGain, SampleType { 1.0e-6f });

                    const int nCh = juce::jmin (buffer.getNumChannels(), 2);

                    splitter.processSample (0,
                                           wl[i] * effectiveInputGain,
                                           bandSampsL.data(),
                                           activeNumBands);

                    splitter.processSample (1,
                                           (nCh > 1 && wr != nullptr) ? wr[i] * effectiveInputGain
                                                                        : wl[i] * effectiveInputGain,
                                           bandSampsR.data(),
                                           activeNumBands);

                    bool anySoloActive = false;

                    for (int b = 0; b < activeNumBands; ++b)
                        anySoloActive = anySoloActive || soloByBand[(size_t) b];

                    float accLf = 0.f;
                    float accRf = 0.f;

                    for (int b = 0; b < activeNumBands; ++b)
                    {
                        const bool audible =
                            (! anySoloActive || soloByBand[(size_t) b]) && ! muteByBand[(size_t) b];

                        const float gatedL =
                            spectralGate[0][(size_t) b].processSample (static_cast<float> (bandSampsL[(size_t) b]));
                        float gatedR = gatedL;

                        if (nCh > 1 && wr != nullptr)
                            gatedR = spectralGate[1][(size_t) b]
                                         .processSample (static_cast<float> (bandSampsR[(size_t) b]));

                        if constexpr (std::is_same_v<SampleType, float>)
                        {
                            float obL {}, obR {};

                            if (relayBands[(size_t) b].isRelayEnabled())
                            {
                                relayBands[(size_t) b].processStereoSample (gatedL,
                                                                            gatedR,
                                                                            obL,
                                                                            obR);
                            }
                            else
                            {
                                obL = gatedL;
                                obR = gatedR;
                            }

                            if (audible)
                            {
                                accLf += obL;
                                accRf += obR;
                            }
                        }
                        else
                        {
                            if (audible)
                            {
                                accLf += static_cast<float> (gatedL);
                                accRf += static_cast<float> (gatedR);
                            }
                        }
                    }

                    SampleType relayL { static_cast<SampleType> (accLf) };
                    SampleType relayR { static_cast<SampleType> (accRf) };

                    for (int channel = 0; channel < nCh; ++channel)
                    {
                        auto* channelData = channel == 0 ? wl : wr;
                        jassert (channelData != nullptr);
                        const auto wetMixed = channel == 0 ? relayL : relayR;

                        const auto drySample = dryBuffer.getSample (channel, i);
                        const auto dryAligned = pushDryDelayAndGet (channel, static_cast<SampleType> (drySample));

                        channelData[i] = static_cast<SampleType> (
                            (static_cast<float> (dryAligned) * (1.0f - static_cast<float> (mix))
                             + static_cast<float> (wetMixed) * static_cast<float> (mix))
                            * static_cast<float> (effectiveOutputGain));
                    }
                }
            }

            void reset (SampleType inputGain = SampleType { 0.0 },
                        SampleType outputGain = SampleType { 0.0 },
                        SampleType parallelGain = SampleType { 0.0 },
                        SampleType mix = SampleType { 100.0 })
            {
                splitter.reset();
                for (int ch = 0; ch < 2; ++ch)
                    for (int b = 0; b < kMaxBands; ++b)
                        spectralGate[(size_t) ch][(size_t) b].reset();

                for (auto& r : relayBands)
                    r.reset();

                inputGainSmoother.reset (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (inputGain));
                inputGainSmoother.snapToTargetValue();
                outputGainSmoother.reset (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (outputGain));
                outputGainSmoother.snapToTargetValue();
                parallelGainSmoother.reset (Utils::DSPUtils::dbToGain (parallelGain));
                parallelGainSmoother.setTargetValue (Utils::DSPUtils::dbToGain (parallelGain));
                parallelGainSmoother.snapToTargetValue();
                mixSmoother.reset (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.setTargetValue (Utils::DSPUtils::percentageToNormalized (mix));
                mixSmoother.snapToTargetValue();
                resetDryDelayCompensation();
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
                                 const float* bandSmoothingMs, const bool* bandFlip,
                                 const bool* bandSolo, const bool* bandMute, const float* crossoverHz)
            {
                for (int i = 0; i < kMaxBands; ++i)
                {
                    thrDb[(size_t) i] = bandThresholdDb != nullptr ? bandThresholdDb[i] : -60.0f;
                    redDb[(size_t) i] = bandReductionDb != nullptr ? bandReductionDb[i] : -80.0f;
                    smoothMs[(size_t) i] = bandSmoothingMs != nullptr ? bandSmoothingMs[i] : 20.0f;
                    flipByBand[(size_t) i] = bandFlip != nullptr ? bandFlip[i] : false;
                    soloByBand[(size_t) i] = bandSolo != nullptr ? bandSolo[i] : false;
                    muteByBand[(size_t) i] = bandMute != nullptr ? bandMute[i] : false;
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
                parallelGainSmoother.prepare (sampleRate, 1.0);
                mixSmoother.prepare (sampleRate, 5.0);
            }

            void configureDryDelayCompensation()
            {
                dryDelaySamples = juce::jmax (0, currentLatency);
                const int needed = juce::jmax (1, dryDelaySamples + 1);
                for (auto& buf : dryDelayBuffers)
                    buf.assign ((size_t) needed, SampleType { 0 });
                dryDelayWritePos = 0;
            }

            void resetDryDelayCompensation()
            {
                for (auto& buf : dryDelayBuffers)
                    std::fill (buf.begin(), buf.end(), SampleType { 0 });
                dryDelayWritePos = 0;
            }

            SampleType pushDryDelayAndGet (int channel, SampleType inSample)
            {
                if (channel < 0 || channel >= (int) dryDelayBuffers.size())
                    return inSample;

                auto& buf = dryDelayBuffers[(size_t) channel];
                if (buf.empty())
                    return inSample;

                const int size = (int) buf.size();
                const int readPos = (dryDelayWritePos + size - dryDelaySamples) % size;
                const auto out = buf[(size_t) readPos];
                buf[(size_t) dryDelayWritePos] = inSample;

                if (channel == juce::jmin (numChannels, 2) - 1)
                    dryDelayWritePos = (dryDelayWritePos + 1) % size;

                return out;
            }

            MultiBandLinkwitzRileySplitter<SampleType> splitter;
            std::array<RelayDelayCore, kMaxBands> relayBands {};
            std::array<std::array<SpectralGate, kMaxBands>, 2> spectralGate {};

            Utils::ParameterSmoother<SampleType> inputGainSmoother;
            Utils::ParameterSmoother<SampleType> outputGainSmoother;
            Utils::ParameterSmoother<SampleType> parallelGainSmoother;
            Utils::ParameterSmoother<SampleType> mixSmoother;

            juce::AudioBuffer<SampleType> dryBuffer;

            double sampleRate = 44100.0;
            int samplesPerBlock = 512;
            int numChannels = 2;
            int currentLatency = 2048;
            int currentFFTOrder = 11;
            int activeNumBands = 1;

            std::array<float, kMaxBands> thrDb {};
            std::array<float, kMaxBands> redDb {};
            std::array<float, kMaxBands> smoothMs {};
            std::array<bool, kMaxBands> flipByBand {};
            std::array<bool, kMaxBands> soloByBand {};
            std::array<bool, kMaxBands> muteByBand {};
            std::array<float, kMaxBands - 1> crossoverSorted {};
            int lastActiveNumBands = -1;
            std::array<std::vector<SampleType>, 2> dryDelayBuffers {};
            int dryDelaySamples = 0;
            int dryDelayWritePos = 0;
        };

    } // namespace Core
} // namespace DSP
