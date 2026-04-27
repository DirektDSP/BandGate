#include "SpectrumVisualizer.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace
{
    constexpr float kFreqPlotMinHz = 20.f;
    constexpr float kDbFloor = -100.f;
    constexpr float kDbCeil = 6.f;
    constexpr float kHitPx = 7.f;

    float logNormX (float hz, float fMin, float fMax, float x0, float width)
    {
        hz = juce::jlimit (fMin, fMax, hz);
        const float t = (std::log10 (hz) - std::log10 (fMin)) / (std::log10 (fMax) - std::log10 (fMin));
        return x0 + t * width;
    }

    float xToHz (float mx, float fMin, float fMax, const juce::Rectangle<float>& plot)
    {
        const float t = juce::jlimit (0.f, 1.f, (mx - plot.getX()) / juce::jmax (plot.getWidth(), 1.f));
        const float logLo = std::log10 (fMin);
        const float logHi = std::log10 (fMax);
        return std::pow (10.f, logLo + t * (logHi - logLo));
    }

    float dbToY (float db, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.f, 1.f, (db - kDbFloor) / (kDbCeil - kDbFloor));
        return plot.getBottom() - t * plot.getHeight();
    }

    float yToDb (float my, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.f, 1.f, (plot.getBottom() - my) / juce::jmax (plot.getHeight(), 1.f));
        return kDbFloor + t * (kDbCeil - kDbFloor);
    }

    int getNumBands (juce::AudioProcessorValueTreeState& apvts)
    {
        const int c = static_cast<int> (*apvts.getRawParameterValue ("NUM_BANDS"));
        return juce::jlimit (2, PluginProcessor::kMaxBands, c + 2);
    }

    juce::Colour bandColour (int bandIndex)
    {
        static const juce::uint32 cols[] = {
            0xff4ecdc4, 0xffffe66d, 0xffff6b6b, 0xffa29bfe, 0xff95e1d3, 0xfff38181
        };
        return juce::Colour (cols[(size_t) bandIndex % (sizeof (cols) / sizeof (cols[0]))]);
    }
} // namespace

SpectrumVisualizer::SpectrumVisualizer (PluginProcessor& p)
    : processor (p)
{
    startTimerHz (33);
    setRepaintsOnMouseActivity (true);
}

SpectrumVisualizer::~SpectrumVisualizer()
{
    stopTimer();
}

void SpectrumVisualizer::timerCallback()
{
    repaint();
}

juce::Rectangle<float> SpectrumVisualizer::getPlotArea (juce::Rectangle<float> bounds) const
{
    auto plot = bounds.reduced (8.f, 8.f);
    plot.removeFromBottom (16.f);
    plot.removeFromTop (2.f);
    return plot;
}

