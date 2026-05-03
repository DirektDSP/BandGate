#include <PluginProcessor.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>

using Catch::Approx;

TEST_CASE ("PluginProcessor fftChoiceToOrder mapping", "[plugin]")
{
    REQUIRE (PluginProcessor::fftChoiceToOrder (0) == 8);
    REQUIRE (PluginProcessor::fftChoiceToOrder (3) == 11);
    REQUIRE (PluginProcessor::fftChoiceToOrder (4) == 12);
    REQUIRE (PluginProcessor::fftChoiceToOrder (5) == 14);
    REQUIRE (PluginProcessor::fftChoiceToOrder (6) == 15);
}

TEST_CASE ("PluginProcessor single-instance smoke", "[plugin]")
{
    // Moonbase/licensing globals do not tolerate multiple PluginProcessor lifetimes in one
    // test process; keep one instance and one editor attach for the whole case.
    PluginProcessor p;

    CHECK_THAT (p.getName().toStdString(), Catch::Matchers::Equals ("BandGate"));

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (p.isBusesLayoutSupported (stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    REQUIRE (p.isBusesLayoutSupported (mono));

    {
        p.setPlayConfigDetails (2, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                REQUIRE (std::isfinite (buf.getSample (ch, i)));

        p.releaseResources();
    }

    {
        p.setPlayConfigDetails (2, 2, 44100.0, 512);
        p.prepareToPlay (44100.0, 512);
        REQUIRE (p.getLatencySamples() == 2048);
        p.releaseResources();
    }

    {
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;

        for (int n = 0; n < 20; ++n)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float s = 0.05f * std::sin (0.0009f * (float) (n * buf.getNumSamples() + i));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }

            p.processBlock (buf, midi);
        }

        std::vector<float> mag, gain;
        int fftSize = 0;
        double sr = 0.0;
        p.fetchSpectralVisualData (mag, gain, fftSize, sr);

        REQUIRE (sr == Approx (48000.0));
        REQUIRE (fftSize == 2048);
        REQUIRE_FALSE (mag.empty());
        REQUIRE (mag.size() == gain.size());

        p.releaseResources();
    }

    {
        p.setPlayConfigDetails (2, 2, 44100.0, 128);
        p.prepareToPlay (44100.0, 128);

        auto* gainParam = dynamic_cast<juce::RangedAudioParameter*> (p.getApvts().getParameter ("INPUT_GAIN"));
        REQUIRE (gainParam != nullptr);
        gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (6.0f));
        const float normBefore = gainParam->getValue();

        juce::MemoryBlock data;
        p.getStateInformation (data);
        p.setStateInformation (data.getData(), (int) data.getSize());

        REQUIRE (gainParam->getValue() == Approx (normBefore).margin (1.0e-5f));
        p.releaseResources();
    }

    auto* ed = p.createEditor();
    REQUIRE (ed != nullptr);
    p.editorBeingDeleted (ed);
    delete ed;
}

#ifdef BANDGATE_IPP
    #include <ipp.h>

TEST_CASE ("IPP version", "[ipp]")
{
    CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2022.0.0 (r0x131e93b0)"));
}
#endif
