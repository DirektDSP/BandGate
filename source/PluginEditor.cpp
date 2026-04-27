#include "PluginEditor.h"

namespace
{
    int numBandsFromApvts (juce::AudioProcessorValueTreeState& apvts)
    {
        const int c = static_cast<int> (*apvts.getRawParameterValue ("NUM_BANDS"));
        return juce::jlimit (2, PluginProcessor::kMaxBands, c + 2);
    }
} // namespace

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), apvts (p.getApvts())
{
#if !BANDGATE_NO_MOONBASE && INCLUDE_MOONBASE_UI
    if (processorRef.moonbaseClient != nullptr)
        activationUI.reset (processorRef.moonbaseClient->createActivationUi (*this));

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

    setupSlider (inputGainSlider, inputGainLabel, "Input", "dB");
    setupSlider (outputGainSlider, outputGainLabel, "Output", "dB");
    setupSlider (mixSlider, mixLabel, "Mix", "%");
    setupSlider (thresholdSlider, thresholdLabel, "Threshold", "dB");
    setupSlider (reductionSlider, reductionLabel, "Reduction", "dB");
    setupSlider (smoothingSlider, smoothingLabel, "Smoothing", "ms");

    addAndMakeVisible (fftSizeCB);
    addAndMakeVisible (fftSizeLabel);
    fftSizeCB.addItemList (juce::StringArray { "256", "512", "1024", "2048", "4096" }, 1);
    fftSizeLabel.setText ("FFT Size", juce::dontSendNotification);
    fftSizeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    fftSizeLabel.setJustificationType (juce::Justification::centred);
    fftSizeLabel.attachToComponent (&fftSizeCB, false);

    addAndMakeVisible (numBandsCB);
    addAndMakeVisible (numBandsLabel);
    numBandsCB.addItemList (juce::StringArray { "2", "3", "4", "5", "6" }, 1);
    numBandsLabel.setText ("Bands", juce::dontSendNotification);
    numBandsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    numBandsLabel.setJustificationType (juce::Justification::centred);
    numBandsLabel.attachToComponent (&numBandsCB, false);

    addAndMakeVisible (activeBandCB);
    addAndMakeVisible (activeBandLabel);
    activeBandCB.addItemList (juce::StringArray { "Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6" }, 1);
    activeBandLabel.setText ("Edit band", juce::dontSendNotification);
    activeBandLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    activeBandLabel.setJustificationType (juce::Justification::centred);
    activeBandLabel.attachToComponent (&activeBandCB, false);

    for (int i = 0; i < 5; ++i)
    {
        setupLinearSlider (crossoverSliders[(size_t) i], crossoverLabels[(size_t) i],
                           "Xover " + juce::String (i + 1), " Hz");
        crossoverAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, "CROSSOVER_" + juce::String (i), crossoverSliders[(size_t) i]);
    }

    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "INPUT_GAIN", inputGainSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "OUTPUT_GAIN", outputGainSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "MIX", mixSlider);
    fftSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "FFT_SIZE", fftSizeCB);
    numBandsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "NUM_BANDS", numBandsCB);
    activeBandAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "ACTIVE_BAND", activeBandCB);

    rebuildGateSliderAttachments();
    updateCrossoverSliderVisibility();

    apvts.addParameterListener ("NUM_BANDS", this);
    apvts.addParameterListener ("ACTIVE_BAND", this);

    addAndMakeVisible (spectrumViz);

    setSize (880, 560);
}

PluginEditor::~PluginEditor()
{
    apvts.removeParameterListener ("NUM_BANDS", this);
    apvts.removeParameterListener ("ACTIVE_BAND", this);
}

void PluginEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "NUM_BANDS")
    {
        syncActiveBandToNumBands();
        updateCrossoverSliderVisibility();
        rebuildGateSliderAttachments();
    }
    else if (parameterID == "ACTIVE_BAND")
    {
        rebuildGateSliderAttachments();
    }
}

