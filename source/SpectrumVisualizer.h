#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginProcessor;

class SpectrumVisualizer : public juce::Component,
                           private juce::Timer
{
public:
    explicit SpectrumVisualizer (PluginProcessor&);
    ~SpectrumVisualizer() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumVisualizer)
};
