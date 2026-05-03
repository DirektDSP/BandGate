#include "DSP/Core/SpectralGate.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using DSP::Core::SpectralGate;

TEST_CASE ("SpectralGate latency matches FFT size", "[dsp][gate]")
{
    SpectralGate g;
    g.prepare (44100.0, 512, 10);
    REQUIRE (g.getLatencySamples() == (1 << 10));
    REQUIRE (g.getFftSize() == (1 << 10));
}

TEST_CASE ("SpectralGate processSample output stays finite", "[dsp][gate]")
{
    SpectralGate g;
    g.prepare (48000.0, 256, 9);
    g.updateParameters (-50.f, -60.f, 15.f, false);

    for (int i = 0; i < 50'000; ++i)
    {
        const float in = 0.15f * std::sin (0.02f * (float) i);
        const float out = g.processSample (in);
        REQUIRE (std::isfinite (out));
    }
}

TEST_CASE ("SpectralGate reset clears to stable zero tail", "[dsp][gate]")
{
    SpectralGate g;
    g.prepare (44100.0, 512, 8);
    g.updateParameters (-40.f, -40.f, 5.f, false);

    for (int i = 0; i < 10'000; ++i)
        (void) g.processSample (0.5f * std::sin (0.003f * (float) i));

    g.reset();

    for (int i = 0; i < 5'000; ++i)
    {
        const float o = g.processSample (0.f);
        REQUIRE (std::isfinite (o));
    }
}
