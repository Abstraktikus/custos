#include "CustosProcessor.h"
#include "SynthLoader.h"
#include "HostTrace.h"
#include "SynthWindow.h"
#include "TitledSynthWindow.h"
#include "CustosEditor.h"
#include "StateCodec.h"
#include "CustosOscServer.h"
#include "OscContract.h"
#include "MidiRoute.h"
#include "AudioBusMapper.h"
#include "InstrumentBrowser.h"
#include <algorithm>

namespace custos
{
CustosProcessor::CustosProcessor (bool enableOsc)
    : juce::AudioProcessor (BusesProperties()
        .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 1", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 2", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 3", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 4", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 5", juce::AudioChannelSet::stereo(), true))
{
    trace ("ctor: begin");
    for (int i = 0; i < 16; ++i) midiRoute[(size_t) i].store ((std::uint8_t) (i + 1), std::memory_order_relaxed);
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

    browseDebounce.cb = [this] { commitBrowseLoad(); };   // fires 400 ms after flipping stops

    if (enableOsc)
        oscServer = std::make_unique<CustosOscServer> (*this);

    trace ("ctor: end, boundCount=" + juce::String (boundCount));
}

CustosProcessor::~CustosProcessor()
{
    oscServer.reset();                  // stop OSC callbacks before tearing down the rest
    synthWindow.reset();                // destroy the hosted view before the inner synth (its owner)
    titledWindow.reset();               // same for the titled window
    InnerBinding::unbindAll (facade);   // drop dangling pointers before inner is destroyed
}

juce::AudioProcessorEditor* CustosProcessor::createEditor()
{
    return new CustosEditor (*this);
}

bool CustosProcessor::loadInner (std::unique_ptr<juce::AudioProcessor> newInner, const juce::String& path)
{
    // The M2 synth window hosts the OUTGOING inner's editor — destroy it before the old inner.
    hideSynthWindow();

    // Slow work OUTSIDE the lock: prepare the incoming inner in ITS OWN bus layout (multi-out safe).
    if (newInner != nullptr && isPrepared)
    {
        newInner->setRateAndBufferSizeDetails (preparedSampleRate, preparedBlockSize);
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

    resizeInnerScratch();   // match the new inner's channel count (multi-out safe)
    browseIndex = -1;       // any load re-syncs the browse cursor to the loaded synth
    currentSynthPath = (inner != nullptr) ? path : juce::String();
    refreshEditor();
    emitLoaded();
    return inner != nullptr;
}

CommandResult CustosProcessor::load (const juce::String& path)
{
    const double sr    = preparedSampleRate > 0.0 ? preparedSampleRate : 44100.0;
    const int    block = preparedBlockSize  > 0   ? preparedBlockSize  : 512;

    juce::String err;
    if (auto instance = SynthLoader::loadVST3 (path, sr, block, err))
    {
        loadInner (std::move (instance), path);
        applyVolumeDefault (path);
        return { true, boundCount, "loaded " + path };
    }
    // Load failed: keep whatever is currently loaded.
    return { false, boundCount, "error " + err };
}

void CustosProcessor::clear()
{
    loadInner (nullptr, {});
}

void CustosProcessor::emitLoaded()
{
    if (outboundSink)
        outboundSink (buildLoaded (identityN, currentSynthPath, boundCount, innerParamTotal()));
}

void CustosProcessor::dumpParams (int start, int count)
{
    if (! outboundSink) return;

    const int from = juce::jmax (0, start);
    const int to   = juce::jmin (boundCount, from + juce::jmax (0, count));
    int sent = 0;
    for (int i = from; i < to; ++i)
    {
        auto* fp = facade[(size_t) i];
        outboundSink (buildParam (identityN, i, fp->getValue(), fp->getName (128),
                                  fp->getDefaultValue(), fp->getNumSteps(), fp->getLabel()));
        ++sent;
    }
    outboundSink (buildParamsDone (identityN, start, sent));
}

void CustosProcessor::setVolumeDb (float db)
{
    masterGain.store (juce::Decibels::decibelsToGain (db));
}

static void sortByFavOrder (std::vector<Favorite>& v)
{
    std::sort (v.begin(), v.end(), [] (const Favorite& a, const Favorite& b) { return a.favOrder < b.favOrder; });
}

void CustosProcessor::favoritesBegin() { favAccumulator.clear(); }
void CustosProcessor::favoritesAdd (const Favorite& f) { favAccumulator.push_back (f); }

void CustosProcessor::favoritesEnd()
{
    favorites = std::move (favAccumulator);
    favAccumulator.clear();
    sortByFavOrder (favorites);
    refreshEditor();
}

void CustosProcessor::setFavorites (std::vector<Favorite> favs)
{
    favorites = std::move (favs);
    sortByFavOrder (favorites);
    refreshEditor();
}

void CustosProcessor::loadFavorite (int index)
{
    if (index >= 0 && index < (int) favorites.size())
        load (favorites[(size_t) index].path);
}

int CustosProcessor::indexOfPath (const juce::String& path) const
{
    if (path.isEmpty()) return -1;
    for (int i = 0; i < (int) favorites.size(); ++i)
        if (favorites[(size_t) i].path == path) return i;
    return -1;
}

void CustosProcessor::browseInstrument (int delta)
{
    const int cnt = (int) favorites.size();
    if (cnt == 0) return;
    if (browseIndex < 0) browseIndex = indexOfPath (currentSynthPath);   // seed from the loaded synth
    const auto step = browseStep (browseIndex, delta, cnt);
    browseIndex = step.index;
    emitBrowsing (browseIndex, favorites[(size_t) browseIndex].name, step.wrapped);
    browseDebounce.startTimer (400);   // (re)arm; the synth loads only when flipping stops
}

void CustosProcessor::setBrowseIndex (int i)
{
    const int cnt = (int) favorites.size();
    if (cnt == 0) return;
    browseIndex = juce::jlimit (0, cnt - 1, i);
    emitBrowsing (browseIndex, favorites[(size_t) browseIndex].name, false);
    browseDebounce.startTimer (400);
}

void CustosProcessor::commitBrowseLoad()
{
    if (browseIndex < 0 || browseIndex >= (int) favorites.size()) return;
    const juce::String path = favorites[(size_t) browseIndex].path;
    if (path == currentSynthPath) return;   // de-dup: cursor already on the loaded synth
    load (path);                            // synchronous; emits /custos/loaded (= ready/playable)
}

void CustosProcessor::emitBrowsing (int index, const juce::String& name, bool wrapped)
{
    if (outboundSink) outboundSink (buildBrowsing (identityN, index, name, wrapped));
}

void CustosProcessor::applyVolumeDefault (const juce::String& path)
{
    for (const auto& f : favorites)
        if (f.path == path) { setVolumeDb (f.gainDb); return; }
    setVolumeDb (0.0f);   // not a favourite -> unity
}

void CustosProcessor::bindOsc()
{
    if (oscServer != nullptr)
        lastBindOk = oscServer->bindToIdentity (identityN);
    else
        lastBindOk = false;
}

void CustosProcessor::setIdentity (int n)
{
    identityN = n;
    bindOsc();          // no-op inner details when oscServer is null (unit tests)
    refreshEditor();
}

void CustosProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    trace ("prepareToPlay sr=" + juce::String (sampleRate));
    preparedSampleRate = sampleRate;
    preparedBlockSize  = samplesPerBlock;
    isPrepared = true;

    if (inner != nullptr)
    {
        inner->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);   // keep the inner's OWN bus layout
        inner->prepareToPlay (sampleRate, samplesPerBlock);
    }
    resizeInnerScratch();
}

void CustosProcessor::resizeInnerScratch()
{
    const int ch = (inner != nullptr)
        ? juce::jmax (inner->getTotalNumInputChannels(), inner->getTotalNumOutputChannels(), 2)
        : 2;
    innerScratch.setSize (ch, preparedBlockSize > 0 ? preparedBlockSize : 512, false, false, true);
}

void CustosProcessor::releaseResources()
{
    isPrepared = false;
    if (inner != nullptr) inner->releaseResources();
}

void CustosProcessor::setMidiRoute (const std::array<int, 16>& route)
{
    for (int i = 0; i < 16; ++i)
        midiRoute[(size_t) i].store ((std::uint8_t) juce::jlimit (0, 16, route[(size_t) i]),
                                     std::memory_order_relaxed);
}

std::array<int, 16> CustosProcessor::getMidiRoute() const
{
    std::array<int, 16> out {};
    for (int i = 0; i < 16; ++i) out[(size_t) i] = midiRoute[(size_t) i].load (std::memory_order_relaxed);
    return out;
}

void CustosProcessor::emitMidiRoute()
{
    if (outboundSink) outboundSink (buildMidiRoute (identityN, getMidiRoute()));
}

void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    std::array<std::uint8_t, 16> snap {};
    for (int i = 0; i < 16; ++i) snap[(size_t) i] = midiRoute[(size_t) i].load (std::memory_order_relaxed);
    applyMidiRoute (midi, snap, routeScratch);

