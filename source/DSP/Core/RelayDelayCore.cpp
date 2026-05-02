#include "RelayDelayCore.h"

#include "../Utils/DSPUtils.h"

#include <climits>
#include <cmath>
#include <limits>

namespace DSP {
namespace Core {
namespace
{
    float syncDividerBeats (int syncDivIdx) noexcept
    {
        constexpr float table[] =
        {
            1.f / 8.f, 1.f / 4.f, 1.f / 2.f, (1.f / 2.f) * 1.5f,
            2.f / 3.f, 1.f, 1.5f, 4.f / 3.f, 2.f, 3.f, 4.f, 8.f, 16.f
        };
        return table[juce::jlimit (0, (int) juce::numElementsInArray (table) - 1, syncDivIdx)];
    }

    inline float sane (float z) noexcept
    {
        return std::isfinite (z) ? z : 0.f;
    }
} // namespace

void RelayDelayCore::AllPassSection::setLengthAndCoeff (int lengthSamples, float coeffG)
{
    const int capped = juce::jlimit (5, 2048, lengthSamples);
    const float ng = juce::jlimit (0.45f, 0.78f, coeffG);

    if ((int) buf.size() != capped)
    {
        buf.assign ((size_t) capped, 0.0f);
        w = 0;
        baseG = ng;
        return;
    }

    const float coefDelta = std::abs (ng - baseG);
    baseG = ng;

    if (coefDelta > 0.03f)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        w = 0;
    }
}

void RelayDelayCore::AllPassSection::reset() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.f);
    w = 0;
}

float RelayDelayCore::AllPassSection::processSample (float x, float coeffApplied) noexcept
{
    if (buf.empty())
        return x;

    const float cg = juce::jlimit (0.35f, 0.795f, coeffApplied);

    const uint32_t n = (uint32_t) buf.size();
    const float delayed = buf[(size_t) w];
    const float y = delayed - cg * x;
    buf[(size_t) w] = x + cg * y;
    w = (w + 1u) % n;
    return y;
}

float RelayDelayCore::computeTargetDelayMs (const RelayRuntimeParams& p) const noexcept
{
    if (p.timeMode == 1 && p.hostBpmValid && p.hostBpm > 1.e-6)
    {
        const float beatMs = float (60000.0 / p.hostBpm);
        return juce::jlimit (1.f, 8000.f, syncDividerBeats (p.syncDivIndex) * beatMs);
    }

    return juce::jlimit (1.f, 8000.f, p.timeMs);
}

float RelayDelayCore::estimateDiffuseSmearSamples (float diffusionMsScaled) const noexcept
{
    static constexpr int bases[kApfStages] = { 17, 41, 79, 127, 197, 311 };
    float sum = 0.f;

    for (int d : bases)
        sum += (float) juce::jmax (8, int ((float) d * diffusionMsScaled));

    return sum;
}

float RelayDelayCore::computeDiffuseExtraMs() const noexcept
{
    const float samp = estimateDiffuseSmearSamples (diffusionScaleTarget);
    return (float) (((double) samp / juce::jmax (1.0, sampleRate)) * 1000.0 * 0.38);
}

int RelayDelayCore::computeFilterFingerprint (const RelayRuntimeParams& p) const noexcept
{
    const int dpi = (int) std::lround ((double) p.dampingPct);
    const int hpi = juce::jlimit (16, (int) (sampleRate * 0.459), (int) std::lround ((double) p.loopHpfHz));
    const int lpi =
        juce::jlimit (300, (int) (sampleRate * 0.489), (int) std::lround ((double) (p.loopLpfHz * 0.025f)));

    return dpi ^ (hpi * 734187) ^ (lpi * 219413);
}

