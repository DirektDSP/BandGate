#include "DSP/Core/RelayDelayCore.h"
#include <catch2/catch_test_macros.hpp>

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
