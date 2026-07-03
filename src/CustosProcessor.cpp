#include "CustosProcessor.h"
#include "SynthLoader.h"
#include "HostTrace.h"

namespace custos
{
CustosProcessor::CustosProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    trace ("ctor: begin");
    facade.reserve (kFacadeParamCount);
    for (int i = 0; i < kFacadeParamCount; ++i)
    {
        auto* p = new FacadeParameter (i);
        addParameter (p);
        facade.push_back (p);
    }

    // M1: load a single hard-coded synth at boot. Empty path => silent passthrough.
    const auto path = hardcodedSynthPath();
    if (path.isNotEmpty())
    {
        juce::String error;
        // Placeholder rate/block; prepareToPlay re-prepares with the host's real values.
        if (auto instance = SynthLoader::loadVST3 (path, 44100.0, 512, error))
            attachInner (std::move (instance));
        else
            juce::Logger::writeToLog ("Custos: inner synth load failed: " + error);
    }

    trace ("ctor: end, boundCount=" + juce::String (boundCount));
}

CustosProcessor::~CustosProcessor()
{
    InnerBinding::unbindAll (facade);   // drop dangling pointers before inner is destroyed
}

void CustosProcessor::attachInner (std::unique_ptr<juce::AudioProcessor> newInner)
{
    InnerBinding::unbindAll (facade);
    inner = std::move (newInner);
    boundCount = 0;

    if (inner != nullptr)
    {
        boundCount = InnerBinding::bind (*inner, facade);
        if (isPrepared)
        {
            inner->setPlayConfigDetails (0, getTotalNumOutputChannels(),
                                         preparedSampleRate, preparedBlockSize);
            inner->prepareToPlay (preparedSampleRate, preparedBlockSize);
        }
    }
}

void CustosProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    trace ("prepareToPlay sr=" + juce::String (sampleRate));
    preparedSampleRate = sampleRate;
    preparedBlockSize  = samplesPerBlock;
    isPrepared = true;

    if (inner != nullptr)
    {
        inner->setPlayConfigDetails (0, getTotalNumOutputChannels(), sampleRate, samplesPerBlock);
        inner->prepareToPlay (sampleRate, samplesPerBlock);
    }
}

void CustosProcessor::releaseResources()
{
    isPrepared = false;
    if (inner != nullptr) inner->releaseResources();
}

void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (inner != nullptr)
        inner->processBlock (buffer, midi);   // MIDI in -> stereo out, straight through
    else
        buffer.clear();
}

bool CustosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CustosProcessor::getStateInformation (juce::MemoryBlock&)
{
    trace ("getStateInformation");   // M3 will persist real state here
}

void CustosProcessor::setStateInformation (const void*, int)
{
    trace ("setStateInformation");   // M3 will restore real state here
}
}
