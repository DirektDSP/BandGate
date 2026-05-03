#include "DSP/Core/RelayDelayCore.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace DSP::Core;

TEST_CASE ("RelayDelayCore prepare sets prepared", "[relay]")
{
    RelayDelayCore core;
    REQUIRE_FALSE (core.isPrepared());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    core.prepare (spec);
    REQUIRE (core.isPrepared());
}

TEST_CASE ("RelayDelayCore reset bumps clear generation", "[relay]")
{
    RelayDelayCore core;
    REQUIRE (core.getClearGeneration() == 0);

    juce::dsp::ProcessSpec spec { 48000.0, 256, 2 };
    core.prepare (spec);

    REQUIRE (core.getClearGeneration() == 0);
    core.reset();
    REQUIRE (core.getClearGeneration() == 1);
    core.reset();
    REQUIRE (core.getClearGeneration() == 2);
}

TEST_CASE ("RelayDelayCore prepare does not bump clear generation", "[relay]")
{
    RelayDelayCore core;
    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };

    core.prepare (spec);
    const auto g = core.getClearGeneration();
    core.prepare (spec);
    REQUIRE (core.getClearGeneration() == g);
}

TEST_CASE ("RelayDelayCore isRelayEnabled tracks setTargets", "[relay]")
{
    RelayDelayCore core;
    core.prepare ({ 48000.0, 256, 2 });
    REQUIRE_FALSE (core.isRelayEnabled());

    RelayRuntimeParams on {};
    on.enabled = true;
    core.setTargets (on);
    REQUIRE (core.isRelayEnabled());

    RelayRuntimeParams off {};
    off.enabled = false;
    core.setTargets (off);
    REQUIRE_FALSE (core.isRelayEnabled());
}

TEST_CASE ("RelayDelayCore sync mode lengthens delay vs shorter free time", "[relay]")
{
    RelayDelayCore core;
    core.prepare ({ 48000.0, 256, 2 });

    RelayRuntimeParams freeMode {};
    freeMode.enabled = true;
    freeMode.timeMode = 0;
    freeMode.timeMs = 333.f;
    core.setTargets (freeMode);
    const float rtFree = core.getEstimatedRoundTripMs();

    RelayRuntimeParams syncMode = freeMode;
    syncMode.timeMode = 1;
    syncMode.syncDivIndex = 5; // 1 beat in syncDividerBeats table
    syncMode.hostBpm = 120.0;
    syncMode.hostBpmValid = true;
    core.setTargets (syncMode);
    const float rtSync = core.getEstimatedRoundTripMs();

    // 120 BPM → 500 ms/beat; should read longer RT than 333 ms free (+ same-ish diffusion tail).
    REQUIRE (rtSync > rtFree + 50.f);
}

TEST_CASE ("RelayDelayCore high feedback output stays finite", "[relay]")
{
    RelayDelayCore core;
    core.prepare ({ 44100.0, 64, 2 });

    RelayRuntimeParams p {};
    p.enabled = true;
    p.feedback = 1.55f;
    p.feedbackTrimPercent = 150.f;
    p.mixPercent = 100.f;
    p.timeMs = 80.f;
    p.ottAmountPct = 80.f;
    core.setTargets (p);

    for (int i = 0; i < 60'000; ++i)
    {
        float ol {}, or_{};
        const float in = 0.02f * std::sin (0.0007f * (float) i);
        core.processStereoSample (in, -in * 0.5f, ol, or_);
        REQUIRE (std::isfinite (ol));
        REQUIRE (std::isfinite (or_));
    }
}
