#include "PluginEditor.h"

namespace
{
    bool gUpdateInfoModalShown = false;

    int numBandsFromApvts (juce::AudioProcessorValueTreeState& apvts)
    {
        const int c = static_cast<int> (*apvts.getRawParameterValue ("NUM_BANDS"));
        return juce::jlimit (1, PluginProcessor::kMaxBands, c + 1);
    }

    // Single source of truth for layout — keeps paint() and resized() aligned and avoids overflow.
    struct Layout
    {
        static constexpr int margin = 12;
        static constexpr int gap = 8;
        static constexpr int topKnobsH = 92;
        static constexpr int spectrumH = 200;
        static constexpr int bandBarH = 48;
        static constexpr int gateH = 112;
        static constexpr int relayRowH = 118;
        static constexpr int relayH = relayRowH * 2 + gap;

        static constexpr int defaultWidth = 920;
        static constexpr int defaultHeight = 628 + relayH;
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
    {
        activationUI->setWelcomePageText ("BandGate", "Made by DirektDSP");
        activationUI->enableUpdateBadge();
    }
#endif

    maybeShowUpdateInfoModalOnLaunch();

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
    setupVerticalSlider (thresholdSlider, thresholdLabel, "Threshold", "dB");
    setupVerticalSlider (reductionSlider, reductionLabel, "Reduction", "dB");
    setupSlider (smoothingSlider, smoothingLabel, "Smoothing", "ms");

    setupSlider (relayTimeSlider, relayTimeLabel, "Delay", "ms");
    relayTimeSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
    setupSlider (relayFeedbackSlider, relayFeedbackLabel, "Rly FB", {});
    relayFeedbackSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
    setupSlider (relayInputGainSlider, relayInputGainLabel, "Rly in", "dB");
    relayInputGainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayMixSlider, relayMixLabel, "Rly mx", "%");
    relayMixSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
    setupSlider (relayDiffusionSlider, relayDiffusionLabel, "Smear", "ms");
    relayDiffusionSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
    setupSlider (relayDampingSlider, relayDampingLabel, "Tone", "%");
    relayDampingSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
    setupSlider (relayFlutterRateSlider, relayFlutterRateLabel, "Fl.Rt", "Hz");
    relayFlutterRateSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayFlutterDepthSlider, relayFlutterDepthLabel, "Fl.Dp", "%");
    relayFlutterDepthSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayChorusRateSlider, relayChorusRateLabel, "Ch.Rt", "Hz");
    relayChorusRateSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayChorusDepthSlider, relayChorusDepthLabel, "Ch.Dp", "%");
    relayChorusDepthSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayLoopHpfSlider, relayLoopHpfLabel, "Loop HPF", "Hz");
    relayLoopHpfSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayLoopLpfSlider, relayLoopLpfLabel, "Loop LPF", "Hz");
    relayLoopLpfSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
    setupSlider (relayOttAmountSlider, relayOttAmountLabel, "OTT", "%");
    relayOttAmountSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    setupSlider (relayOttTimeSlider, relayOttTimeLabel, "OTT t", "ms");
    relayOttTimeSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);

    addAndMakeVisible (relayEnableButton);
    addAndMakeVisible (relayClearButton);
    relayEnableButton.setClickingTogglesState (true);
    relayClearButton.setClickingTogglesState (true);
    relayEnableButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::aquamarine.withAlpha (0.55f));

    addAndMakeVisible (relayTimeModeCB);
    addAndMakeVisible (relayTimeModeLabel);
    relayTimeModeCB.addItemList (juce::StringArray { "Free", "Sync" }, 1);
    relayTimeModeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    relayTimeModeLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (relaySyncDivCB);
    addAndMakeVisible (relaySyncDivLabel);
    relaySyncDivCB.addItemList (juce::StringArray {
                                    "1/32", "1/16", "1/8", "1/8 dot", "1/4 tri", "1/4", "1/4 dot", "1/2 tri",
                                    "1/2", "1/2 dot", "1 bar", "2 bar", "4 bar" },
                                1);
    relaySyncDivLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    relaySyncDivLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (flipButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (muteButton);

    addAndMakeVisible (fftSizeCB);
    addAndMakeVisible (fftSizeLabel);
    fftSizeCB.addItemList (juce::StringArray { "256", "512", "1024", "2048", "4096", "16384", "32768" }, 1);
    fftSizeLabel.setText ("FFT", juce::dontSendNotification);
    fftSizeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    fftSizeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);
    latencyLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
    latencyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (relayRoundTripLabel);
    relayRoundTripLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
    relayRoundTripLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (numBandsCB);
    addAndMakeVisible (numBandsLabel);
    numBandsCB.addItemList (juce::StringArray { "1", "2", "3", "4", "5", "6" }, 1);
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
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.withAlpha (0.9f));
    muteButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    muteButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

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

    relayClearAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, "RELAY_CLEAR", relayClearButton);

    rebuildRelayAttachments();

    apvts.addParameterListener ("NUM_BANDS", this);
    apvts.addParameterListener ("ACTIVE_BAND", this);
    apvts.addParameterListener ("FFT_SIZE", this);

    addAndMakeVisible (spectrumViz);

    setSize (Layout::defaultWidth, Layout::defaultHeight);
    setResizeLimits (760, 780, 1600, 1100);
    updateLatencyLabel();
    updateRelayRoundTripLabel();
    startTimerHz (15);
}

