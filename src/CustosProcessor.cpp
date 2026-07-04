#include "CustosProcessor.h"
#include "SynthLoader.h"
#include "HostTrace.h"
#include "SynthWindow.h"
#include "CustosEditor.h"
#include "StateCodec.h"
#include "CustosOscServer.h"

namespace custos
{
CustosProcessor::CustosProcessor (bool enableOsc)
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

    // Dev/headless default: load a hard-coded synth at boot if one is configured. In production the
    // synth arrives via setStateInformation (gig) or an OSC /custos/load — both call load() too.
    const auto path = hardcodedSynthPath();
    if (path.isNotEmpty())
    {
        const auto r = load (path);
        if (! r.ok) juce::Logger::writeToLog ("Custos: inner synth load failed: " + r.message);
    }

    if (enableOsc)
        oscServer = std::make_unique<CustosOscServer> (*this);

    trace ("ctor: end, boundCount=" + juce::String (boundCount));
}

CustosProcessor::~CustosProcessor()
{
    oscServer.reset();                  // stop OSC callbacks before tearing down the rest
    synthWindow.reset();                // destroy the hosted view before the inner synth (its owner)
    InnerBinding::unbindAll (facade);   // drop dangling pointers before inner is destroyed
}

juce::AudioProcessorEditor* CustosProcessor::createEditor()
{
    return new CustosEditor (*this);
}

bool CustosProcessor::loadInner (std::unique_ptr<juce::AudioProcessor> newInner)
{
    // The M2 synth window hosts the OUTGOING inner's editor — destroy it before the old inner.
    hideSynthWindow();

    // Slow work OUTSIDE the lock: prepare the incoming inner to the current play config.
    if (newInner != nullptr && isPrepared)
    {
        newInner->setPlayConfigDetails (0, getTotalNumOutputChannels(), preparedSampleRate, preparedBlockSize);
        newInner->prepareToPlay (preparedSampleRate, preparedBlockSize);
    }

    // Fast pointer/bind swap INSIDE the lock — the audio thread's try-lock misses at most one block.
    std::unique_ptr<juce::AudioProcessor> oldInner;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        InnerBinding::unbindAll (facade);
        oldInner = std::move (inner);
        inner = std::move (newInner);
        boundCount = (inner != nullptr) ? InnerBinding::bind (*inner, facade) : 0;
    }

    // Slow teardown OUTSIDE the lock.
    if (oldInner != nullptr) { oldInner->releaseResources(); oldInner.reset(); }

    refreshEditor();
    return inner != nullptr;
}

CommandResult CustosProcessor::load (const juce::String& path)
{
    const double sr    = preparedSampleRate > 0.0 ? preparedSampleRate : 44100.0;
    const int    block = preparedBlockSize  > 0   ? preparedBlockSize  : 512;

    juce::String err;
    if (auto instance = SynthLoader::loadVST3 (path, sr, block, err))
    {
        loadInner (std::move (instance));
        currentSynthPath = path;
        return { true, boundCount, "loaded " + path };
    }
    // Load failed: keep whatever is currently loaded.
    return { false, boundCount, "error " + err };
}

void CustosProcessor::clear()
{
    currentSynthPath = {};
    loadInner (nullptr);
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
    const juce::SpinLock::ScopedTryLockType tl (swapLock);
    if (tl.isLocked() && inner != nullptr)
        inner->processBlock (buffer, midi);   // MIDI in -> stereo out, straight through
    else
        buffer.clear();                        // no synth, or a swap is in progress -> silence
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
    {
        // The window's close button defers this callback via the message queue; it can outlive
        // both the window and this processor. Guard with a weak token so a late dispatch after
        // ~CustosProcessor is a safe no-op (not a use-after-free).
        std::weak_ptr<bool> weak = aliveToken;
        synthWindow = std::make_unique<SynthWindow> (
            kProduct + juce::String (" - ") + inner->getName(),
            ed,
            [this, weak] { if (! weak.expired()) hideSynthWindow(); });
    }
    refreshEditor();
}

void CustosProcessor::hideSynthWindow()
{
    synthWindow.reset();
    refreshEditor();   // keep the editor's button label in sync (also on external title-bar close)
}

void CustosProcessor::refreshEditor()
{
    if (auto* e = dynamic_cast<CustosEditor*> (getActiveEditor()))
        e->refresh();
}

void CustosProcessor::toggleSynthWindow()
{
    if (isSynthWindowVisible()) hideSynthWindow();
    else                        showSynthWindow();
}

void CustosProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    trace ("getStateInformation");
    juce::MemoryBlock innerChunk;
    if (inner != nullptr) inner->getStateInformation (innerChunk);
    dest = serializeState (currentSynthPath, innerChunk, 0);
}

void CustosProcessor::setStateInformation (const void* data, int size)
{
    trace ("setStateInformation");
    PersistedState ps;
    if (! parseState (data, size, ps)) return;   // unknown/legacy blob -> ignore, don't crash

    if (ps.path.isEmpty())
    {
        clear();
        return;
    }

    const auto r = load (ps.path);                // safe swap + currentSynthPath
    if (r.ok && inner != nullptr && ps.innerState.getSize() > 0)
        inner->setStateInformation (ps.innerState.getData(), (int) ps.innerState.getSize());
}
}
