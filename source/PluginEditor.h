#pragma once

#include <array>
#include <memory>

#include "PluginProcessor.h"
#include "SpectrumVisualizer.h"
#include "melatonin_inspector/melatonin_inspector.h"

#include <juce_gui_basics/juce_gui_basics.h>

#if !BANDGATE_NO_MOONBASE
    #include "moonbase_JUCEClient/moonbase_JUCEClient.h"
#endif

class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void rebuildGateSliderAttachments();
    void syncActiveBandToNumBands();
    void updateCrossoverSliderVisibility();

    PluginProcessor& processorRef;
    AudioProcessorValueTreeState& apvts;

    SpectrumVisualizer spectrumViz { processorRef };

    juce::TextButton inspectButton { "Inspect the UI" };

#if !BANDGATE_NO_MOONBASE && INCLUDE_MOONBASE_UI
    std::unique_ptr<Moonbase::JUCEClient::ActivationUI> activationUI;
#endif
    std::unique_ptr<melatonin::Inspector> inspector;

    juce::Slider inputGainSlider, outputGainSlider, mixSlider;
    juce::Slider thresholdSlider, reductionSlider, smoothingSlider;
    juce::ToggleButton flipButton { "Flip" };
    juce::ToggleButton soloButton { "Solo" };
    juce::ComboBox fftSizeCB;
    juce::ComboBox numBandsCB, activeBandCB;

    juce::Label inputGainLabel, outputGainLabel, mixLabel;
    juce::Label thresholdLabel, reductionLabel, smoothingLabel;
    juce::Label fftSizeLabel, numBandsLabel, activeBandLabel;
    std::array<juce::Label, 5> crossoverLabels {};
    std::array<juce::Slider, 5> crossoverSliders {};

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flipAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> numBandsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> activeBandAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> crossoverAttachments {};

    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& suffix);
    void setupLinearSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
