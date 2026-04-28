#include "PluginEditor.h"

namespace
{
    int numBandsFromApvts (juce::AudioProcessorValueTreeState& apvts)
    {
        const int c = static_cast<int> (*apvts.getRawParameterValue ("NUM_BANDS"));
        return juce::jlimit (2, PluginProcessor::kMaxBands, c + 2);
    }

    // Single source of truth for layout — keeps paint() and resized() aligned and avoids overflow.
    struct Layout
    {
        static constexpr int margin = 12;
        static constexpr int gap = 8;
        static constexpr int topKnobsH = 92;
        static constexpr int spectrumH = 200;
        static constexpr int bandBarH = 40;
        static constexpr int xoverH = 56;
        static constexpr int gateH = 112;

        static constexpr int defaultWidth = 920;
        static constexpr int defaultHeight = 620;
    };
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
    setupSlider (parallelGainSlider, parallelGainLabel, "Drive", "dB");
    setupSlider (mixSlider, mixLabel, "Mix", "%");
    setupSlider (thresholdSlider, thresholdLabel, "Threshold", "dB");
    setupSlider (reductionSlider, reductionLabel, "Reduction", "dB");
    setupSlider (smoothingSlider, smoothingLabel, "Smoothing", "ms");
    addAndMakeVisible (flipButton);
    addAndMakeVisible (soloButton);