void PluginEditor::maybeShowUpdateInfoModalOnLaunch()
{
#if BANDGATE_NO_MOONBASE
    return;
#else
    if (gUpdateInfoModalShown || processorRef.moonbaseClient == nullptr)
        return;

    if (! processorRef.moonbaseClient->isUpdateAvailable())
        return;

    const auto currentVersion = processorRef.moonbaseClient->getProductVersion();
    const auto latestVersion = processorRef.moonbaseClient->getCurrentReleaseVersion();
    gUpdateInfoModalShown = true;

    juce::MessageManager::callAsync ([this, currentVersion, latestVersion]
    {
        juce::NativeMessageBox::showYesNoBox (
            juce::MessageBoxIconType::InfoIcon,
            "BandGate Update Available",
            "The interwebs thinks that a newer BandGate release is available.\n\n"
            "Installed version: " + currentVersion + "\n"
            "Latest release: " + latestVersion + "\n\n"
            "Open License Manager now to update?",
            nullptr,
            juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<PluginEditor> (this)] (int result)
            {
                if (result == 1 && safeThis != nullptr && safeThis->processorRef.moonbaseClient != nullptr)
                    safeThis->processorRef.moonbaseClient->showActivationUi();
            }));
    });
#endif
}

PluginEditor::~PluginEditor()
{
    stopTimer();

    apvts.removeParameterListener ("NUM_BANDS", this);
    apvts.removeParameterListener ("ACTIVE_BAND", this);
    apvts.removeParameterListener ("FFT_SIZE", this);
}

void PluginEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "NUM_BANDS")
    {
        syncActiveBandToNumBands();
        rebuildGateSliderAttachments();
        rebuildRelayAttachments();
        updateRelayRoundTripLabel();
    }
    else if (parameterID == "ACTIVE_BAND")
    {
        rebuildGateSliderAttachments();
        rebuildRelayAttachments();
        updateRelayRoundTripLabel();
    }
    else if (parameterID == "FFT_SIZE")
    {
        updateLatencyLabel();
    }
    else if (parameterID.startsWith ("BAND") && parameterID.contains ("_RELAY_"))
    {
        updateRelayRoundTripLabel();
    }
}

void PluginEditor::timerCallback()
{
    updateRelayRoundTripLabel();
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
    muteAttachment.reset();

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
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, pfx + "MUTE", muteButton);
}