    const int n = buffer.getNumSamples();
    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && inner != nullptr)
        {
            if (innerScratch.getNumSamples() < n)
                innerScratch.setSize (innerScratch.getNumChannels(), n, false, false, true);
            innerScratch.clear();
            // Feed Custos's input bus (ch 0/1) into the inner's first stereo pair.
            const int fed = juce::jmin (2, innerScratch.getNumChannels(), buffer.getNumChannels());
            for (int c = 0; c < fed; ++c) innerScratch.copyFrom (c, 0, buffer, c, 0, n);

            juce::AudioBuffer<float> innerView (innerScratch.getArrayOfWritePointers(),
                                                innerScratch.getNumChannels(), n);
            inner->processBlock (innerView, midi);                    // inner writes into ITS full channel set
            mapInnerToOutputs (innerView, buffer, mainLROnlyFlag.load());   // fold to Custos's 5 stereo buses
        }
        else
        {
            buffer.clear();                        // no synth, or a swap is in progress -> silence
        }
    }
    buffer.applyGain (masterGain.load());          // F5 uniform trim (1.0 = unity)
}

bool CustosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    if (! (in.isDisabled() || in == juce::AudioChannelSet::stereo())) return false;
    if (layouts.outputBuses.size() != 5) return false;
    for (int b = 0; b < layouts.outputBuses.size(); ++b)
        if (layouts.outputBuses.getReference (b) != juce::AudioChannelSet::stereo()) return false;
    return true;
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
        synthWindow = std::make_unique<SynthWindow> (ed);    // borderless; closed via hideSynthWindow only
        synthWindow->setAlwaysOnTop (onTopMode == OnTopInstrument);
        synthWindow->onReadout = [this] { updateEditorRectReadout(); };   // live x/y/w/h (drag + inner zoom)
        synthWindow->onCommit  = [this] { emitWindowRect(); };            // drag-end + content-driven resize
    }
    refreshEditor();
}