    addAndMakeVisible (fftSizeCB);
    addAndMakeVisible (fftSizeLabel);
    fftSizeCB.addItemList (juce::StringArray { "256", "512", "1024", "2048", "4096" }, 1);
    fftSizeLabel.setText ("FFT", juce::dontSendNotification);
    fftSizeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    fftSizeLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (numBandsCB);
    addAndMakeVisible (numBandsLabel);
    numBandsCB.addItemList (juce::StringArray { "2", "3", "4", "5", "6" }, 1);
    numBandsLabel.setText ("Bands", juce::dontSendNotification);
    numBandsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    numBandsLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (spectrumMinDbCB);
    addAndMakeVisible (spectrumMinDbLabel);
    addAndMakeVisible (spectrumMaxDbCB);
    addAndMakeVisible (spectrumMaxDbLabel);
    const juce::StringArray dbSteps { "-96 dB", "-84 dB", "-72 dB", "-60 dB", "-48 dB", "-36 dB", "-24 dB", "-12 dB", "0 dB", "+12 dB", "+24 dB" };
    spectrumMinDbCB.addItemList (dbSteps, 1);
    spectrumMaxDbCB.addItemList (dbSteps, 1);
    spectrumMinDbCB.setSelectedId (1, juce::dontSendNotification);
    spectrumMaxDbCB.setSelectedId (11, juce::dontSendNotification);
    auto applySpectrumDbRange = [this] (bool minChanged)
    {
        const float minDb = -96.0f + 12.0f * (float) (spectrumMinDbCB.getSelectedId() - 1);
        const float maxDb = -96.0f + 12.0f * (float) (spectrumMaxDbCB.getSelectedId() - 1);

        if (minDb >= maxDb)
        {
            if (minChanged)
                spectrumMaxDbCB.setSelectedId (juce::jlimit (2, 11, spectrumMinDbCB.getSelectedId() + 1), juce::dontSendNotification);
            else
                spectrumMinDbCB.setSelectedId (juce::jlimit (1, 10, spectrumMaxDbCB.getSelectedId() - 1), juce::dontSendNotification);
        }

        const float fixedMinDb = -96.0f + 12.0f * (float) (spectrumMinDbCB.getSelectedId() - 1);
        const float fixedMaxDb = -96.0f + 12.0f * (float) (spectrumMaxDbCB.getSelectedId() - 1);
        spectrumViz.setVerticalDbRange (fixedMinDb, fixedMaxDb);
    };
    spectrumMinDbCB.onChange = [applySpectrumDbRange] { applySpectrumDbRange (true); };
    spectrumMaxDbCB.onChange = [applySpectrumDbRange] { applySpectrumDbRange (false); };
    spectrumMinDbLabel.setText ("Min dB", juce::dontSendNotification);
    spectrumMinDbLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    spectrumMinDbLabel.setJustificationType (juce::Justification::centredRight);
    spectrumMaxDbLabel.setText ("Max dB", juce::dontSendNotification);
    spectrumMaxDbLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    spectrumMaxDbLabel.setJustificationType (juce::Justification::centredRight);
    spectrumViz.setVerticalDbRange (-96.0f, 24.0f);

    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange.withAlpha (0.85f));
    soloButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    soloButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

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
    parallelGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "PARALLEL_GAIN", parallelGainSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "MIX", mixSlider);
    fftSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "FFT_SIZE", fftSizeCB);
    numBandsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "NUM_BANDS", numBandsCB);
    rebuildGateSliderAttachments();
    updateCrossoverSliderVisibility();

    apvts.addParameterListener ("NUM_BANDS", this);
    apvts.addParameterListener ("ACTIVE_BAND", this);

    addAndMakeVisible (spectrumViz);

    setSize (Layout::defaultWidth, Layout::defaultHeight);
    setResizeLimits (760, 540, 1600, 900);
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
    flipAttachment.reset();
    soloAttachment.reset();

    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "THRESHOLD", thresholdSlider);
    reductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "REDUCTION", reductionSlider);
    smoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "SMOOTHING", smoothingSlider);
    flipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, pfx + "FLIP", flipButton);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, pfx + "SOLO", soloButton);
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
    using namespace juce;

    const auto bg = Colour (0xff1a1c1e);
    const auto panel = Colour (0xff232629);
    const auto rule = Colour (0xff3a3f44);

    g.fillAll (bg);

    auto outer = getLocalBounds().toFloat().reduced ((float) Layout::margin);

    g.setColour (panel);
    g.fillRoundedRectangle (outer, 6.0f);

    g.setColour (rule.withAlpha (0.55f));
    g.drawRoundedRectangle (outer, 6.0f, 1.0f);

    // Hairlines centered in the gaps between sections (matches resized() strip order).
    const float inset = 10.0f;
    const float xL = outer.getX() + inset;
    const float xR = outer.getRight() - inset;
    const float g2 = (float) Layout::gap * 0.5f;

    float y = outer.getY();
    auto hairlineInNextGap = [&] (int blockH) {
        y += (float) blockH + g2;
        g.setColour (rule.withAlpha (0.35f));
        g.drawHorizontalLine (roundToInt (y), xL, xR);
        y += g2;
    };

    hairlineInNextGap (Layout::topKnobsH);
    hairlineInNextGap (Layout::spectrumH);
    hairlineInNextGap (Layout::bandBarH);
    hairlineInNextGap (Layout::xoverH);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (Layout::margin);

    #ifdef JUCE_DEBUG
        constexpr int inspectH = 28;
        inspectButton.setBounds (area.removeFromBottom (inspectH)
                                      .withSizeKeepingCentre (juce::jmin (140, area.getWidth() - 8),
                                                              juce::jmax (22, inspectH - 4)));
        if (area.getHeight() > 0)
            area.removeFromBottom (Layout::gap);
    #endif

    auto topRow = area.removeFromTop (Layout::topKnobsH);
    const int pad = 8;
    const int topKnobWidth = juce::jmax (96, (topRow.getWidth() - 3 * pad) / 4);

    inputGainSlider.setBounds (topRow.removeFromLeft (topKnobWidth).reduced (pad, 6));
    outputGainSlider.setBounds (topRow.removeFromLeft (topKnobWidth).reduced (pad, 6));
    parallelGainSlider.setBounds (topRow.removeFromLeft (topKnobWidth).reduced (pad, 6));
    mixSlider.setBounds (topRow.reduced (pad, 6));

    area.removeFromTop (Layout::gap);

    spectrumViz.setBounds (area.removeFromTop (Layout::spectrumH));

    area.removeFromTop (Layout::gap);

    {
        auto bandRow = area.removeFromTop (Layout::bandBarH);
        const int lab = 52;
        const int comboFixed = 64;
        const int dbCombo = 88;

        auto b1 = bandRow.removeFromLeft (lab + comboFixed);
        numBandsLabel.setBounds (b1.removeFromLeft (lab));
        numBandsCB.setBounds (b1.reduced (0, 6));

        bandRow.removeFromLeft (Layout::gap);

        auto minDb = bandRow.removeFromLeft (lab + dbCombo);
        spectrumMinDbLabel.setBounds (minDb.removeFromLeft (lab));
        spectrumMinDbCB.setBounds (minDb.reduced (0, 6));

        bandRow.removeFromLeft (Layout::gap);

        auto maxDb = bandRow.removeFromLeft (lab + dbCombo);
        spectrumMaxDbLabel.setBounds (maxDb.removeFromLeft (lab));
        spectrumMaxDbCB.setBounds (maxDb.reduced (0, 6));

        bandRow.removeFromLeft (Layout::gap);

        auto fftBlock = bandRow.removeFromRight (lab + comboFixed);
        fftSizeLabel.setBounds (fftBlock.removeFromLeft (lab));
        fftSizeCB.setBounds (fftBlock.reduced (0, 6));
    }

    area.removeFromTop (Layout::gap);

    auto xRow = area.removeFromTop (Layout::xoverH);
    const int nx = 5;
    const int xw = juce::jmax (56, (xRow.getWidth() - (nx - 1) * 4) / nx);
    for (int i = 0; i < nx; ++i)
    {
        if (i > 0)
            xRow.removeFromLeft (4);
        crossoverSliders[(size_t) i].setBounds (xRow.removeFromLeft (xw).reduced (2, 4));
    }

    area.removeFromTop (Layout::gap);

    auto gateRow = area.removeFromTop (Layout::gateH);
    const int flipW = 80;
    const int soloW = 80;
    const int gateKnobWidth = juce::jmax (110, (gateRow.getWidth() - flipW - soloW - Layout::gap * 2) / 3);

    thresholdSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10, 8));
    reductionSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10, 8));
    smoothingSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10, 8));
    gateRow.removeFromLeft (Layout::gap);
    flipButton.setBounds (gateRow.removeFromLeft (flipW).withSizeKeepingCentre (flipW, 24));
    gateRow.removeFromLeft (Layout::gap);
    soloButton.setBounds (gateRow.removeFromLeft (soloW).withSizeKeepingCentre (soloW, 24));

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