void SpectrumVisualizer::paintBandOverlapBars (juce::Graphics& g, juce::Rectangle<float> plot,
                                               double sampleRate, float nyquist) const
{
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);
    const int nb = getNumBands (processor.getApvts());
    const int nx = nb - 1;

    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    for (int i = 0; i < nx; ++i)
        cross[(size_t) i] = processor.getApvts().getRawParameterValue ("CROSSOVER_" + juce::String (i))->load();

    for (int a = 0; a < nx - 1; ++a)
        for (int b = 0; b < nx - 1 - a; ++b)
            if (cross[(size_t) b] > cross[(size_t) b + 1])
                std::swap (cross[(size_t) b], cross[(size_t) b + 1]);

    const float stripH = 2.8f;
    const float stripY0 = plot.getBottom() - stripH * (float) nb - 4.f;

    for (int band = 0; band < nb; ++band)
    {
        const float fLo = (band == 0) ? kFreqPlotMinHz : cross[(size_t) (band - 1)];
        const float fHi = (band == nb - 1) ? fMax : cross[(size_t) band];
        const float x0 = logNormX (fLo, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        const float x1 = logNormX (fHi, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        g.setColour (bandColour (band).withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x0, x1), stripY0 + (float) band * stripH,
                                              juce::jmax (4.f, std::abs (x1 - x0)), stripH));
    }

    // LR transition overlap: vertical shaded column (read as overlap region on log axis)
    for (int i = 0; i < nx; ++i)
    {
        const float fc = cross[(size_t) i];
        const float fLo = fc * 0.75f;
        const float fHi = fc * 1.333f;
        const float x0 = logNormX (fLo, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        const float x1 = logNormX (fHi, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x0, x1), plot.getY(),
                                              juce::jmax (3.f, std::abs (x1 - x0)), plot.getHeight()));
    }
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

    auto plot = getPlotArea (bounds);
    const auto tickArea = juce::Rectangle<float> (bounds.getX() + 8.f, plot.getBottom(), bounds.getWidth() - 16.f, 16.f);

    g.setColour (juce::Colour (0xff2e2e34));
    for (float dbMark : { -80.f, -60.f, -40.f, -20.f, 0.f })
    {
        const float y = dbToY (dbMark, plot);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    g.setColour (juce::Colour (0xff55555e));
    g.setFont (11.f);
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

    paintBandOverlapBars (g, plot, sampleRate, nyquist);

    if (fftSize <= 0 || magDb.empty())
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("No spectrum (transport stopped or idle)", bounds, juce::Justification::centred);
        return;
    }

    const int numBins = (int) magDb.size();
    const int nb = getNumBands (processor.getApvts());

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

        float nearestThr = -60.f;
        float minDist = 1.0e9f;
        for (int b = 0; b < nb; ++b)
        {
            const juce::String pfx = "BAND" + juce::String (b) + "_";
            const float tDb = processor.getApvts().getRawParameterValue (pfx + "THRESHOLD")->load();
            const float d = std::abs (mDb - tDb);
            if (d < minDist)
            {
                minDist = d;
                nearestThr = tDb;
            }
        }

        const bool gated = (gv < 0.98f) || (mDb < nearestThr - 0.25f);
        const juce::Colour cold (0xff4ecdc4);
        const juce::Colour hot (0xffff6b6b);
        const juce::Colour c = gated ? hot : cold;

        g.setColour (c.withAlpha (gated ? 0.92f : 0.55f));
        g.fillRect (barLeft, yTop, barW, barH);
    }

    // Per-band threshold lines + handles
    for (int b = 0; b < nb; ++b)
    {
        const juce::String pfx = "BAND" + juce::String (b) + "_";
        const float thrDb = processor.getApvts().getRawParameterValue (pfx + "THRESHOLD")->load();
        const float yThr = dbToY (thrDb, plot);
        const auto col = bandColour (b);
        g.setColour (col.withAlpha (0.9f));
        g.drawLine (plot.getX(), yThr, plot.getRight(), yThr, 1.2f);
        const float hx = plot.getRight() - 6.f;
        g.fillEllipse (hx - 5.f, yThr - 5.f, 10.f, 10.f);
    }

    // Crossover handles (top of plot)
    const int nx = nb - 1;
    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    for (int i = 0; i < nx; ++i)
        cross[(size_t) i] = processor.getApvts().getRawParameterValue ("CROSSOVER_" + juce::String (i))->load();
    for (int a = 0; a < nx - 1; ++a)
        for (int b = 0; b < nx - 1 - a; ++b)
            if (cross[(size_t) b] > cross[(size_t) b + 1])
                std::swap (cross[(size_t) b], cross[(size_t) b + 1]);

    for (int i = 0; i < nx; ++i)
    {
        const float xc = logNormX (cross[(size_t) i], kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        juce::Path tri;
        tri.addTriangle (xc, plot.getY() + 2.f, xc - 6.f, plot.getY() + 14.f, xc + 6.f, plot.getY() + 14.f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillPath (tri);
    }
}

void SpectrumVisualizer::mouseDown (const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float nyquist = (float) (processor.getSampleRate() * 0.5);
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);
    auto plot = getPlotArea (bounds);
    const float mx = (float) e.position.getX();
    const float my = (float) e.position.getY();

    const int nb = getNumBands (processor.getApvts());
    const int nx = nb - 1;

    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    for (int i = 0; i < nx; ++i)
        cross[(size_t) i] = processor.getApvts().getRawParameterValue ("CROSSOVER_" + juce::String (i))->load();
    for (int a = 0; a < nx - 1; ++a)
        for (int b = 0; b < nx - 1 - a; ++b)
            if (cross[(size_t) b] > cross[(size_t) b + 1])
                std::swap (cross[(size_t) b], cross[(size_t) b + 1]);

    for (int i = 0; i < nx; ++i)
    {
        const float xc = logNormX (cross[(size_t) i], kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        if (std::abs (mx - xc) <= kHitPx && my >= plot.getY() && my <= plot.getBottom())
        {
            dragKind = DragKind::crossover;
            dragCrossoverIndex = i;
            dragParam = dynamic_cast<juce::RangedAudioParameter*> (processor.getApvts().getParameter ("CROSSOVER_" + juce::String (i)));
            if (dragParam != nullptr)
                dragParam->beginChangeGesture();
            return;
        }
    }

    int bestBand = -1;
    float bestDy = 1.0e9f;
    for (int b = 0; b < nb; ++b)
    {
        const juce::String pfx = "BAND" + juce::String (b) + "_";
        const float thrDb = processor.getApvts().getRawParameterValue (pfx + "THRESHOLD")->load();
        const float yThr = dbToY (thrDb, plot);
        const float dy = std::abs (my - yThr);
        if (dy < bestDy && mx >= plot.getX() && mx <= plot.getRight())
        {
            bestDy = dy;
            bestBand = b;
        }
    }

    if (bestBand >= 0 && bestDy <= kHitPx)
    {
        dragKind = DragKind::threshold;
        dragThresholdBand = bestBand;
        dragParam = dynamic_cast<juce::RangedAudioParameter*> (
            processor.getApvts().getParameter ("BAND" + juce::String (bestBand) + "_THRESHOLD"));
        if (dragParam != nullptr)
            dragParam->beginChangeGesture();
    }
}

void SpectrumVisualizer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKind == DragKind::none || dragParam == nullptr)
        return;

    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float nyquist = (float) (processor.getSampleRate() * 0.5);
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);
    auto plot = getPlotArea (bounds);
    const float mx = (float) e.position.getX();
    const float my = (float) e.position.getY();

    if (dragKind == DragKind::crossover)
    {
        float hz = xToHz (mx, kFreqPlotMinHz, fMax, plot);
        hz = juce::jlimit (40.f, juce::jmin (20000.f, nyquist * 0.48f), hz);
        dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (hz));
    }
    else
    {
        float db = yToDb (my, plot);
        db = juce::jlimit (-100.f, 0.f, db);
        dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (db));
    }
}