void CustosProcessor::showSynthWindowTitled()
{
    if (inner == nullptr) return;                          // nothing to show
    if (titledWindow != nullptr) { titledWindow->toFront (true); return; }
    if (auto* ed = inner->createEditorAndMakeActive())     // null if the synth has no editor
    {
        auto w = std::make_unique<TitledSynthWindow> (inner->getName(), ed);
        w->setAlwaysOnTop (onTopMode == OnTopInstrument);
        w->onCloseButton = [this] { titledWindow.reset(); refreshEditor(); };   // native close tears it down
        titledWindow = std::move (w);
    }
    refreshEditor();
}

void CustosProcessor::setOnTopMode (OnTopMode mode)
{
    onTopMode = mode;
    if (synthWindow != nullptr)
        synthWindow->setAlwaysOnTop (mode == OnTopInstrument);
    if (titledWindow != nullptr)
        titledWindow->setAlwaysOnTop (mode == OnTopInstrument);
    if (auto* e = getActiveEditor())
        if (auto* top = e->getTopLevelComponent())
            top->setAlwaysOnTop (mode == OnTopCustos);
    refreshEditor();
}

void CustosProcessor::setSynthWindowRect (int x, int y, int w, int h, bool movable, bool clamp)
{
    if (synthWindow == nullptr) showSynthWindow();   // ensure it exists
    if (synthWindow == nullptr) return;              // no inner / editor-less synth

    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto logical = displays.physicalToLogical (juce::Rectangle<int> (x, y, w, h));

    if (clamp)   // config phase: keep the window inside the target display's work area (borders stay reachable)
    {
        const auto* disp = displays.getDisplayForRect (logical);
        if (disp == nullptr) disp = displays.getPrimaryDisplay();
        if (disp != nullptr) logical = logical.constrainedWithin (disp->userArea);
    }

    synthWindowMovable = movable;
    synthWindow->applyRect (logical, movable);
    updateEditorRectReadout();   // reflect the applied position in the editor fields
    emitWindowRect();            // echo the applied position to KM
}

