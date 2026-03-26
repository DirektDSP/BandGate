#pragma once

#include "PluginProcessor.h"
#include "melatonin_inspector/melatonin_inspector.h"

#include <juce_gui_basics/juce_gui_basics.h>

#if !BANDGATE_NO_MOONBASE
    #include "moonbase_JUCEClient/moonbase_JUCEClient.h"
#endif

class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processorRef;
    AudioProcessorValueTreeState& apvts;

    juce::TextButton inspectButton { "Inspect the UI" };

#if !BANDGATE_NO_MOONBASE
    std::unique_ptr<Moonbase::JUCEClient::ActivationUI> activationUI;
#endif
    std::unique_ptr<melatonin::Inspector> inspector;

    // Sliders
    juce::Slider inputGainSlider, outputGainSlider, mixSlider;
    juce::Slider thresholdSlider, reductionSlider, smoothingSlider;
    juce::ComboBox fftSizeCB;

    // Labels
    juce::Label inputGainLabel, outputGainLabel, mixLabel;
    juce::Label thresholdLabel, reductionLabel, smoothingLabel;
    juce::Label fftSizeLabel;

    // APVTS Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftSizeAttachment;

    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
