#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace juce
{
    class RangedAudioParameter;
}

class PluginProcessor;

class SpectrumVisualizer : public juce::Component,
                           private juce::Timer
{
public:
    explicit SpectrumVisualizer (PluginProcessor&);
    ~SpectrumVisualizer() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setVerticalDbRange (float minDb, float maxDb);
    float getVerticalMinDb() const noexcept;
    float getVerticalMaxDb() const noexcept;

private:
    void timerCallback() override;

    juce::Rectangle<float> getPlotArea (juce::Rectangle<float> bounds) const;

    PluginProcessor& processor;
    juce::RangedAudioParameter* dragParam = nullptr;

    enum class DragKind { none, crossover, threshold };
    DragKind dragKind = DragKind::none;
    int dragCrossoverIndex = 0;
    int dragThresholdBand = 0;
    float verticalMinDb = -96.0f;
    float verticalMaxDb = 24.0f;
    std::vector<float> smoothedMagDb;
    std::vector<float> smoothedGain;
    bool smoothingPrimed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumVisualizer)
};