void RelayDelayCore::updateDiffusionFromMs (float diffusionMsTarget, float channelDetuneSigned) noexcept
{
    const float scl = juce::jlimit (1.f, 750.f, diffusionMsTarget);
    diffusionScaleTarget = juce::jmap (scl, 1.f, 750.f, 0.48f, 3.2f);

    static constexpr int bases[kApfStages] = { 17, 41, 79, 127, 197, 311 };
    static constexpr uint8_t stagger[kApfStages] = { 0, 3, 5, 7, 13, 19 };

    const float coeffBase = juce::jmap (scl, 1.f, 750.f, 0.58f, 0.76f);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float det = channelDetuneSigned * (float) (ch == 0 ? -1.f : 1.f);

        for (int si = 0; si < kApfStages; ++si)
        {
            const int len =
                juce::jlimit (8, 4096,
                             int (((float) bases[si] * diffusionScaleTarget)
                                  + float (det * (float) stagger[si] * 3.f)));
            const float c = coeffBase + 0.02f * float (si) / (float) kApfStages * det;

            diffuser[(size_t) ch][(size_t) si].setLengthAndCoeff (
                len,
                juce::jlimit (0.52f, 0.785f, c));
        }
    }
}

void RelayDelayCore::rebuildDamping (float dampingPercent, float hpfHz, float lpfHz) noexcept
{
    const float pc = juce::jlimit (0.f, 100.f, dampingPercent) / 100.f;
    const float bright = juce::jmin ((float) (sampleRate * 0.48), 17800.f);
    const float dampingCutHz = bright * std::pow (460.f / bright, pc);

    const float hpCut = juce::jlimit (16.f, float (sampleRate * 0.458), hpfHz);
    const float lpCut = juce::jlimit (350.f, float (sampleRate * 0.49), lpfHz);

    for (int ch = 0; ch < 2; ++ch)
    {
        *loopHpf[(size_t) ch].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, hpCut, 0.707f);
        *loopLpf[(size_t) ch].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, lpCut, 0.707f);

        auto lpA = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, dampingCutHz, 0.707f);

        dampingA[(size_t) ch].coefficients = lpA;
        dampingB[(size_t) ch].coefficients = lpA;
    }
}

void RelayDelayCore::rebuildOttCrossovers() noexcept
{
    const float fcLow = 160.f;
    const float fcHigh = 3900.f;

    for (int ch = 0; ch < 2; ++ch)
    {
        *ottLowLp[(size_t) ch].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, fcLow, 0.707f);

        *ottMidHp[(size_t) ch].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, fcLow * 1.02f, 0.707f);
        *ottMidLp[(size_t) ch].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, fcHigh * 0.975f, 0.707f);

        *ottHighHp[(size_t) ch].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, fcHigh, 0.707f);
    }
}

void RelayDelayCore::updateOttTiming (float ottTimeMs) noexcept
{
    const float tMs = juce::jlimit (1.f, 900.f, ottTimeMs);
    const double sr = juce::jmax (8000.0, sampleRate);

    const float atkMs = juce::jmax (2.5f, tMs * 0.42f);
    const float relMs = juce::jmax (18.f, tMs * 1.85f);

    attackCoeff =
        float (std::exp (-1000.0 / (double) atkMs / sr));
    releaseCoeff =
        float (std::exp (-1000.0 / (double) relMs / sr));

    attackCoeff = juce::jlimit (0.0015f, 0.93f, attackCoeff);
    releaseCoeff = juce::jlimit (0.0004f, 0.9975f, releaseCoeff);
}

float RelayDelayCore::ottMult (float env, float amtNorm) const noexcept
{
    if (amtNorm < 1.e-5f)
        return 1.f;

    const float t = 0.055f;
    const float e = juce::jmax (env, 1.0e-12f);
    const float shape = std::sqrt (t / (e + t));

    const float biased = std::clamp (shape * 1.28f, 0.52f, 1.92f);

    return 1.f + amtNorm * (biased - 1.f);
}

float RelayDelayCore::processOtt3 (int channel,
                                   float xl,
                                   float xm,
                                   float xh,
                                   float ottAmtNorm) noexcept
{
    auto upd = [this] (float& e, float s)
    {
        const float a = std::abs (sane (s));
        if (a > e)
            e = attackCoeff * e + (1.f - attackCoeff) * a;
        else
            e = releaseCoeff * e + (1.f - releaseCoeff) * a;
    };

    upd (ottEnv[(size_t) channel][0], xl);
    upd (ottEnv[(size_t) channel][1], xm);
    upd (ottEnv[(size_t) channel][2], xh);

    xl *= ottMult (ottEnv[(size_t) channel][0], ottAmtNorm);
    xm *= ottMult (ottEnv[(size_t) channel][1], ottAmtNorm);
    xh *= ottMult (ottEnv[(size_t) channel][2], ottAmtNorm);

    return sane (xl + xm + xh);
}

