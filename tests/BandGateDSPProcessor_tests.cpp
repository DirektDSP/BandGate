#include "DSP/Core/BandGateDSPProcessor.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using DSP::Core::BandGateDSPProcessor;

namespace
{
    void fillDefaults (int numBands,
                       std::array<float, 6>& thr,
                       std::array<float, 6>& red,
                       std::array<float, 6>& sm,
                       std::array<bool, 6>& flip,
                       std::array<bool, 6>& solo,
                       std::array<bool, 6>& mute,
                       std::array<float, 5>& xover)
    {
        for (int b = 0; b < 6; ++b)
        {
            thr[(size_t) b] = -60.f;
            red[(size_t) b] = -80.f;
            sm[(size_t) b] = 20.f;
            flip[(size_t) b] = false;
            solo[(size_t) b] = false;
            mute[(size_t) b] = false;
        }

        const float defaults[5] = { 250.f, 800.f, 2500.f, 8000.f, 14000.f };

        for (int i = 0; i < numBands - 1; ++i)
            xover[(size_t) i] = defaults[(size_t) i];
    }
} // namespace

TEST_CASE ("BandGateDSPProcessor silence block is finite", "[dsp][processor]")
{
    BandGateDSPProcessor<float> dsp;
    juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };

    std::array<float, 6> thr {}, red {}, sm {};
    std::array<bool, 6> flip {}, solo {}, mute {};
    std::array<float, 5> xover {};
    fillDefaults (3, thr, red, sm, flip, solo, mute, xover);

    dsp.prepare (spec, 0.f, 0.f, 0.f, 100.f, 11, 3,
                 thr.data(), red.data(), sm.data(),
                 flip.data(), solo.data(), mute.data(), xover.data());

    REQUIRE (dsp.getLatencySamples() > 0);

    juce::AudioBuffer<float> buf (2, 512);
    buf.clear();

    dsp.processBlock (buf);

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            REQUIRE (std::isfinite (buf.getSample (ch, i)));
}

TEST_CASE ("BandGateDSPProcessor relay round-trip per band is sane", "[dsp][processor]")
{
    BandGateDSPProcessor<float> dsp;
    juce::dsp::ProcessSpec spec { 44100.0, 256, 2 };
    std::array<float, 6> thr {}, red {}, sm {};
    std::array<bool, 6> flip {}, solo {}, mute {};
    std::array<float, 5> xover {};
    fillDefaults (2, thr, red, sm, flip, solo, mute, xover);

    dsp.prepare (spec, 0.f, 0.f, 0.f, 100.f, 10, 2,
                 thr.data(), red.data(), sm.data(),
                 flip.data(), solo.data(), mute.data(), xover.data());

    std::array<DSP::Core::RelayRuntimeParams, BandGateDSPProcessor<float>::kMaxBands> relays {};

    for (auto& r : relays)
    {
        r.enabled = true;
        r.timeMs = 220.f;
        r.feedback = 0.4f;
        r.mixPercent = 40.f;
    }

    dsp.updateRelayParameters (relays);
    const float rt0 = dsp.getRelayRoundTripMsForBand (0);
    REQUIRE (rt0 > 50.f);
    REQUIRE (rt0 < 2000.f);
}

TEST_CASE ("BandGateDSPProcessor many blocks with sine stays finite", "[dsp][processor]")
{
    BandGateDSPProcessor<float> dsp;
    juce::dsp::ProcessSpec spec { 44100.0, 64, 2 };
    std::array<float, 6> thr {}, red {}, sm {};
    std::array<bool, 6> flip {}, solo {}, mute {};
    std::array<float, 5> xover {};
    fillDefaults (4, thr, red, sm, flip, solo, mute, xover);

    dsp.prepare (spec, -3.f, 2.f, 6.f, 85.f, 9, 4,
                 thr.data(), red.data(), sm.data(),
                 flip.data(), solo.data(), mute.data(), xover.data());

    juce::AudioBuffer<float> buf (2, 64);

    for (int n = 0; n < 400; ++n)
    {
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float s = 0.08f * std::sin (juce::MathConstants<float>::twoPi * 220.f * (float) (n * 64 + i) / 44100.f);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s * 0.9f);
        }

        dsp.processBlock (buf);

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                REQUIRE (std::isfinite (buf.getSample (ch, i)));
    }
}
