#pragma once

#include <array>
#include <climits>
#include <cstdint>
#include <limits>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "../Utils/ParameterSmoother.h"

namespace DSP {
namespace Core {

struct RelayRuntimeParams
{
    bool enabled = false;
    int timeMode = 0;
    float timeMs = 400.f;
    int syncDivIndex = 0;
    float feedback = 0.45f;
    float inputGainDb = 0.f;
    float mixPercent = 50.f;
    float diffusionMs = 45.f;
    float dampingPct = 35.f;
    float loopHpfHz = 95.f;
    float loopLpfHz = 12500.f;
    double hostBpm = 120.0;
    bool hostBpmValid = false;

    float ottAmountPct = 0.f;
    float ottTimeMs = 135.f;

    /** 0–100 %: scales new audio into delay summing node only (feedback unchanged). */
    float sendPercent = 100.f;
    /** 0–150 %: multiplier on feedback gain (100 % = unity). */
    float feedbackTrimPercent = 100.f;

    float flutterRateHz = 1.75f;
    float flutterDepthPct = 15.f;

    float chorusRateHz = 0.62f;
    float chorusDepthPct = 18.f;
};

/** Stereo relay delay: diffusion, damping, light OTT in loop, flutter, chorus on tap, guardrails. */
class RelayDelayCore
{
public:
    RelayDelayCore() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;

    void setTargets (const RelayRuntimeParams& p) noexcept;

    void processStereoSample (float inL, float inR, float& outL, float& outR) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared; }
    [[nodiscard]] bool isRelayEnabled() const noexcept { return prepared && lastRt.enabled; }
    [[nodiscard]] uint32_t getClearGeneration() const noexcept { return clearGeneration; }

    /** Display / UI: delay time + coarse diffuser smear (ms). */
    [[nodiscard]] float getEstimatedRoundTripMs() const noexcept { return cachedRoundTripMs; }

private:
    static constexpr int kApfStages = 6;
    static constexpr int kOttBands = 3;
    static constexpr float twoPi = 6.28318530718f;

    struct AllPassSection
    {
        std::vector<float> buf{};
        uint32_t w = 0;
        float baseG = 0.65f;

        void setLengthAndCoeff (int lengthSamples, float coeffG);
        void reset() noexcept;
        [[nodiscard]] float getBaseCoeff() const noexcept { return baseG; }
        float processSample (float x, float coeffApplied) noexcept;
    };

    float computeTargetDelayMs (const RelayRuntimeParams& p) const noexcept;
    float estimateDiffuseSmearSamples (float diffusionMsScaled) const noexcept;
    float computeDiffuseExtraMs() const noexcept;

    void updateDiffusionFromMs (float diffusionMsTarget, float channelDetuneSigned) noexcept;
    void rebuildDamping (float dampingPercent, float hpfHz, float lpfHz) noexcept;
    void rebuildOttCrossovers() noexcept;

    void updateOttTiming (float ottTimeMs) noexcept;
    [[nodiscard]] float ottMult (float env, float amtNorm) const noexcept;
    float processOtt3 (int channel, float xl, float xm, float xh, float ottAmtNorm) noexcept;

    [[nodiscard]] float feedbackGuard (float z) noexcept;
    void advanceModPhases() noexcept;
    float chorusProcessTap (int ch, float tapped) noexcept;

    void resetInternalBuffersOnly() noexcept;

    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 2> delay {};
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 2> chorusDl {};

    std::array<std::array<AllPassSection, kApfStages>, 2> diffuser {};

    std::array<juce::dsp::IIR::Filter<float>, 2> loopHpf;
    std::array<juce::dsp::IIR::Filter<float>, 2> loopLpf;
    std::array<juce::dsp::IIR::Filter<float>, 2> dampingA;
    std::array<juce::dsp::IIR::Filter<float>, 2> dampingB;

    std::array<juce::dsp::IIR::Filter<float>, 2> ottLowLp;
    std::array<juce::dsp::IIR::Filter<float>, 2> ottMidHp;
    std::array<juce::dsp::IIR::Filter<float>, 2> ottMidLp;
    std::array<juce::dsp::IIR::Filter<float>, 2> ottHighHp;

    std::array<std::array<float, kOttBands>, 2> ottEnv {};

    Utils::ParameterSmoother<float> delaySamplesSmooth;
    Utils::ParameterSmoother<float> feedbackSmooth;
    Utils::ParameterSmoother<float> relayMixSmooth;
    Utils::ParameterSmoother<float> injectorSmooth;
    Utils::ParameterSmoother<float> inputSendSmooth;
    Utils::ParameterSmoother<float> feedbackTrimSmooth;
    Utils::ParameterSmoother<float> ottAmountSmooth;

    float flutterPhase = 0.f;
    float chorusPhase = 0.f;
    float attackCoeff = 0.15f;
    float releaseCoeff = 0.02f;

    double sampleRate = 44100.0;
    float diffusionScaleTarget = 1.f;
    float cachedRoundTripMs = 0.f;

    int chorusCentreSamples[2] { 512, 520 };

    RelayRuntimeParams lastRt {};
    bool prepared = false;
    uint32_t clearGeneration = 0;
    uint32_t maxDelayAllocated = 4;

    float diffusionMsAppliedToken = std::numeric_limits<float>::infinity();
    int filterSetupFingerprint = INT32_MIN;

    [[nodiscard]] int computeFilterFingerprint (const RelayRuntimeParams& p) const noexcept;
};

} // namespace Core
} // namespace DSP
