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
                     private juce::AudioProcessorValueTreeState::Listener,
                     private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void rebuildGateSliderAttachments();
    void rebuildRelayAttachments();
    void syncActiveBandToNumBands();
    void maybeShowUpdateInfoModalOnLaunch();
    void updateLatencyLabel();
    void updateRelayRoundTripLabel();

    PluginProcessor& processorRef;
    AudioProcessorValueTreeState& apvts;

    SpectrumVisualizer spectrumViz { processorRef };

    juce::TextButton inspectButton { "Inspect the UI" };

#if !BANDGATE_NO_MOONBASE && INCLUDE_MOONBASE_UI
    std::unique_ptr<Moonbase::JUCEClient::ActivationUI> activationUI;
#endif
    std::unique_ptr<melatonin::Inspector> inspector;

    juce::Slider inputGainSlider, outputGainSlider, parallelGainSlider, mixSlider;
    juce::Slider thresholdSlider, reductionSlider, smoothingSlider;
    juce::ToggleButton flipButton { "Flip" };
    juce::ToggleButton soloButton { "Solo" };
    juce::ToggleButton muteButton { "Mute" };
    juce::ToggleButton relayEnableButton { "Relay on" };
    juce::ToggleButton relayClearButton { "Clr tails" };
    juce::ComboBox relayTimeModeCB;
    juce::ComboBox relaySyncDivCB;
    juce::ComboBox fftSizeCB;
    juce::ComboBox numBandsCB, spectrumMinDbCB, spectrumMaxDbCB;

    juce::Label inputGainLabel, outputGainLabel, parallelGainLabel, mixLabel;
    juce::Label thresholdLabel, reductionLabel, smoothingLabel;
    juce::Slider relayTimeSlider, relayFeedbackSlider, relayInputGainSlider, relayMixSlider;
    juce::Slider relayDiffusionSlider, relayDampingSlider;
    juce::Slider relayFlutterRateSlider, relayFlutterDepthSlider;
    juce::Slider relayChorusRateSlider, relayChorusDepthSlider;
    juce::Slider relayLoopHpfSlider, relayLoopLpfSlider;
    juce::Slider relayOttAmountSlider, relayOttTimeSlider;

    juce::Label relayTimeLabel, relayFeedbackLabel, relayInputGainLabel, relayMixLabel;
    juce::Label relayDiffusionLabel, relayDampingLabel;
    juce::Label relayFlutterRateLabel, relayFlutterDepthLabel;
    juce::Label relayChorusRateLabel, relayChorusDepthLabel;
    juce::Label relayLoopHpfLabel, relayLoopLpfLabel;
    juce::Label relayOttAmountLabel, relayOttTimeLabel;
    juce::Label relayTimeModeLabel { {}, "Dly mode" };
    juce::Label relaySyncDivLabel { {}, "Grid" };

    juce::Label fftSizeLabel, numBandsLabel, spectrumMinDbLabel, spectrumMaxDbLabel;
    juce::Label latencyLabel;
    juce::Label relayRoundTripLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> parallelGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flipAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> numBandsAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> relayEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> relayClearAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> relayTimeModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> relaySyncDivAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayFeedbackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayInputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayDiffusionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayDampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayFlutterRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayFlutterDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayChorusRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayChorusDepthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayLoopHpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayLoopLpfAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayOttAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> relayOttTimeAttachment;

    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& suffix);
    void setupLinearSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& suffix);
    void setupVerticalSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