void SpectrumVisualizer::mouseMove (const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float nyquist = (float) (processor.getSampleRate() * 0.5);
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);
    auto plot = getPlotArea (bounds);
    const float mx = (float) e.position.getX();
    const float my = (float) e.position.getY();

    const int nb = getNumBands (processor.getApvts());
    const int nx = nb - 1;

    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    for (int i = 0; i < nx; ++i)
        cross[(size_t) i] = processor.getApvts().getRawParameterValue ("CROSSOVER_" + juce::String (i))->load();
    for (int a = 0; a < nx - 1; ++a)
        for (int b = 0; b < nx - 1 - a; ++b)
            if (cross[(size_t) b] > cross[(size_t) b + 1])
                std::swap (cross[(size_t) b], cross[(size_t) b + 1]);

    for (int i = 0; i < nx; ++i)
    {
        const float xc = logNormX (cross[(size_t) i], kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        if (std::abs (mx - xc) <= kHitPx && my >= plot.getY() && my <= plot.getBottom())
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    for (int b = 0; b < nb; ++b)
    {
        const juce::String pfx = "BAND" + juce::String (b) + "_";
        const float thrDb = processor.getApvts().getRawParameterValue (pfx + "THRESHOLD")->load();
        const float yThr = dbToY (thrDb, plot);
        if (std::abs (my - yThr) <= kHitPx && mx >= plot.getX() && mx <= plot.getRight())
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            return;
        }
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void SpectrumVisualizer::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void SpectrumVisualizer::mouseUp (const juce::MouseEvent&)
{
    if (dragParam != nullptr)
        dragParam->endChangeGesture();
    dragParam = nullptr;
    dragKind = DragKind::none;
}
