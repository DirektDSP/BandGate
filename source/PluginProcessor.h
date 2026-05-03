#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#if BANDGATE_NO_MOONBASE
    #include "MoonbaseStubs.h"
#else
    #include "moonbase_JUCEClient/moonbase_JUCEClient.h"
    #include "BinaryData.h"
#endif
#include "Service/PresetManager.h"
#include "DSP/BandGateDSP.h"

#if (MSVC)
#include "ipps.h"
#endif

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() noexcept override;

    MOONBASE_DECLARE_LICENSING("DirektDSP", "bandgate", VERSION)

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    AudioProcessorValueTreeState& getApvts() { return apvts; }

    Service::PresetManager& getPresetManager() { return *presetManager; }

    void fetchSpectralVisualData (std::vector<float>& magDbOut,
                                  std::vector<float>& gainOut,
                                  int& fftSizeOut,
                                  double& sampleRateOut) const;

    /** UI / metering: synced delay (+ diffusion bump) averaged over active relays. */
    float getEstimatedRelayRoundTripMs() const;

    /** UI: round-trip for one band (matches active band editor). */
    float getEstimatedRelayRoundTripMsForBand (int bandIndex) const;

    static constexpr int kMaxBands = 6;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        // Input/Output Gains
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"INPUT_GAIN", 1}, "Input",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"OUTPUT_GAIN", 1}, "Output",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"PARALLEL_GAIN", 1}, "Drive",
            juce::NormalisableRange<float>(-36.0f, 36.0f, 0.1f), 0.0f));

        // Mix
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"MIX", 1}, "Mix",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));

        // Band count (1..6). Each band has its own gate; LR splits at crossover Hz.
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"NUM_BANDS", 1}, "Bands",
            juce::StringArray{"1", "2", "3", "4", "5", "6"}, 1));

        const float defaultCrossovers[5] = { 250.0f, 800.0f, 2500.0f, 8000.0f, 14000.0f };

        for (int i = 0; i < 5; ++i)
        {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{juce::String ("CROSSOVER_") + juce::String (i), 1},
                juce::String ("Xover ") + juce::String (i + 1),
                juce::NormalisableRange<float>(40.0f, 20000.0f, 1.0f, 0.35f),
                defaultCrossovers[(size_t) i]));
        }

        for (int b = 0; b < kMaxBands; ++b)
        {
            const juce::String pfx = "BAND" + juce::String (b) + "_";
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{pfx + "THRESHOLD", 1}, pfx + "Threshold",
                juce::NormalisableRange<float>(-100.0f, 0.0f, 0.1f), -60.0f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{pfx + "REDUCTION", 1}, pfx + "Reduction",
                juce::NormalisableRange<float>(-200.0f, 0.0f, 0.1f, 0.2f), -80.0f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{pfx + "SMOOTHING", 1}, pfx + "Smoothing",
                juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 20.0f));
            params.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { pfx + "FLIP", 1 }, pfx + "Flip", false));
            params.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { pfx + "SOLO", 1 }, pfx + "Solo", false));
            params.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { pfx + "MUTE", 1 }, pfx + "Mute", false));

            const juce::String bandStr = juce::String (b + 1);
            const juce::String rfx = pfx + "RELAY_";

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { rfx + "ENABLE", 2 },
                juce::String ("Band ") + bandStr + " Relay Enable",
                false));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { rfx + "TIME_MODE", 2 },
                juce::String ("Band ") + bandStr + " Relay Time Mode",
                juce::StringArray { "Free", "Sync" }, 0));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "TIME_MS", 2 },
                juce::String ("Band ") + bandStr + " Relay Time",
                juce::NormalisableRange<float> { 1.0f, 8000.0f, 0.0f, 0.35f }, 400.0f,
                juce::AudioParameterFloatAttributes().withLabel ("ms")));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { rfx + "TIME_SYNC_DIV", 2 },
                juce::String ("Band ") + bandStr + " Relay Sync",
                juce::StringArray { "1/32", "1/16", "1/8", "1/8 dot", "1/4 tri", "1/4", "1/4 dot", "1/2 tri",
                                    "1/2", "1/2 dot", "1 bar", "2 bar", "4 bar" },
                5)); // ~1/4

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "FEEDBACK", 2 },
                juce::String ("Band ") + bandStr + " Relay Feedback",
                juce::NormalisableRange<float> { 0.0f, 1.55f }, 0.45f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "FEEDBACK_TRIM", 2 },
                juce::String ("Band ") + bandStr + " Relay Feedback Trim",
                juce::NormalisableRange<float> { 0.0f, 150.0f, 0.1f }, 100.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "INPUT_GAIN", 2 },
                juce::String ("Band ") + bandStr + " Relay Input",
                juce::NormalisableRange<float> { -36.0f, 36.0f, 0.1f }, 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("dB")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "MIX", 2 },
                juce::String ("Band ") + bandStr + " Relay Mix",
                juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "SEND", 2 },
                juce::String ("Band ") + bandStr + " Relay Send",
                juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "DIFFUSION_TIME", 2 },
                juce::String ("Band ") + bandStr + " Relay Diffusion Time",
                juce::NormalisableRange<float> { 1.0f, 750.0f, 0.0f, 0.42f }, 45.0f,
                juce::AudioParameterFloatAttributes().withLabel ("ms")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "DAMPING", 2 },
                juce::String ("Band ") + bandStr + " Relay Damping",
                juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "FLUTTER_RATE", 2 },
                juce::String ("Band ") + bandStr + " Relay Flutter Rate",
                juce::NormalisableRange<float> { 0.05f, 24.0f, 0.0f, 0.52f }, 1.75f,
                juce::AudioParameterFloatAttributes().withLabel ("Hz")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "FLUTTER_DEPTH", 2 },
                juce::String ("Band ") + bandStr + " Relay Flutter Depth",
                juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "CHORUS_RATE", 2 },
                juce::String ("Band ") + bandStr + " Relay Chorus Rate",
                juce::NormalisableRange<float> { 0.02f, 14.0f, 0.0f, 0.5f }, 0.62f,
                juce::AudioParameterFloatAttributes().withLabel ("Hz")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "CHORUS_DEPTH", 2 },
                juce::String ("Band ") + bandStr + " Relay Chorus Depth",
                juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 18.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "LOOP_HPF", 2 },
                juce::String ("Band ") + bandStr + " Relay Loop HPF",
                juce::NormalisableRange<float> { 20.0f, 4000.0f, 0.1f, 0.43f }, 95.0f,
                juce::AudioParameterFloatAttributes().withLabel ("Hz")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "LOOP_LPF", 2 },
                juce::String ("Band ") + bandStr + " Relay Loop LPF",
                juce::NormalisableRange<float> { 500.0f, 21000.0f, 0.01f, 0.42f }, 12500.0f,
                juce::AudioParameterFloatAttributes().withLabel ("Hz")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "OTT_AMOUNT", 2 },
                juce::String ("Band ") + bandStr + " Relay OTT Amount",
                juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 40.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { rfx + "OTT_TIME", 2 },
                juce::String ("Band ") + bandStr + " Relay OTT Time",
                juce::NormalisableRange<float> { 1.0f, 900.0f, 0.1f }, 135.0f,
                juce::AudioParameterFloatAttributes().withLabel ("ms")));
        }

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"ACTIVE_BAND", 1}, "Edit band",
            juce::StringArray{"Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6"}, 0));

        // FFT Size - controls frequency resolution vs time resolution tradeoff
        // 0=256, 1=512, 2=1024, 3=2048, 4=4096, 5=16384, 6=32768
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"FFT_SIZE", 1}, "FFT Size",
            juce::StringArray{"256", "512", "1024", "2048", "4096", "16384", "32768"}, 3));

        // Global: clears relay feedback tails on every band simultaneously.
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "RELAY_CLEAR", 2 }, "Relay Clear", false));

        return { params.begin(), params.end() };
    }

    static int fftChoiceToOrder (int choiceIndex)
    {
        // 0=256(8), 1=512(9), 2=1024(10), 3=2048(11), 4=4096(12), 5=16384(14), 6=32768(15)
        if (choiceIndex >= 6)
            return 15;
        if (choiceIndex >= 5)
            return 14;
        return choiceIndex + 8;
    }

private:

    std::unique_ptr<Service::PresetManager> presetManager;

    DSP::FloatProcessor dspProcessor;
    bool lastRelayClearHigh = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