float RelayDelayCore::feedbackGuard (float z) noexcept
{
    constexpr float ceil = 2.88f;

    const float y = sane (z) + 1.0e-10f;

    return Utils::DSPUtils::softClip (std::clamp (y, -ceil, ceil));
}

void RelayDelayCore::advanceModPhases() noexcept
{
    const float sr = float (sampleRate > 80.0 ? sampleRate : 44100.0);
    const float dF = twoPi / sr;

    flutterPhase += dF * juce::jlimit (0.02f, 36.f, lastRt.flutterRateHz);
    chorusPhase += dF * juce::jlimit (0.01f, 20.f, lastRt.chorusRateHz);

    flutterPhase = std::fmod (flutterPhase, twoPi);
    chorusPhase = std::fmod (chorusPhase, twoPi);
    if (flutterPhase < 0.f)
        flutterPhase += twoPi;
    if (chorusPhase < 0.f)
        chorusPhase += twoPi;
}

float RelayDelayCore::chorusProcessTap (int ch, float tapped) noexcept
{
    auto& dl = chorusDl[(size_t) ch];

    const float depth = Utils::DSPUtils::percentageToNormalized (lastRt.chorusDepthPct);

    if (depth < 1.0e-3f)
        return tapped;

    const float maxDel = (float) juce::jmax (32, dl.getMaximumDelayInSamples () - 8);
    const float cent =
        (float) juce::jlimit (64, (int) maxDel - 48, chorusCentreSamples[ch]);

    const float sweep =
        depth * 0.46f * (float) dl.getMaximumDelayInSamples () * 0.014f * std::sin (chorusPhase);

    float readPos = cent + sweep;
    readPos = std::clamp (readPos, 8.f, maxDel);

    const float wet = sane (dl.popSample (0, readPos, true));
    dl.pushSample (0, tapped);

    return tapped * (1.f - depth) + wet * depth;
}

void RelayDelayCore::resetInternalBuffersOnly() noexcept
{
    delay[0].reset();
    delay[1].reset();

    chorusDl[0].reset();
    chorusDl[1].reset();

    for (int ch = 0; ch < 2; ++ch)
        for (int si = 0; si < kApfStages; ++si)
            diffuser[(size_t) ch][(size_t) si].reset();

    for (auto& f : loopHpf)
        f.reset();
    for (auto& f : loopLpf)
        f.reset();
    for (auto& f : dampingA)
        f.reset();
    for (auto& f : dampingB)
        f.reset();

    for (auto& f : ottLowLp)
        f.reset();
    for (auto& f : ottMidHp)
        f.reset();
    for (auto& f : ottMidLp)
        f.reset();
    for (auto& f : ottHighHp)
        f.reset();

    for (auto& chEv : ottEnv)
        std::fill (chEv.begin(), chEv.end(), 0.f);

    delaySamplesSmooth.snapToTargetValue();
    feedbackSmooth.snapToTargetValue();
    relayMixSmooth.snapToTargetValue();
    injectorSmooth.snapToTargetValue();
    ottAmountSmooth.snapToTargetValue();

    flutterPhase = 0.f;
    chorusPhase = 0.f;
}