void PluginEditor::rebuildRelayAttachments()
{
    const int band = juce::jlimit (
        0, PluginProcessor::kMaxBands - 1,
        static_cast<int> (*apvts.getRawParameterValue ("ACTIVE_BAND")));
    const juce::String pfx = "BAND" + juce::String (band) + "_RELAY_";

    relayEnableAttachment.reset();
    relayTimeModeAttachment.reset();
    relaySyncDivAttachment.reset();
    relayTimeAttachment.reset();
    relayFeedbackAttachment.reset();
    relayInputGainAttachment.reset();
    relayMixAttachment.reset();
    relayDiffusionAttachment.reset();
    relayDampingAttachment.reset();
    relayFlutterRateAttachment.reset();
    relayFlutterDepthAttachment.reset();
    relayChorusRateAttachment.reset();
    relayChorusDepthAttachment.reset();
    relayLoopHpfAttachment.reset();
    relayLoopLpfAttachment.reset();
    relayOttAmountAttachment.reset();
    relayOttTimeAttachment.reset();

    relayEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, pfx + "ENABLE", relayEnableButton);
    relayTimeModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, pfx + "TIME_MODE", relayTimeModeCB);
    relaySyncDivAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, pfx + "TIME_SYNC_DIV", relaySyncDivCB);

    relayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "TIME_MS", relayTimeSlider);
    relayFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "FEEDBACK", relayFeedbackSlider);
    relayInputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "INPUT_GAIN", relayInputGainSlider);
    relayMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "MIX", relayMixSlider);
    relayDiffusionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "DIFFUSION_TIME", relayDiffusionSlider);
    relayDampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "DAMPING", relayDampingSlider);
    relayFlutterRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "FLUTTER_RATE", relayFlutterRateSlider);
    relayFlutterDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "FLUTTER_DEPTH", relayFlutterDepthSlider);
    relayChorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "CHORUS_RATE", relayChorusRateSlider);
    relayChorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "CHORUS_DEPTH", relayChorusDepthSlider);
    relayLoopHpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "LOOP_HPF", relayLoopHpfSlider);
    relayLoopLpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "LOOP_LPF", relayLoopLpfSlider);
    relayOttAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "OTT_AMOUNT", relayOttAmountSlider);
    relayOttTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pfx + "OTT_TIME", relayOttTimeSlider);
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
    hairlineInNextGap (Layout::gateH);
    hairlineInNextGap (Layout::relayH);
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
        auto metricStrip = bandRow.reduced (6, 6);
        relayRoundTripLabel.setBounds (metricStrip.removeFromBottom (18));
        latencyLabel.setBounds (metricStrip);
    }

    area.removeFromTop (Layout::gap);

    auto gateRow = area.removeFromTop (Layout::gateH);
    const int flipW = 72;
    const int soloW = 72;
    const int muteW = 72;
    const int gateKnobWidth = juce::jmax (92, (gateRow.getWidth() - flipW - soloW - muteW - Layout::gap * 3) / 3);

    thresholdSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10, 4));
    reductionSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10, 4));
    smoothingSlider.setBounds (gateRow.removeFromLeft (gateKnobWidth).reduced (10, 8));
    gateRow.removeFromLeft (Layout::gap);
    flipButton.setBounds (gateRow.removeFromLeft (flipW).withSizeKeepingCentre (flipW, 24));
    gateRow.removeFromLeft (Layout::gap);
    soloButton.setBounds (gateRow.removeFromLeft (soloW).withSizeKeepingCentre (soloW, 24));
    gateRow.removeFromLeft (Layout::gap);
    muteButton.setBounds (gateRow.removeFromLeft (muteW).withSizeKeepingCentre (muteW, 24));

    area.removeFromTop (Layout::gap);

    {
        auto relayBulk = area.removeFromTop (Layout::relayH);
        auto rr1 = relayBulk.removeFromTop (Layout::relayRowH).reduced (6, 2);

        relayEnableButton.setBounds (rr1.removeFromLeft (64).withSizeKeepingCentre (64, 28));
        rr1.removeFromLeft (6);

        relayClearButton.setBounds (rr1.removeFromLeft (82).withSizeKeepingCentre (82, 28));
        rr1.removeFromLeft (8);

        {
            auto c = rr1.removeFromLeft (128);
            relayTimeModeLabel.setBounds (c.removeFromLeft (54));
            relayTimeModeCB.setBounds (c.reduced (0, 26));
        }
        rr1.removeFromLeft (4);

        const int fbTarget = rr1.getWidth() / 5;
        const int knobWRow1 = juce::jmax (66, fbTarget);

        relayTimeSlider.setBounds (rr1.removeFromLeft (knobWRow1).reduced (4, 2));

        {
            auto c = rr1.removeFromLeft (154);
            relaySyncDivLabel.setBounds (c.removeFromLeft (46));
            relaySyncDivCB.setBounds (c.reduced (0, 26));
        }
        rr1.removeFromLeft (4);

        const int rk = juce::jmax (64, rr1.getWidth() / 3);
        relayFeedbackSlider.setBounds (rr1.removeFromLeft (rk).reduced (4, 2));
        relayMixSlider.setBounds (rr1.removeFromLeft (rk).reduced (4, 2));
        relayDiffusionSlider.setBounds (rr1.reduced (4, 2));

        relayBulk.removeFromTop (Layout::gap);
        auto rr2 = relayBulk.reduced (6, 4);
        constexpr int nk = 11;
        const int kw = juce::jmax (60, rr2.getWidth() / nk);

        relayInputGainSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayDampingSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayFlutterRateSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayFlutterDepthSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayChorusRateSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayChorusDepthSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayLoopHpfSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayLoopLpfSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayOttAmountSlider.setBounds (rr2.removeFromLeft (kw).reduced (2, 0));
        relayOttTimeSlider.setBounds (rr2.reduced (2, 0));
    }

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

void PluginEditor::setupVerticalSlider (juce::Slider& slider, juce::Label& label,
                                        const juce::String& labelText, const juce::String& suffix)
{
    addAndMakeVisible (slider);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setTextValueSuffix (" " + suffix);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);

    label.setText (labelText, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
}

void PluginEditor::updateLatencyLabel()
{
    const int latencySamples = processorRef.getLatencySamples();
    const double sr = processorRef.getSampleRate();
    const double latencyMs = (sr > 1.0) ? (1000.0 * (double) latencySamples / sr) : 0.0;
    latencyLabel.setText ("Latency: " + juce::String (latencySamples) + " smp / "
                              + juce::String (latencyMs, 2) + " ms",
                          juce::dontSendNotification);
}

void PluginEditor::updateRelayRoundTripLabel()
{
    const int band = juce::jlimit (
        0, PluginProcessor::kMaxBands - 1,
        static_cast<int> (*apvts.getRawParameterValue ("ACTIVE_BAND")));
    const juce::String relayEnId = "BAND" + juce::String (band) + "_RELAY_ENABLE";

    if (auto* en = apvts.getRawParameterValue (relayEnId))
    {
        if (*en <= 0.5f)
        {
            relayRoundTripLabel.setText ("Relay RT: -- (off)", juce::dontSendNotification);
            return;
        }
    }

    const float ms = processorRef.getEstimatedRelayRoundTripMs();
    relayRoundTripLabel.setText ("Relay RT: " + juce::String (ms, 1) + " ms",
                                juce::dontSendNotification);
}
