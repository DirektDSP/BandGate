#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>

namespace
{
    int getNumBands (juce::AudioProcessorValueTreeState& apvts)
    {
        const int c = static_cast<int> (*apvts.getRawParameterValue ("NUM_BANDS"));
        return juce::jlimit (2, PluginProcessor::kMaxBands, c + 2);
    }

    void fillBandArrays (juce::AudioProcessorValueTreeState& apvts,
                         int numBands,
                         double sampleRate,
                         std::array<float, PluginProcessor::kMaxBands>& thr,
                         std::array<float, PluginProcessor::kMaxBands>& red,
                         std::array<float, PluginProcessor::kMaxBands>& sm,
                         std::array<float, PluginProcessor::kMaxBands - 1>& xover)
    {
        for (int b = 0; b < PluginProcessor::kMaxBands; ++b)
        {
            const juce::String pfx = "BAND" + juce::String (b) + "_";
            thr[(size_t) b] = apvts.getRawParameterValue (pfx + "THRESHOLD")->load();
            red[(size_t) b] = apvts.getRawParameterValue (pfx + "REDUCTION")->load();
            sm[(size_t) b] = apvts.getRawParameterValue (pfx + "SMOOTHING")->load();
        }

        const float maxHz = juce::jmax (50.f, (float) (sampleRate * 0.48));
        const int nx = numBands - 1;
        for (int i = 0; i < nx; ++i)
            xover[(size_t) i] = juce::jlimit (40.f, maxHz,
                                             apvts.getRawParameterValue ("CROSSOVER_" + juce::String (i))->load());
    }
} // namespace

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    apvts.state.setProperty(Service::PresetManager::presetNameProperty, "", nullptr);
    presetManager = std::make_unique<Service::PresetManager>(apvts);
}

PluginProcessor::~PluginProcessor() noexcept
{
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(samplesPerBlock);
    spec.numChannels = static_cast<uint32>(getTotalNumOutputChannels());

    float inputGain = *apvts.getRawParameterValue("INPUT_GAIN");
    float outputGain = *apvts.getRawParameterValue("OUTPUT_GAIN");
    float mix = *apvts.getRawParameterValue("MIX");
    int fftChoice = static_cast<int>(*apvts.getRawParameterValue("FFT_SIZE"));
    int fftOrder = fftChoiceToOrder(fftChoice);
    const int numBands = getNumBands (apvts);
    std::array<float, kMaxBands> thr {}, red {}, sm {};
    std::array<float, kMaxBands - 1> xover {};
    fillBandArrays (apvts, numBands, sampleRate, thr, red, sm, xover);

    dspProcessor.prepare (spec, inputGain, outputGain, mix, fftOrder, numBands,
                            thr.data(), red.data(), sm.data(), xover.data());

    setLatencySamples(dspProcessor.getLatencySamples());

    MOONBASE_PREPARE_TO_PLAY (sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    float inputGain = *apvts.getRawParameterValue("INPUT_GAIN");
    float outputGain = *apvts.getRawParameterValue("OUTPUT_GAIN");
    float mix = *apvts.getRawParameterValue("MIX");

    dspProcessor.reset(inputGain, outputGain, mix);
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
   #endif
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    float inputGain = *apvts.getRawParameterValue("INPUT_GAIN");
    float outputGain = *apvts.getRawParameterValue("OUTPUT_GAIN");
    float mix = *apvts.getRawParameterValue("MIX");
    int fftChoice = static_cast<int>(*apvts.getRawParameterValue("FFT_SIZE"));
    int fftOrder = fftChoiceToOrder(fftChoice);
    const int numBands = getNumBands (apvts);
    std::array<float, kMaxBands> thr {}, red {}, sm {};
    std::array<float, kMaxBands - 1> xover {};
    fillBandArrays (apvts, numBands, getSampleRate(), thr, red, sm, xover);

    dspProcessor.updateParameters (inputGain, outputGain, mix, fftOrder, numBands,
                                    thr.data(), red.data(), sm.data(), xover.data());

    // Update latency if FFT size changed
    setLatencySamples(dspProcessor.getLatencySamples());

    dspProcessor.processBlock(buffer);

    MOONBASE_PROCESS (buffer);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PluginProcessor::fetchSpectralVisualData (std::vector<float>& magDbOut,
                                                std::vector<float>& gainOut,
                                                int& fftSizeOut,
                                                double& sampleRateOut) const
{
    dspProcessor.fetchSpectralVisualData (magDbOut, gainOut, fftSizeOut, sampleRateOut);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
