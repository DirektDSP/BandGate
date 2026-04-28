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

    static constexpr int kMaxBands = 6;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        // Input/Output Gains
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"INPUT_GAIN", 1}, "Input",
            juce::NormalisableRange<float>(-48.0f, 24.0f, 0.1f), 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"OUTPUT_GAIN", 1}, "Output",
            juce::NormalisableRange<float>(-48.0f, 24.0f, 0.1f), 0.0f));

        // Mix
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"MIX", 1}, "Mix",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));

        // Band count (2..6). Each band has its own gate; LR splits at crossover Hz.
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"NUM_BANDS", 1}, "Bands",
            juce::StringArray{"2", "3", "4", "5", "6"}, 1));

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
        }

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"ACTIVE_BAND", 1}, "Edit band",
            juce::StringArray{"Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6"}, 0));

        // FFT Size - controls frequency resolution vs time resolution tradeoff
        // 0=256, 1=512, 2=1024, 3=2048, 4=4096
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"FFT_SIZE", 1}, "FFT Size",
            juce::StringArray{"256", "512", "1024", "2048", "4096"}, 3));

        return { params.begin(), params.end() };
    }

    static int fftChoiceToOrder (int choiceIndex)
    {
        // 0=256(8), 1=512(9), 2=1024(10), 3=2048(11), 4=4096(12)
        return choiceIndex + 8;
    }

private:

    std::unique_ptr<Service::PresetManager> presetManager;

    DSP::FloatProcessor dspProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
