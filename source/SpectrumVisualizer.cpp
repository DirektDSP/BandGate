#include "SpectrumVisualizer.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <utility>
#include <vector>

namespace
{
    constexpr float kFreqPlotMinHz = 20.f;
    constexpr float kHitPx = 8.f;

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

    float dbToY (float db, juce::Rectangle<float> plot, float dbFloor, float dbCeil)
    {
        const float t = juce::jlimit (0.f, 1.f, (db - dbFloor) / (dbCeil - dbFloor));
        return plot.getBottom() - t * plot.getHeight();
    }

    float yToDb (float my, juce::Rectangle<float> plot, float dbFloor, float dbCeil)
    {
        const float t = juce::jlimit (0.f, 1.f, (plot.getBottom() - my) / juce::jmax (plot.getHeight(), 1.f));
        return dbFloor + t * (dbCeil - dbFloor);
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

    void copySortedCrossovers (juce::AudioProcessorValueTreeState& apvts, int nx,
                               std::array<float, (size_t) PluginProcessor::kMaxBands - 1>& cross)
    {
        for (int i = 0; i < nx; ++i)
            cross[(size_t) i] = apvts.getRawParameterValue ("CROSSOVER_" + juce::String (i))->load();
        for (int a = 0; a < nx - 1; ++a)
            for (int b = 0; b < nx - 1 - a; ++b)
                if (cross[(size_t) b] > cross[(size_t) b + 1])
                    std::swap (cross[(size_t) b], cross[(size_t) b + 1]);
    }

    int bandIndexForBinCenterHz (float fc, float fMax, const std::array<float, (size_t) PluginProcessor::kMaxBands - 1>& cross, int nb)
    {
        fc = juce::jlimit (kFreqPlotMinHz, fMax, fc);
        for (int b = 0; b < nb - 1; ++b)
            if (fc < cross[(size_t) b])
                return b;
        return nb - 1;
    }

    void bandXExtents (int band, float fMax, const std::array<float, (size_t) PluginProcessor::kMaxBands - 1>& cross, int nb,
                       const juce::Rectangle<float>& plot, float& xL, float& xR)
    {
        const float fLo = (band == 0) ? kFreqPlotMinHz : cross[(size_t) (band - 1)];
        const float fHi = (band == nb - 1) ? fMax : cross[(size_t) band];
        xL = logNormX (fLo, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        xR = logNormX (fHi, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        if (xR < xL)
            std::swap (xL, xR);
    }

    void setEditBandFromSpectrumClick (juce::AudioProcessorValueTreeState& apvts, int bandIndex)
    {
        const int nb = getNumBands (apvts);
        const int b = juce::jlimit (0, nb - 1, bandIndex);
        if (auto* p = apvts.getParameter ("ACTIVE_BAND"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) b));
    }

    int bandIndexAtMouseX (float mx, float fMax, const std::array<float, (size_t) PluginProcessor::kMaxBands - 1>& cross, int nb,
                           const juce::Rectangle<float>& plot)
    {
        for (int b = 0; b < nb; ++b)
        {
            float xL = 0, xR = 0;
            bandXExtents (b, fMax, cross, nb, plot, xL, xR);
            if (mx >= xL && mx <= xR)
                return b;
        }
        return -1;
    }

    struct SpectrumLayout
    {
        juce::Rectangle<float> plot, dbAxis, header, freqStrip;
    };

    SpectrumLayout makeLayout (juce::Rectangle<float> bounds)
    {
        auto outer = bounds.reduced (8.f, 8.f);
        SpectrumLayout L {};
        L.dbAxis = outer.removeFromLeft (36.f);
        L.header = outer.removeFromTop (16.f);
        L.freqStrip = outer.removeFromBottom (18.f);
        L.plot = outer;
        return L;
    }

    void paintBandLanes (juce::Graphics& g, const juce::Rectangle<float>& plot, float fMax, int nb,
                         const std::array<float, (size_t) PluginProcessor::kMaxBands - 1>& cross,
                         const std::array<bool, (size_t) PluginProcessor::kMaxBands>& solo,
                         bool anySoloActive)
    {
        const int nx = nb - 1;
        for (int band = 0; band < nb; ++band)
        {
            float xL = 0, xR = 0;
            bandXExtents (band, fMax, cross, nb, plot, xL, xR);
            const bool mutedBySolo = anySoloActive && ! solo[(size_t) band];
            auto lane = bandColour (band).withAlpha (0.11f);
            if (mutedBySolo)
                lane = lane.withMultipliedSaturation (0.08f).withMultipliedBrightness (0.7f);
            g.setColour (lane);
            g.fillRect (xL, plot.getY(), juce::jmax (xR - xL, 1.f), plot.getHeight());
        }

        g.setColour (juce::Colour (0xfff0f2f5).withAlpha (0.72f));
        for (int i = 0; i < nx; ++i)
        {
            const float xc = logNormX (cross[(size_t) i], kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
            g.fillRect (std::floor (xc) - 1.f, plot.getY(), 3.f, plot.getHeight());
        }
    }

    void paintBandHeaders (juce::Graphics& g, const SpectrumLayout& L, float fMax, int nb,
                           const std::array<float, (size_t) PluginProcessor::kMaxBands - 1>& cross,
                           const std::array<bool, (size_t) PluginProcessor::kMaxBands>& solo,
                           bool anySoloActive)
    {
        g.setFont (12.f);
        for (int band = 0; band < nb; ++band)
        {
            float xL = 0, xR = 0;
            bandXExtents (band, fMax, cross, nb, L.plot, xL, xR);
            const float cx = 0.5f * (xL + xR);
            const int rw = (int) juce::jmax (28.f, std::floor (0.5f * (xR - xL)));
            auto r = juce::Rectangle<int> ((int) std::floor (cx - 0.5f * (float) rw), (int) L.header.getY() + 1, rw, (int) L.header.getHeight() - 2);
            const juce::String name = (xR - xL) < 52.f ? ("B" + juce::String (band + 1))
                                                       : ("Band " + juce::String (band + 1));
            const bool mutedBySolo = anySoloActive && ! solo[(size_t) band];
            auto txt = bandColour (band).brighter (0.15f);
            if (mutedBySolo)
                txt = txt.withMultipliedSaturation (0.08f).withMultipliedBrightness (0.85f);
            g.setColour (txt);
            g.drawText (name, r, juce::Justification::centred, false);
        }
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

void SpectrumVisualizer::setVerticalDbRange (float minDb, float maxDb)
{
    minDb = juce::jlimit (-96.0f, 24.0f, minDb);
    maxDb = juce::jlimit (-96.0f, 24.0f, maxDb);
    if (minDb >= maxDb)
        maxDb = juce::jmin (24.0f, minDb + 12.0f);

    if (verticalMinDb == minDb && verticalMaxDb == maxDb)
        return;

    verticalMinDb = minDb;
    verticalMaxDb = maxDb;
    repaint();
}

float SpectrumVisualizer::getVerticalMinDb() const noexcept
{
    return verticalMinDb;
}

float SpectrumVisualizer::getVerticalMaxDb() const noexcept
{
    return verticalMaxDb;
}

juce::Rectangle<float> SpectrumVisualizer::getPlotArea (juce::Rectangle<float> bounds) const
{
    return makeLayout (bounds).plot;
}

void SpectrumVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff1c1f24));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (juce::Colour (0xff3a3f44).withAlpha (0.7f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    const auto L = makeLayout (bounds);
    const auto& plot = L.plot;
    const float dbFloor = verticalMinDb;
    const float dbCeil = verticalMaxDb;

    std::vector<float> magDb, gain;
    int fftSize = 0;
    double sampleRate = 44100.0;
    processor.fetchSpectralVisualData (magDb, gain, fftSize, sampleRate);

    const float nyquist = (float) (sampleRate * 0.5);
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);

    auto& apvts = processor.getApvts();
    const int nb = getNumBands (apvts);
    const int nx = nb - 1;
    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    std::array<bool, (size_t) PluginProcessor::kMaxBands> solo {};
    copySortedCrossovers (apvts, nx, cross);
    bool anySoloActive = false;
    for (int b = 0; b < nb; ++b)
    {
        solo[(size_t) b] = apvts.getRawParameterValue ("BAND" + juce::String (b) + "_SOLO")->load() > 0.5f;
        anySoloActive = anySoloActive || solo[(size_t) b];
    }

    paintBandLanes (g, plot, fMax, nb, cross, solo, anySoloActive);
    paintBandHeaders (g, L, fMax, nb, cross, solo, anySoloActive);

    g.setColour (juce::Colour (0xff3d4249).withAlpha (0.85f));
    for (float dbMark : { -96.f, -84.f, -72.f, -60.f, -48.f, -36.f, -24.f, -12.f, 0.f, 12.f, 24.f })
    {
        if (dbMark < dbFloor || dbMark > dbCeil)
            continue;
        const float y = dbToY (dbMark, plot, dbFloor, dbCeil);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    g.setColour (juce::Colour (0xff8b919a).withAlpha (0.9f));
    g.setFont (10.5f);
    for (float dbMark : { -96.f, -84.f, -72.f, -60.f, -48.f, -36.f, -24.f, -12.f, 0.f, 12.f, 24.f })
    {
        if (dbMark < dbFloor || dbMark > dbCeil)
            continue;
        const float y = dbToY (dbMark, plot, dbFloor, dbCeil);
        auto tr = juce::Rectangle<int> ((int) L.dbAxis.getX(), (int) (y - 7.f), (int) L.dbAxis.getWidth() - 2, 14);
        const juce::String text = dbMark > 0.f ? "+" + juce::String (juce::roundToInt (dbMark))
                                               : juce::String (juce::roundToInt (dbMark));
        g.drawText (text, tr, juce::Justification::centredRight, false);
    }

    if (fftSize <= 0 || magDb.empty())
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("No spectrum (transport stopped or idle)", bounds, juce::Justification::centred);
        return;
    }

    const int numBins = (int) magDb.size();

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float f0 = (float) bin * (float) sampleRate / (float) fftSize;
        const float f1 = (float) (bin + 1) * (float) sampleRate / (float) fftSize;
        if (f1 < kFreqPlotMinHz)
            continue;

        const float fc = 0.5f * (f0 + f1);
        const int bandIdx = bandIndexForBinCenterHz (fc, fMax, cross, nb);

        const float x0 = logNormX (juce::jmax (f0, kFreqPlotMinHz), kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        const float x1 = logNormX (f1, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        const float barLeft = juce::jmin (x0, x1);
        const float barW = juce::jmax (juce::jmax (x1, x0) - barLeft, 0.7f);

        const float mDb = magDb[(size_t) bin];
        const float gv = gain[(size_t) bin];
        const float yTop = dbToY (mDb, plot, dbFloor, dbCeil);
        const float yBottom = plot.getBottom();
        const float barH = juce::jmax (yBottom - yTop, 1.f);

        const auto base = bandColour (bandIdx);
        const float atten = juce::jlimit (0.f, 1.f, 1.f - gv);
        juce::Colour fill = base.interpolatedWith (juce::Colour (0xffff7040), atten * 0.85f);
        if (anySoloActive && ! solo[(size_t) bandIdx])
            fill = fill.withMultipliedSaturation (0.0f).withMultipliedBrightness (0.6f);
        g.setColour (fill.withAlpha (juce::jmap (atten, 0.f, 1.f, 0.42f, 0.78f)));
        g.fillRect (barLeft, yTop, barW, barH);
    }

    for (int b = 0; b < nb; ++b)
    {
        const juce::String pfx = "BAND" + juce::String (b) + "_";
        const float thrDb = apvts.getRawParameterValue (pfx + "THRESHOLD")->load();
        const float yThr = dbToY (thrDb, plot, dbFloor, dbCeil);
        float xL = 0, xR = 0;
        bandXExtents (b, fMax, cross, nb, plot, xL, xR);
        auto col = bandColour (b);
        if (anySoloActive && ! solo[(size_t) b])
            col = col.withMultipliedSaturation (0.0f).withMultipliedBrightness (0.72f);
        g.setColour (col.brighter (0.1f));
        g.drawLine (xL, yThr, xR, yThr, 2.2f);
        const float hx = juce::jlimit (xL + 6.f, xR - 6.f, xR - 5.f);
        g.setColour (col.brighter (0.25f));
        g.fillRoundedRectangle (hx - 5.f, yThr - 5.f, 10.f, 10.f, 3.f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (hx - 5.f, yThr - 5.f, 10.f, 10.f, 3.f, 1.f);
    }

    g.setColour (juce::Colour (0xff6a7078));
    g.setFont (11.f);
    for (float fMark : { 100.f, 500.f, 1000.f, 5000.f, 10000.f })
    {
        if (fMark > fMax * 1.01f)
            continue;

        const float x = logNormX (fMark, kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        juce::String label = fMark >= 1000.f ? juce::String (fMark / 1000.f, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " k"
                                            : juce::String (juce::roundToInt (fMark));
        g.setColour (juce::Colour (0xffb0b6bf));
        g.drawText (label,
                    juce::Rectangle<int> ((int) x - 22, (int) L.freqStrip.getY() + 1, 44, (int) L.freqStrip.getHeight() - 2),
                    juce::Justification::centred, false);
    }

    for (int i = 0; i < nx; ++i)
    {
        const float xc = logNormX (cross[(size_t) i], kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        juce::Path tri;
        tri.addTriangle (xc, plot.getY() + 3.f, xc - 7.f, plot.getY() + 15.f, xc + 7.f, plot.getY() + 15.f);
        g.setColour (juce::Colour (0xfff5f6f8).withAlpha (0.92f));
        g.fillPath (tri);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (tri, juce::PathStrokeType (1.f));

        const juce::String hzLabel = cross[(size_t) i] >= 1000.f
                                         ? juce::String (cross[(size_t) i] / 1000.f, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz"
                                         : juce::String (juce::roundToInt (cross[(size_t) i])) + " Hz";
        g.setFont (10.f);
        g.setColour (juce::Colour (0xffcfd4da));
        g.drawText (hzLabel, juce::Rectangle<int> ((int) xc - 40, (int) plot.getY() + 16, 80, 14), juce::Justification::centred, false);
    }
}

void SpectrumVisualizer::mouseDown (const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float nyquist = (float) (processor.getSampleRate() * 0.5);
    const float fMax = juce::jmax (kFreqPlotMinHz * 2.f, nyquist);
    auto plot = getPlotArea (bounds);
    const float dbFloor = verticalMinDb;
    const float dbCeil = verticalMaxDb;
    const float mx = (float) e.position.getX();
    const float my = (float) e.position.getY();

    auto& apvts = processor.getApvts();
    const int nb = getNumBands (apvts);
    const int nx = nb - 1;

    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    copySortedCrossovers (apvts, nx, cross);

    for (int i = 0; i < nx; ++i)
    {
        const float xc = logNormX (cross[(size_t) i], kFreqPlotMinHz, fMax, plot.getX(), plot.getWidth());
        if (std::abs (mx - xc) <= kHitPx && my >= plot.getY() && my <= plot.getBottom())
        {
            setEditBandFromSpectrumClick (apvts, i);
            dragKind = DragKind::crossover;
            dragCrossoverIndex = i;
            dragParam = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter ("CROSSOVER_" + juce::String (i)));
            if (dragParam != nullptr)
                dragParam->beginChangeGesture();
            return;
        }
    }

    int bestBand = -1;
    float bestDy = 1.0e9f;
    for (int b = 0; b < nb; ++b)
    {
        float xL = 0, xR = 0;
        bandXExtents (b, fMax, cross, nb, plot, xL, xR);
        if (mx < xL || mx > xR)
            continue;

        const juce::String pfx = "BAND" + juce::String (b) + "_";
        const float thrDb = apvts.getRawParameterValue (pfx + "THRESHOLD")->load();
        const float yThr = dbToY (thrDb, plot, dbFloor, dbCeil);
        const float dy = std::abs (my - yThr);
        if (dy < bestDy)
        {
            bestDy = dy;
            bestBand = b;
        }
    }

    if (bestBand >= 0 && bestDy <= kHitPx)
    {
        setEditBandFromSpectrumClick (apvts, bestBand);
        dragKind = DragKind::threshold;
        dragThresholdBand = bestBand;
        dragParam = dynamic_cast<juce::RangedAudioParameter*> (
            apvts.getParameter ("BAND" + juce::String (bestBand) + "_THRESHOLD"));
        if (dragParam != nullptr)
            dragParam->beginChangeGesture();
        return;
    }

    if (mx >= plot.getX() && mx <= plot.getRight() && my >= plot.getY() && my <= plot.getBottom())
    {
        const int b = bandIndexAtMouseX (mx, fMax, cross, nb, plot);
        if (b >= 0)
            setEditBandFromSpectrumClick (apvts, b);
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
    const float dbFloor = verticalMinDb;
    const float dbCeil = verticalMaxDb;
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
        float db = yToDb (my, plot, dbFloor, dbCeil);
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
    const float dbFloor = verticalMinDb;
    const float dbCeil = verticalMaxDb;
    const float mx = (float) e.position.getX();
    const float my = (float) e.position.getY();

    auto& apvts = processor.getApvts();
    const int nb = getNumBands (apvts);
    const int nx = nb - 1;

    std::array<float, (size_t) PluginProcessor::kMaxBands - 1> cross {};
    copySortedCrossovers (apvts, nx, cross);

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
        float xL = 0, xR = 0;
        bandXExtents (b, fMax, cross, nb, plot, xL, xR);
        if (mx < xL || mx > xR)
            continue;

        const juce::String pfx = "BAND" + juce::String (b) + "_";
        const float thrDb = apvts.getRawParameterValue (pfx + "THRESHOLD")->load();
        const float yThr = dbToY (thrDb, plot, dbFloor, dbCeil);
        if (std::abs (my - yThr) <= kHitPx)
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
