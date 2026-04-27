#pragma once

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
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::Rectangle<float> getPlotArea (juce::Rectangle<float> bounds) const;
    void paintBandOverlapBars (juce::Graphics& g, juce::Rectangle<float> plot,
                               double sampleRate, float nyquist) const;

    PluginProcessor& processor;
    juce::RangedAudioParameter* dragParam = nullptr;

    enum class DragKind { none, crossover, threshold };
    DragKind dragKind = DragKind::none;
    int dragCrossoverIndex = 0;
    int dragThresholdBand = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumVisualizer)
};
