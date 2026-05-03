#include "DSP/Utils/ParameterSmoother.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using Catch::Approx;

TEST_CASE ("ParameterSmoother moves monotonically toward target", "[dsp][smoother]")
{
    DSP::Utils::ParameterSmoother<float> s;
    s.prepare (48000.0, 8.0);
    s.reset (0.f);
    s.setTargetValue (1.f);

    float v = 0.f;
    float prev = -1.f;

    for (int i = 0; i < 12'000; ++i)
    {
        v = s.getNextValue();
        REQUIRE (v >= prev - 1.0e-5f);
        REQUIRE (v <= 1.0001f);
        REQUIRE (std::isfinite (v));
        prev = v;
    }

    REQUIRE (v > 0.995f);
}

TEST_CASE ("ParameterSmoother snap holds current", "[dsp][smoother]")
{
    DSP::Utils::ParameterSmoother<float> s;
    s.prepare (44100.0, 10.0);
    s.setTargetValue (0.25f);
    s.snapToTargetValue();
    REQUIRE (s.getCurrentValue() == Approx (0.25f));
    REQUIRE (s.getNextValue() == Approx (0.25f));
}
