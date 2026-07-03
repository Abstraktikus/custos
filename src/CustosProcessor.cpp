#include "CustosProcessor.h"
#include "SynthLoader.h"
#include "HostTrace.h"
#include "SynthWindow.h"

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
    synthWindow.reset();                // destroy the hosted view before the inner synth (its owner)
    InnerBinding::unbindAll (facade);   // drop dangling pointers before inner is destroyed
}

void CustosProcessor::attachInner (std::unique_ptr<juce::AudioProcessor> newInner)
{
    // M1 contract: called once, from the constructor, on the message thread, before the host
    // starts processing. TODO(M4 glitch-free swap): a runtime swap must (a) bypass the audio
    // thread around the exchange and (b) call releaseResources() on the OUTGOING inner before
    // it is destroyed. processBlock reads `inner` (and getTailLengthSeconds too) without
    // synchronization, so an unguarded second call here is a use-after-free on the audio thread.
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

juce::String CustosProcessor::innerSynthName() const
{
    return inner != nullptr ? inner->getName() : juce::String ("(none)");
}

void CustosProcessor::showSynthWindow()
{
    if (inner == nullptr) return;                              // nothing to show
    if (synthWindow != nullptr) { synthWindow->toFront (true); return; }
    if (auto* ed = inner->createEditorAndMakeActive())        // null if the synth has no editor (JUCE 8 API)
        synthWindow = std::make_unique<SynthWindow> (
            kProduct + juce::String (" - ") + inner->getName(),
            ed,
            [this] { hideSynthWindow(); });
}

void CustosProcessor::hideSynthWindow()
{
    synthWindow.reset();
}

void CustosProcessor::toggleSynthWindow()
{
    if (isSynthWindowVisible()) hideSynthWindow();
    else                        showSynthWindow();
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