void RelayDelayCore::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    prepared = false;

    const int maxSam = int (std::ceil (sampleRate * 8.25)) + 8192;
    maxDelayAllocated = (uint32_t) maxSam;

    juce::dsp::ProcessSpec mono { sampleRate,
                                  juce::jmax<juce::uint32> (1u, spec.maximumBlockSize),
                                  1 };

    delay[0].prepare (mono);
    delay[0].setMaximumDelayInSamples (maxSam);
    delay[1].prepare (mono);
    delay[1].setMaximumDelayInSamples (maxSam);

    const int chorMax = juce::jlimit (512, 16384, int (sampleRate * 0.07) + 384);

    chorusDl[0].prepare (mono);
    chorusDl[0].setMaximumDelayInSamples (chorMax);
    chorusDl[1].prepare (mono);
    chorusDl[1].setMaximumDelayInSamples (chorMax);

    chorusCentreSamples[0] = juce::jlimit (180, chorMax - 220, int (sampleRate * 0.032));
    chorusCentreSamples[1] = juce::jlimit (chorusCentreSamples[0] + 4,
                                         chorMax - 200,
                                         chorusCentreSamples[0] + juce::jlimit (8, 120, int (sampleRate * 0.0025f)));

    for (auto& f : loopHpf)
    {
        f.reset();
        f.prepare (mono);
    }
    for (auto& f : loopLpf)
    {
        f.reset();
        f.prepare (mono);
    }
    for (auto& f : dampingA)
    {
        f.reset();
        f.prepare (mono);
    }
    for (auto& f : dampingB)
    {
        f.reset();
        f.prepare (mono);
    }

    for (auto& f : ottLowLp)
    {
        f.reset();
        f.prepare (mono);
    }
    for (auto& f : ottMidHp)
    {
        f.reset();
        f.prepare (mono);
    }
    for (auto& f : ottMidLp)
    {
        f.reset();
        f.prepare (mono);
    }
    for (auto& f : ottHighHp)
    {
        f.reset();
        f.prepare (mono);
    }

    rebuildOttCrossovers();

    delaySamplesSmooth.prepare (sampleRate, 320.f);
    feedbackSmooth.prepare (sampleRate, 85.f);
    relayMixSmooth.prepare (sampleRate, 145.f);
    injectorSmooth.prepare (sampleRate, 110.f);
    ottAmountSmooth.prepare (sampleRate, 240.f);

    lastRt.enabled = false;
    lastRt.feedback = 0.45f;
    lastRt.loopHpfHz = 95.f;
    lastRt.loopLpfHz = 12500.f;
    lastRt.ottAmountPct = 0.f;
    lastRt.ottTimeMs = 135.f;

    updateOttTiming (lastRt.ottTimeMs);

    filterSetupFingerprint = INT32_MIN;
    diffusionMsAppliedToken = std::numeric_limits<float>::infinity();

    rebuildDamping (lastRt.dampingPct, lastRt.loopHpfHz, lastRt.loopLpfHz);
    filterSetupFingerprint = computeFilterFingerprint (lastRt);

    const float diffusePrep = std::floor (lastRt.diffusionMs * 2.f + 0.5f) * 0.5f;
    updateDiffusionFromMs (diffusePrep, 1.f);
    diffusionMsAppliedToken = diffusePrep;

    relayMixSmooth.reset (Utils::DSPUtils::percentageToNormalized (lastRt.mixPercent));
    injectorSmooth.reset (Utils::DSPUtils::dbToGain (lastRt.inputGainDb));
    ottAmountSmooth.reset (juce::jlimit (0.f, 1.f, lastRt.ottAmountPct * 0.01f));

    const float targMsInit = computeTargetDelayMs (lastRt);
    const float targSamps = targMsInit * 0.001f * float (sampleRate);

    delaySamplesSmooth.reset (targSamps);
    feedbackSmooth.reset (juce::jmin (lastRt.feedback * 0.92f, 0.986f));

    cachedRoundTripMs = targMsInit + computeDiffuseExtraMs();

    resetInternalBuffersOnly();

    prepared = true;
}

void RelayDelayCore::reset() noexcept
{
    ++clearGeneration;

    if (! prepared)
        return;

    resetInternalBuffersOnly();
}

