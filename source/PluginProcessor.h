#pragma once

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

        // Spectral Gate Threshold (dB) - bins below this magnitude get gated
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"THRESHOLD", 1}, "Threshold",
            juce::NormalisableRange<float>(-100.0f, 0.0f, 0.1f), -60.0f));

        // Reduction amount (dB) - how much gated bins are attenuated
        // 0 dB = no reduction (gate off), -80 dB = nearly silent
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"REDUCTION", 1}, "Reduction",
            juce::NormalisableRange<float>(-80.0f, 0.0f, 0.1f), -80.0f));

        // Smoothing (ms) - temporal smoothing to reduce musical noise artifacts
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"SMOOTHING", 1}, "Smoothing",
            juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 20.0f));

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
