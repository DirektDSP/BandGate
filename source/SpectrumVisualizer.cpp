#include "SpectrumVisualizer.h"
#include "PluginProcessor.h"

#include <vector>

namespace
{
    constexpr float kFreqPlotMinHz = 20.f;
    constexpr float kDbFloor = -100.f;
    constexpr float kDbCeil = 6.f;

    float logNormX (float hz, float fMin, float fMax, float x0, float width)
    {
        hz = juce::jlimit (fMin, fMax, hz);
        const float t = (std::log10 (hz) - std::log10 (fMin)) / (std::log10 (fMax) - std::log10 (fMin));
        return x0 + t * width;
    }

    float dbToY (float db, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.f, 1.f, (db - kDbFloor) / (kDbCeil - kDbFloor));
        return plot.getBottom() - t * plot.getHeight();
    }
} // namespace

SpectrumVisualizer::SpectrumVisualizer (PluginProcessor& p)
    : processor (p)
{
    startTimerHz (33);
}

SpectrumVisualizer::~SpectrumVisualizer()
{
    stopTimer();
}

void SpectrumVisualizer::timerCallback()
{
    repaint();
}

void SpectrumVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff1a1a1e));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (juce::Colour (0xff3a3a42));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    std::vector<float> magDb, gain;
    int fftSize = 0;
    double sampleRate = 44100.0;
    processor.fetchSpectralVisualData (magDb, gain, fftSize, sampleRate);

    const float nyquist = (float) (sampleRate * 0.5);
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);

    auto plot = bounds.reduced (8.f, 8.f);
    const auto tickArea = plot.removeFromBottom (16.f);
    plot.removeFromTop (2.f);

    g.setColour (juce::Colour (0xff2e2e34));
    for (float dbMark : { -80.f, -60.f, -40.f, -20.f, 0.f })
    {
        const float y = dbToY (dbMark, plot);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    g.setColour (juce::Colour (0xff55555e));
    g.setFont ((float) 11.f);
    for (float fMark : { 100.f, 500.f, 1000.f, 5000.f, 10000.f })
    {
        if (fMark > fMax * 1.01f)
            continue;

        const float x = logNormX (fMark, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        juce::String label = fMark >= 1000.f ? juce::String (fMark / 1000.f, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " k"
                                            : juce::String (juce::roundToInt (fMark));
        g.drawText (label, juce::Rectangle<int> ((int) x - 22, (int) tickArea.getY(), 44, (int) tickArea.getHeight()),
                    juce::Justification::centred, false);
    }

    float thrDb = -60.f;
    if (auto* thr = processor.getApvts().getRawParameterValue ("THRESHOLD"))
        thrDb = thr->load();

    if (fftSize <= 0 || magDb.empty())
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("No spectrum (transport stopped or idle)", bounds, juce::Justification::centred);
        return;
    }

    const int numBins = (int) magDb.size();
    const float yThr = dbToY (thrDb, plot);

    g.setColour (juce::Colour (0xffffe08a).withAlpha (0.85f));
    g.drawLine (plot.getX(), yThr, plot.getRight(), yThr, 1.4f);

    g.setColour (juce::Colour (0xffffe08a).withAlpha (0.5f));
    g.setFont (10.f);
    g.drawText ("Threshold", juce::Rectangle<int> ((int) plot.getRight() - 72, (int) yThr - 14, 70, 12),
                juce::Justification::right, false);

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float f0 = (float) bin * (float) sampleRate / (float) fftSize;
        const float f1 = (float) (bin + 1) * (float) sampleRate / (float) fftSize;
        if (f1 < kFreqPlotMinHz)
            continue;

        const float x0 = logNormX (juce::jmax (f0, kFreqPlotMinHz), kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        const float x1 = logNormX (f1, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        const float barLeft = juce::jmin (x0, x1);
        const float barW = juce::jmax (juce::jmax (x1, x0) - barLeft, 0.7f);

        const float mDb = magDb[(size_t) bin];
        const float gv = gain[(size_t) bin];
        const float yTop = dbToY (mDb, plot);
        const float yBottom = plot.getBottom();
        const float barH = juce::jmax (yBottom - yTop, 1.f);

        const bool gated = (gv < 0.98f) || (mDb < thrDb - 0.25f);
        const juce::Colour cold (0xff4ecdc4);
        const juce::Colour hot (0xffff6b6b);
        const juce::Colour c = gated ? hot : cold;

        g.setColour (c.withAlpha (gated ? 0.92f : 0.55f));
        g.fillRect (barLeft, yTop, barW, barH);
    }
}