void RelayDelayCore::setTargets (const RelayRuntimeParams& p) noexcept
{
    lastRt = p;

    if (! prepared)
        return;

    const int fp = computeFilterFingerprint (p);

    if (fp != filterSetupFingerprint)
    {
        rebuildDamping (p.dampingPct, p.loopHpfHz, p.loopLpfHz);
        filterSetupFingerprint = fp;
    }

    const float diffuseTok = std::floor (p.diffusionMs * 2.f + 0.5f) * 0.5f;

    if ((diffusionMsAppliedToken == std::numeric_limits<float>::infinity())
        || std::abs (diffuseTok - diffusionMsAppliedToken) > 0.45f)
    {
        updateDiffusionFromMs (diffuseTok, 1.f);
        diffusionMsAppliedToken = diffuseTok;
    }

    updateOttTiming (p.ottTimeMs);

    const float targMs = computeTargetDelayMs (p);
    const float userSam = targMs * 0.001f * float (sampleRate);

    const float smearFloor = estimateDiffuseSmearSamples (diffusionScaleTarget) * 1.95f + 12.f;

    delaySamplesSmooth.setTargetValue (juce::jmax (smearFloor, userSam));
    feedbackSmooth.setTargetValue (juce::jmin (p.feedback * 0.925f, 0.984f));

    injectorSmooth.setTargetValue (Utils::DSPUtils::dbToGain (p.inputGainDb));
    relayMixSmooth.setTargetValue (Utils::DSPUtils::percentageToNormalized (p.mixPercent));
    ottAmountSmooth.setTargetValue (juce::jlimit (0.f, 1.f, p.ottAmountPct * 0.01f));

    cachedRoundTripMs = targMs + computeDiffuseExtraMs();
}

void RelayDelayCore::processStereoSample (float inL, float inR, float& outL, float& outR) noexcept
{
    if (! prepared)
    {
        outL = inL;
        outR = inR;
        return;
    }

    if (! lastRt.enabled)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const float fb = feedbackSmooth.getNextValue();
    const float dlySamps = delaySamplesSmooth.getNextValue();
    const float relayMixAbs = relayMixSmooth.getNextValue();
    const float injGain = injectorSmooth.getNextValue();
    const float ottAmtStep = ottAmountSmooth.getNextValue();

    advanceModPhases();

    const float tappedL =
        delay[0].popSample (0, juce::jmax (12.f,
                                           juce::jmin ((float) (maxDelayAllocated - 2),
                                                       dlySamps)),
                            true);

    const float tappedR =
        delay[1].popSample (0, juce::jmax (12.f,
                                           juce::jmin ((float) (maxDelayAllocated - 2),
                                                       dlySamps)),
                            true);

    const float flutterW = Utils::DSPUtils::percentageToNormalized (lastRt.flutterDepthPct);
    const float flOsc = std::sin (flutterPhase) + 0.37f * std::sin (flutterPhase * 0.31f + 1.1f);

    static constexpr uint8_t flStagger[kApfStages] = { 1, 2, 3, 5, 8, 13 };

    auto feedbackPath = [&] (int chIdx, float readTap) noexcept
    {
        float xp = loopHpf[(size_t) chIdx].processSample (readTap);
        xp = loopLpf[(size_t) chIdx].processSample (xp);

        for (int si = 0; si < kApfStages; ++si)
        {
            const float base = diffuser[(size_t) chIdx][(size_t) si].getBaseCoeff();
            const float wig = 1.f + flutterW * 0.028f * flOsc * (float) flStagger[(size_t) si] * 0.09f;
            const float cg = juce::jlimit (0.38f, 0.8f, base * wig);

            xp = diffuser[(size_t) chIdx][(size_t) si].processSample (xp, cg);
        }

        xp = dampingA[(size_t) chIdx].processSample (xp);
        xp = dampingB[(size_t) chIdx].processSample (xp);

        const float lo = ottLowLp[(size_t) chIdx].processSample (xp);
        const float mid = ottMidLp[(size_t) chIdx].processSample (
            ottMidHp[(size_t) chIdx].processSample (xp));
        const float hi = ottHighHp[(size_t) chIdx].processSample (xp);

        return processOtt3 (chIdx, lo, mid, hi, ottAmtStep);
    };

    const float fbL = feedbackPath (0, tappedL);
    const float fbR = feedbackPath (1, tappedR);

    const float sumL = injGain * inL + fbL * fb;
    const float sumR = injGain * inR + fbR * fb;

    delay[0].pushSample (0, feedbackGuard (sumL));
    delay[1].pushSample (0, feedbackGuard (sumR));

    const float chorL = chorusProcessTap (0, tappedL);
    const float chorR = chorusProcessTap (1, tappedR);

    outL = inL * (1.f - relayMixAbs) + chorL * relayMixAbs;
    outR = inR * (1.f - relayMixAbs) + chorR * relayMixAbs;
}

} // namespace Core
} // namespace DSP