juce::Rectangle<int> CustosProcessor::currentSynthWindowPhysical() const
{
    if (synthWindow == nullptr) return {};
    return juce::Desktop::getInstance().getDisplays().logicalToPhysical (synthWindow->getBounds());
}

void CustosProcessor::updateEditorRectReadout()
{
    if (auto* e = dynamic_cast<CustosEditor*> (getActiveEditor()))
        e->updateRectReadout();
}

void CustosProcessor::emitWindowRect()
{
    if (! outboundSink || synthWindow == nullptr) return;
    const auto r = currentSynthWindowPhysical();
    outboundSink (buildWindowRect (identityN, r.getX(), r.getY(), r.getWidth(), r.getHeight(), synthWindowMovable));
}

void CustosProcessor::hideSynthWindow()
{
    synthWindow.reset();
    titledWindow.reset();
    refreshEditor();   // keep the editor's button label in sync (also on external title-bar close)
}

void CustosProcessor::refreshEditor()
{
    if (auto* e = dynamic_cast<CustosEditor*> (getActiveEditor()))
        e->refresh();
}

void CustosProcessor::toggleSynthWindow()   // Instrument-label double-click: one window at a time
{
    if (isSynthWindowVisible()) hideSynthWindow();   // close whichever (titled or borderless) is open
    else                        showSynthWindowTitled();
}

void CustosProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    trace ("getStateInformation");
    juce::MemoryBlock innerChunk;
    if (inner != nullptr) inner->getStateInformation (innerChunk);
    std::array<std::uint8_t, 16> route {};
    for (int i = 0; i < 16; ++i) route[(size_t) i] = (std::uint8_t) getMidiRoute()[(size_t) i];
    dest = serializeState (currentSynthPath, innerChunk, identityN, route, mainLROnlyFlag.load());
}

void CustosProcessor::setStateInformation (const void* data, int size)
{
    trace ("setStateInformation");
    PersistedState ps;
    if (! parseState (data, size, ps)) return;   // unknown/legacy blob -> ignore, don't crash

    identityN = ps.identityN;                    // tag emissions correctly before any load/clear
    mainLROnlyFlag.store (ps.mainLROnly);        // v4 audio-fold mode
    { std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = ps.route[(size_t) i]; setMidiRoute (r); }

    if (ps.path.isEmpty())
    {
        clear();
    }
    else
    {
        const auto r = load (ps.path);           // safe swap + currentSynthPath + /custos/loaded
        if (r.ok && inner != nullptr && ps.innerState.getSize() > 0)
            inner->setStateInformation (ps.innerState.getData(), (int) ps.innerState.getSize());
    }

    bindOsc();                                   // bind BASE+N + announce /custos/here (reflects current inner)
    refreshEditor();
}
}
