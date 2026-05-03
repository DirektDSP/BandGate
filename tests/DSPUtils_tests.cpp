#include "DSP/Utils/DSPUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using DSP::Utils::DSPUtils;

TEST_CASE ("DSPUtils dbToGain unity at 0 dB", "[dsp][utils]")
{
    REQUIRE (DSPUtils::dbToGain (0.f) == Approx (1.f).margin (1.e-5f));
}

TEST_CASE ("DSPUtils percentage normalized round-trip", "[dsp][utils]")
{
    const float pct = 37.5f;
    const float n = DSPUtils::percentageToNormalized (pct);
    REQUIRE (n == Approx (0.375f));
    REQUIRE (DSPUtils::normalizedToPercentage (n) == Approx (pct).margin (1.e-4f));
}

TEST_CASE ("DSPUtils softClip is bounded and finite", "[dsp][utils]")
{
    REQUIRE (DSPUtils::softClip (0.f) == Approx (0.f));
    REQUIRE (DSPUtils::softClip (100.f) == Approx (1.f).margin (0.02f));
    REQUIRE (DSPUtils::softClip (-100.f) == Approx (-1.f).margin (0.02f));
}
