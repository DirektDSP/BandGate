#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace DSP {
    namespace Core {

        class SpectralGate
        {
        public:
            SpectralGate() = default;

            void prepare (double newSampleRate, int maxBlockSize, int newFFTOrder = 11)
            {
                sampleRate = newSampleRate;
                setFFTOrder (newFFTOrder);
            }

            void setFFTOrder (int newOrder)
            {
                fftOrder = newOrder;
                fftSize = 1 << fftOrder;
                hopSize = fftSize / 4; // 75% overlap
                numBins = fftSize / 2 + 1;
                bandFirstBin = 0;
                bandLastBin = numBins - 1;

                fft = std::make_unique<juce::dsp::FFT> (fftOrder);

                // Hann window
                window.resize (fftSize);
                for (int i = 0; i < fftSize; ++i)
                    window[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * i / (float) fftSize));

                // Working buffers
                fftWorkBuffer.resize (fftSize * 2, 0.0f);
                prevGain.resize (numBins, 0.0f);

                // Ring buffer for incoming samples
                inputRing.resize (fftSize, 0.0f);
                inputWritePos = 0;

                // Output overlap-add accumulator (double length for safe overlap)
                outputAccum.resize (fftSize + hopSize, 0.0f);
                outputReadPos = 0;
                samplesUntilNextFrame = hopSize;

                // Window compensation: with 75% overlap Hann squared, the sum is 1.5
                overlapScale = 1.0f / 1.5f;

                {
                    const juce::ScopedLock sl (vizLock);
                    vizMagDb.assign ((size_t) numBins, -120.0f);
                    vizGain.assign ((size_t) numBins, 1.0f);
                    vizScratchMag.resize ((size_t) numBins);
                    vizScratchGain.resize ((size_t) numBins);
                }
            }

            void updateParameters (float thresholdDb, float reductionDb, float smoothing)
            {
                thresholdLinear = std::pow (10.0f, thresholdDb / 20.0f);
                reductionLinear = std::pow (10.0f, reductionDb / 20.0f);

                if (smoothing > 0.0f && sampleRate > 0.0)
                {
                    float smoothingFrames = (smoothing * 0.001f * (float) sampleRate) / (float) hopSize;
                    smoothCoeff = std::exp (-1.0f / std::max (smoothingFrames, 1.0f));
                }
                else
                {
                    smoothCoeff = 0.0f;
                }
            }

            /** Inclusive FFT bin indices where gating applies (per multiband split). Full spectrum: 0 .. numBins-1. */
            void setBandBinRange (int inclusiveFirstBin, int inclusiveLastBin)
            {
                bandFirstBin = juce::jlimit (0, juce::jmax (0, numBins - 1), inclusiveFirstBin);
                bandLastBin = juce::jlimit (bandFirstBin, juce::jmax (0, numBins - 1), inclusiveLastBin);
            }

            float processSample (float inputSample)
            {
                // Write input to ring buffer
                inputRing[inputWritePos] = inputSample;
                inputWritePos = (inputWritePos + 1) % fftSize;

                // Read from output accumulator
                float outputSample = outputAccum[outputReadPos] * overlapScale;
                outputAccum[outputReadPos] = 0.0f;
                outputReadPos = (outputReadPos + 1) % (int) outputAccum.size();

                samplesUntilNextFrame--;
                if (samplesUntilNextFrame <= 0)
                {
                    processFFTFrame();
                    samplesUntilNextFrame = hopSize;
                }

                return outputSample;
            }

            void reset()
            {
                std::fill (inputRing.begin(), inputRing.end(), 0.0f);
                std::fill (outputAccum.begin(), outputAccum.end(), 0.0f);
                std::fill (prevGain.begin(), prevGain.end(), 0.0f);
                inputWritePos = 0;
                outputReadPos = 0;
                samplesUntilNextFrame = hopSize;
            }

            int getLatencySamples() const { return fftSize; }

            int getFftSize() const noexcept { return fftSize; }

            double getSampleRate() const noexcept { return sampleRate; }

            /** Thread-safe copy for UI (message thread holds lock; audio uses tryLock). */
            void copyVisualSnapshot (std::vector<float>& magDbOut,
                                     std::vector<float>& gainOut,
                                     int& numBinsOut) const
            {
                const juce::ScopedLock sl (vizLock);
                if ((int) vizMagDb.size() != numBins || (int) vizGain.size() != numBins)
                {
                    numBinsOut = 0;
                    magDbOut.clear();
                    gainOut.clear();
                    return;
                }

                numBinsOut = numBins;
                magDbOut = vizMagDb;
                gainOut = vizGain;
            }

        private:
            void processFFTFrame()
            {
                // Read fftSize samples from ring buffer ending at current write position
                // and apply analysis window
                for (int i = 0; i < fftSize; ++i)
                {
                    int readIdx = (inputWritePos - fftSize + i + (int) inputRing.size()) % (int) inputRing.size();
                    fftWorkBuffer[i] = inputRing[readIdx] * window[i];
                }

                // Zero imaginary part
                for (int i = fftSize; i < fftSize * 2; ++i)
                    fftWorkBuffer[i] = 0.0f;

                // Forward FFT
                fft->performRealOnlyForwardTransform (fftWorkBuffer.data(), true);

                // Spectral gating: attenuate bins below threshold (only inside band bin range)
                for (int bin = 0; bin < numBins; ++bin)
                {
                    float real = fftWorkBuffer[bin * 2];
                    float imag = fftWorkBuffer[bin * 2 + 1];
                    float magnitude = std::sqrt (real * real + imag * imag);

                    if (bin < bandFirstBin || bin > bandLastBin)
                    {
                        prevGain[(size_t) bin] = 1.0f;
                        vizScratchMag[(size_t) bin] = juce::Decibels::gainToDecibels (juce::jmax (magnitude, 1.0e-9f), -120.0f);
                        vizScratchGain[(size_t) bin] = 1.0f;
                        continue;
                    }

                    float targetGain = (magnitude >= thresholdLinear) ? 1.0f : reductionLinear;

                    // Temporal smoothing to reduce musical noise
                    float gain;
                    if (smoothCoeff > 0.0f)
                    {
                        gain = smoothCoeff * prevGain[(size_t) bin] + (1.0f - smoothCoeff) * targetGain;
                        prevGain[(size_t) bin] = gain;
                    }
                    else
                    {
                        gain = targetGain;
                        prevGain[(size_t) bin] = gain;
                    }

                    fftWorkBuffer[bin * 2] *= gain;
                    fftWorkBuffer[bin * 2 + 1] *= gain;

                    vizScratchMag[(size_t) bin] = juce::Decibels::gainToDecibels (juce::jmax (magnitude, 1.0e-9f), -120.0f);
                    vizScratchGain[(size_t) bin] = gain;
                }

                if (const juce::ScopedTryLock vizTry (vizLock); vizTry.isLocked())
                {
                    vizMagDb.swap (vizScratchMag);
                    vizGain.swap (vizScratchGain);
                }

                // Inverse FFT
                fft->performRealOnlyInverseTransform (fftWorkBuffer.data());

                // Apply synthesis window and overlap-add into output accumulator
                for (int i = 0; i < fftSize; ++i)
                {
                    int writeIdx = (outputReadPos + i) % (int) outputAccum.size();
                    outputAccum[writeIdx] += fftWorkBuffer[i] * window[i];
                }
            }

            std::unique_ptr<juce::dsp::FFT> fft;

            int fftOrder = 11;
            int fftSize = 2048;
            int hopSize = 512;
            int numBins = 1025;
            double sampleRate = 44100.0;

            float thresholdLinear = 0.0f;
            float reductionLinear = 0.0f;
            float smoothCoeff = 0.0f;
            float overlapScale = 1.0f / 1.5f;

            int bandFirstBin = 0;
            int bandLastBin = 1024;

            std::vector<float> window;
            std::vector<float> fftWorkBuffer;
            std::vector<float> prevGain;

            // Ring buffer for input
            std::vector<float> inputRing;
            int inputWritePos = 0;

            // Overlap-add output accumulator (circular)
            std::vector<float> outputAccum;
            int outputReadPos = 0;
            int samplesUntilNextFrame = 0;

            mutable juce::CriticalSection vizLock;
            mutable std::vector<float> vizMagDb;
            mutable std::vector<float> vizGain;
            std::vector<float> vizScratchMag;
            std::vector<float> vizScratchGain;
        };

    } // namespace Core
} // namespace DSP
