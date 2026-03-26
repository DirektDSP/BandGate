#include "PluginEditor.h"

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), apvts(p.getApvts())
{
#if !BANDGATE_NO_MOONBASE
    if (processorRef.moonbaseClient != nullptr)
        activationUI.reset(processorRef.moonbaseClient->createActivationUi(*this));

    if (activationUI)
        activationUI->setWelcomePageText ("BandGate", "Made by DirektDSP");
#endif

    #ifdef JUCE_DEBUG
        addAndMakeVisible (inspectButton);
        inspectButton.onClick = [&] {
            if (!inspector)
            {
                inspector = std::make_unique<melatonin::Inspector> (*this);
                inspector->onClose = [this]() { inspector.reset(); };
            }
            inspector->setVisible (true);
        };
    #endif

    // Setup sliders
    setupSlider(inputGainSlider, inputGainLabel, "Input", "dB");
    setupSlider(outputGainSlider, outputGainLabel, "Output", "dB");
    setupSlider(mixSlider, mixLabel, "Mix", "%");
    setupSlider(thresholdSlider, thresholdLabel, "Threshold", "dB");
    setupSlider(reductionSlider, reductionLabel, "Reduction", "dB");
    setupSlider(smoothingSlider, smoothingLabel, "Smoothing", "ms");

    // FFT Size combo box
    addAndMakeVisible(fftSizeCB);
    addAndMakeVisible(fftSizeLabel);
    fftSizeCB.addItemList(juce::StringArray{"256", "512", "1024", "2048", "4096"}, 1);
    fftSizeLabel.setText("FFT Size", juce::dontSendNotification);
    fftSizeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    fftSizeLabel.setJustificationType(juce::Justification::centred);
    fftSizeLabel.attachToComponent(&fftSizeCB, false);

    // APVTS Attachments
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "INPUT_GAIN", inputGainSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "OUTPUT_GAIN", outputGainSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "MIX", mixSlider);
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "THRESHOLD", thresholdSlider);
    reductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "REDUCTION", reductionSlider);
    smoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, "SMOOTHING", smoothingSlider);
    fftSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, "FFT_SIZE", fftSizeCB);

    setSize (700, 400);
}

PluginEditor::~PluginEditor()
{
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (30, 30, 30));

    // Draw separator between top controls and spectral gate section
    g.setColour (juce::Colour::fromRGB (60, 60, 60));
    auto area = getLocalBounds().reduced(10);
    g.drawHorizontalLine (area.getY() + 90, (float) area.getX(), (float) area.getRight());
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    #ifdef JUCE_DEBUG
        inspectButton.setBounds(area.removeFromBottom(30).withSizeKeepingCentre(100, 25));
    #endif

    // Top row: Input, Output, Mix
    auto topRow = area.removeFromTop(80);
    int topKnobWidth = topRow.getWidth() / 3;

    inputGainSlider.setBounds(topRow.removeFromLeft(topKnobWidth).reduced(5));
    outputGainSlider.setBounds(topRow.removeFromLeft(topKnobWidth).reduced(5));
    mixSlider.setBounds(topRow.reduced(5));

    area.removeFromTop(20);

    // Spectral gate controls
    auto gateRow = area.removeFromTop(area.getHeight() * 2 / 3);
    int gateKnobWidth = gateRow.getWidth() / 3;

    thresholdSlider.setBounds(gateRow.removeFromLeft(gateKnobWidth).reduced(10));
    reductionSlider.setBounds(gateRow.removeFromLeft(gateKnobWidth).reduced(10));
    smoothingSlider.setBounds(gateRow.reduced(10));

    area.removeFromTop(10);

    // Bottom row: FFT Size
    auto bottomRow = area;
    int cbWidth = 120;
    fftSizeCB.setBounds(bottomRow.withSizeKeepingCentre(cbWidth, 25));

    MOONBASE_RESIZE_ACTIVATION_UI
}

//==============================================================================
void PluginEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                               const juce::String& labelText, const juce::String& suffix)
{
    addAndMakeVisible(slider);
    addAndMakeVisible(label);

    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setTextValueSuffix(" " + suffix);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);

    label.setText(labelText, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType(juce::Justification::centred);
    label.attachToComponent(&slider, false);
}