void PluginEditor::syncActiveBandToNumBands()
{
    const int nb = numBandsFromApvts (apvts);
    const int maxIdx = nb - 1;
    if (auto* p = apvts.getParameter ("ACTIVE_BAND"))
    {
        const int cur = static_cast<int> (*apvts.getRawParameterValue ("ACTIVE_BAND"));
        if (cur > maxIdx)
            p->setValueNotifyingHost (p->convertTo0to1 ((float) maxIdx));
    }
}

void PluginEditor::rebuildGateSliderAttachments()
{
    const int band = juce::jlimit (
        0, PluginProcessor::kMaxBands - 1,
        static_cast<int> (*apvts.getRawParameterValue ("ACTIVE_BAND")));
    const juce::String pfx = "BAND" + juce::String (band) + "_";

    thresholdAttachment.reset();
    reductionAttachment.reset();
    smoothingAttachment.reset();

    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "THRESHOLD", thresholdSlider);
    reductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "REDUCTION", reductionSlider);
    smoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "SMOOTHING", smoothingSlider);
}

void PluginEditor::updateCrossoverSliderVisibility()
{
    const int nx = numBandsFromApvts (apvts) - 1;
    for (int i = 0; i < 5; ++i)
    {
        const bool on = i < nx;
        crossoverSliders[(size_t) i].setVisible (on);
        crossoverLabels[(size_t) i].setVisible (on);
    }
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (30, 30, 30));

    g.setColour (juce::Colour::fromRGB (60, 60, 60));
    auto area = getLocalBounds().reduced (10);
    g.drawHorizontalLine (area.getY() + 90, (float) area.getX(), (float) area.getRight());
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    #ifdef JUCE_DEBUG
        inspectButton.setBounds (area.removeFromBottom (30).withSizeKeepingCentre (100, 25));
    #endif

    auto topRow = area.removeFromTop (80);
    const int topKnobWidth = topRow.getWidth() / 3;

    inputGainSlider.setBounds (topRow.removeFromLeft (topKnobWidth).reduced (5));
    outputGainSlider.setBounds (topRow.removeFromLeft (topKnobWidth).reduced (5));
    mixSlider.setBounds (topRow.reduced (5));

    area.removeFromTop (10);

    spectrumViz.setBounds (area.removeFromTop (220));

    area.removeFromTop (10);

    auto bandRow = area.removeFromTop (32);
    numBandsCB.setBounds (bandRow.removeFromLeft (100).reduced (0, 4));
    bandRow.removeFromLeft (12);
    activeBandCB.setBounds (bandRow.removeFromLeft (120).reduced (0, 4));

    area.removeFromTop (8);

    auto xRow = area.removeFromTop (56);
    const int xw = juce::jmax (44, xRow.getWidth() / 5);
    for (int i = 0; i < 5; ++i)
        crossoverSliders[(size_t) i].setBounds (xRow.removeFromLeft (xw).reduced (3, 0));

    area.removeFromTop (10);

    auto gateRow = area.removeFromTop (110);
    const int gateKnobWidth = gateRow.getWidth() / 3;

    thresholdSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10));
    reductionSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10));
    smoothingSlider.setBounds (gateRow.reduced (10));

    area.removeFromTop (8);

    auto bottomRow = area;
    const int cbWidth = 120;
    fftSizeCB.setBounds (bottomRow.withSizeKeepingCentre (cbWidth, 25));

#if !BANDGATE_NO_MOONBASE && INCLUDE_MOONBASE_UI
    if (activationUI)
        activationUI->setBounds (getLocalBounds());
#endif
}

void PluginEditor::setupSlider (juce::Slider& slider, juce::Label& label,
                                const juce::String& labelText, const juce::String& suffix)
{
    addAndMakeVisible (slider);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setTextValueSuffix (" " + suffix);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);

    label.setText (labelText, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
}

void PluginEditor::setupLinearSlider (juce::Slider& slider, juce::Label& label,
                                      const juce::String& labelText, const juce::String& suffix)
{
    addAndMakeVisible (slider);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);

    label.setText (labelText, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType (juce::Justification::centredLeft);
    label.attachToComponent (&slider, false);
}
