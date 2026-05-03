#include "DSP/Core/MultiBandLinkwitzRileySplitter.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using DSP::Core::MultiBandLinkwitzRileySplitter;

TEST_CASE ("MultiBandLinkwitzRileySplitter single band is passthrough", "[dsp][split]")
{
    juce::dsp::ProcessSpec spec { 48000.0, 256, 1 };
    MultiBandLinkwitzRileySplitter<float> sp;
    sp.prepare (spec);

    std::array<float, 8> bands {};

    for (int i = 0; i < 2'000; ++i)
    {
        const float x = 0.2f * std::sin (0.01f * (float) i);
        sp.processSample (0, x, bands.data(), 1);
        REQUIRE (bands[0] == x);
    }
}

TEST_CASE ("MultiBandLinkwitzRileySplitter multi band sums stay finite", "[dsp][split]")
{
    juce::dsp::ProcessSpec spec { 44100.0, 512, 1 };
    MultiBandLinkwitzRileySplitter<float> sp;
    sp.prepare (spec);
    sp.setCutoff (0, 900.f);
    sp.setCutoff (1, 2800.f);
    sp.setCutoff (2, 7000.f);

    std::array<float, 8> bands {};

    for (int i = 0; i < 8'000; ++i)
    {
        const float x = 0.25f * std::sin (0.007f * (float) i);
        sp.processSample (0, x, bands.data(), 4);
        float s = 0.f;

        for (int b = 0; b < 4; ++b)
        {
            REQUIRE (std::isfinite (bands[(size_t) b]));
            s += bands[(size_t) b];
        }

        REQUIRE (std::isfinite (s));
    }
}
