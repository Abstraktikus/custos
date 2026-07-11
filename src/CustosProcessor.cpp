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
#include "PresetStore.h"
#include <algorithm>
#include <cmath>

namespace custos
{
CustosProcessor::CustosProcessor (bool enableOsc, juce::File presetRootConfig)
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

    presetRootCfg = presetRootConfig.getFullPathName().isNotEmpty() ? presetRootConfig
                                                                     : presetRootConfigFile();
    presetRootPath = readPresetRoot (presetRootCfg);

    browseDebounce.cb = [this] { commitBrowseLoad(); };   // fires 400 ms after flipping stops

    if (enableOsc)
        oscServer = std::make_unique<CustosOscServer> (*this);

    trace ("ctor: end, boundCount=" + juce::String (boundCount));
}

CustosProcessor::~CustosProcessor()
{
    if (inner != nullptr) inner->removeListener (this);   // defensive: don't rely on member-destruction order
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
    browseDebounce.stopTimer();   // this load (browse commit / direct load / test attach) clears in-flight
    releasePendingInject();       // release any pending PARAM inject on the OLD inner before it's swapped out

    // The M2 synth window hosts the OUTGOING inner's editor — destroy it before the old inner.
    const WindowMode reopenAs      = windowMode;               // keep the window showing across a swap (browse UI)
    const bool       reopenMovable = windowBorderlessMovable;
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

    // Wire the SINGLE AudioProcessorListener to the NEW inner (message-thread; outside the RT swapLock).
    // One registration serves BOTH: re-bind on late param populate (#11, audioProcessorChanged — the new
    // inner may still report 0 params here for a cold Roland) and Learn per-value capture (#12,
    // audioProcessorParameterChanged). The OLD inner is unregistered at teardown below (single remove).
    if (inner != nullptr) inner->addListener (this);

    // Slow teardown OUTSIDE the lock.
    if (oldInner != nullptr) { oldInner->removeListener (this); oldInner->releaseResources(); oldInner.reset(); }

    resizeInnerScratch();   // match the new inner's channel count (multi-out safe)
    browseIndex = -1;       // any load re-syncs the browse cursor to the loaded synth
    presetCursor = -1;      // ...and the preset recall cursor re-seeds to the new synth
    currentSynthPath = (inner != nullptr) ? path : juce::String();

    if (inner != nullptr)   // re-show the window with the NEW synth (so browsing displays each instrument)
    {
        if      (reopenAs == WinTitled)     showSynthWindowTitled();
        else if (reopenAs == WinBorderless) showSynthWindowBorderless (reopenMovable);
    }
    refreshEditor();
    emitLoaded();
    drainPendingRecall();            // spec §5.1: apply a recall buffered while this load was in-flight
    return inner != nullptr;
}

CommandResult CustosProcessor::load (const juce::String& path)
{
    // Idempotent load: if this exact synth is already loaded, DON'T re-instantiate it. GP restores
    // the persisted inner on gig-open and GP-Script re-sends the song's synths, so a reload request
    // for the already-loaded synth is the common case — re-instantiating a heavy synth (transient
    // 2nd instance during the swap) is wasteful and can crash. Just re-ack as loaded.
    if (inner != nullptr && path.isNotEmpty() && path == currentSynthPath)
    {
        rebindInner();   // safety net: recover a bind frozen at 0 if the inner populated params since (no notify)
        emitLoaded();
        return { true, boundCount, "already loaded " + path };
    }

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

bool CustosProcessor::rebindInner()
{
    int newCount = 0;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);   // guard the facade rewrite vs the audio thread
        if (inner == nullptr) return false;
        newCount = InnerBinding::bind (*inner, facade);
    }
    if (newCount == boundCount) return false;
    boundCount = newCount;
    return true;
}

void CustosProcessor::audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails& details)
{
    // Roland ZenCore & co. populate their VST3 params only after their engine initialises (later than
    // the initial bind), then raise restartComponent(kParamTitlesChanged). Re-bind so the frozen-at-0
    // facade recovers, and re-announce the new count to KM/GP. Per-value automation is ignored.
    if (details.parameterInfoChanged && rebindInner())
        emitLoaded();
}

void CustosProcessor::emitLoaded()
{
    traceN ("loaded \"" + currentSynthPath + "\" count=" + juce::String (boundCount) + " total=" + juce::String (innerParamTotal()));
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

bool CustosProcessor::loadByName (const juce::String& name)
{
    for (const auto& f : favorites)
        if (f.name == name) { load (f.path); return true; }
    return false;
}

int CustosProcessor::indexOfPath (const juce::String& path) const
{
    if (path.isEmpty()) return -1;
    for (int i = 0; i < (int) favorites.size(); ++i)
        if (favorites[(size_t) i].path == path) return i;
    return -1;
}

void CustosProcessor::traceN (const juce::String& msg) const
{
    trace ("N" + juce::String (identityN) + "  " + msg);
}

void CustosProcessor::browseInstrument (int delta, int scope)
{
    const int cnt = (int) favorites.size();
    if (cnt == 0) return;
    if (browseIndex < 0) browseIndex = indexOfPath (currentSynthPath);   // seed from the loaded synth
    const int cap = facadeSize();
    int idx = browseIndex;
    bool wrapped = false;
    for (int tries = 0; tries < cnt; ++tries)   // skip synths that don't fit this facade (e.g. 4000-param in Custos 1000)
    {
        const auto step = browseStep (idx, delta, cnt);
        idx = step.index;
        wrapped = wrapped || step.wrapped;
        if (favouriteFits (favorites[(size_t) idx].slots, cap)
            && favouriteInScope (favorites[(size_t) idx].favOrder, scope)) break;
    }
    browseIndex = idx;
    const auto& f = favorites[(size_t) browseIndex];
    emitBrowsing (browseIndex, f.name, wrapped);
    traceN ("browse cursor=" + juce::String (browseIndex) + " name=\"" + f.name + "\" wrapped=" + juce::String (wrapped ? 1 : 0));
    browseDebounce.startTimer (400);   // (re)arm; the synth loads only when flipping stops
}

void CustosProcessor::setBrowseIndex (int i)
{
    const int cnt = (int) favorites.size();
    if (cnt == 0) return;
    browseIndex = juce::jlimit (0, cnt - 1, i);
    const auto& f = favorites[(size_t) browseIndex];
    emitBrowsing (browseIndex, f.name, false);
    traceN ("browse set cursor=" + juce::String (browseIndex) + " name=\"" + f.name + "\"");
    browseDebounce.startTimer (400);
}

void CustosProcessor::commitBrowseLoad()
{
    if (browseIndex < 0 || browseIndex >= (int) favorites.size()) return;
    const auto& f = favorites[(size_t) browseIndex];
    if (! favouriteFits (f.slots, facadeSize())) return;   // never load an oversized synth
    if (f.path == currentSynthPath) return;                // de-dup: cursor already on the loaded synth
    traceN ("browse-load \"" + f.path + "\"");
    load (f.path);                                         // synchronous; emits /custos/loaded (= ready/playable)
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

void CustosProcessor::emitMainLR()
{
    if (outboundSink) outboundSink (buildMainLR (identityN, mainLROnly()));
}

void CustosProcessor::startLearn()
{
    learnLastEmitted.clear();
    learnFifo.reset();
    learnActive.store (true);
    if (outboundSink) outboundSink (buildLearnStarted (identityN));

    learnDrainTimer.cb = [this] { drainLearn(); };
    learnDrainTimer.startTimer (kLearnDrainMs);

    learnSafetyTimer.cb = [this] { stopLearn ("timeout"); };
    learnSafetyTimer.startTimer (kLearnSafetyMs);   // one-shot (DebounceTimer stops itself)
}

void CustosProcessor::stopLearn (const juce::String& reason)
{
    if (! learnActive.exchange (false)) return;   // already closed -> no duplicate stopped
    learnDrainTimer.stopTimer();
    learnSafetyTimer.stopTimer();
    drainLearn();                                 // flush anything still queued
    if (outboundSink) outboundSink (buildLearnStopped (identityN, reason));
}

void CustosProcessor::learnRecord (int innerIdx, float value) noexcept
{
    if (! learnActive.load (std::memory_order_relaxed)) return;   // gate: only while a window is open

    int start1, size1, start2, size2;
    learnFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        learnIdxRing[(size_t) start1] = innerIdx;
        learnValRing[(size_t) start1] = value;
        learnFifo.finishedWrite (1);
    }
    // FIFO full -> drop (coalescing tolerates loss; a human sweep won't overflow between 25 ms drains)
}

void CustosProcessor::drainLearn()
{
    // Pull everything queued, coalescing to the latest value per index (FIFO order = chronological).
    std::unordered_map<int, float> latest;
    int start1, size1, start2, size2;
    const int ready = learnFifo.getNumReady();
    learnFifo.prepareToRead (ready, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) latest[learnIdxRing[(size_t) (start1 + i)]] = learnValRing[(size_t) (start1 + i)];
    for (int i = 0; i < size2; ++i) latest[learnIdxRing[(size_t) (start2 + i)]] = learnValRing[(size_t) (start2 + i)];
    learnFifo.finishedRead (size1 + size2);

    if (! outboundSink) return;

    for (const auto& kv : latest)
    {
        const int   idx = kv.first;
        const float val = kv.second;
        if (idx < 0 || idx >= boundCount) continue;    // unbound facade tail -> not bindable, ignore

        const auto prev = learnLastEmitted.find (idx);
        if (prev != learnLastEmitted.end() && std::abs (val - prev->second) < kLearnDeadband)
            continue;                                  // sub-deadband since last emit -> drop dither

        learnLastEmitted[idx] = val;
        outboundSink (buildLearnMoved (identityN, idx, val, facade[(size_t) idx]->getName (128)));
    }
}

void CustosProcessor::audioProcessorParameterChanged (juce::AudioProcessor*, int index, float newValue)
{
    // Learn captures only MESSAGE-THREAD parameter moves — the operator turning a knob in the synth's
    // editor. Audio-thread-originated changes (host automation / internal LFO modulation during
    // processBlock) are intentionally ignored: the design treats internal modulation as noise, and this
    // keeps the single-producer/single-consumer learnFifo touched from one thread only (the message
    // thread, same as drainLearn). learnRecord still self-gates on learnActive.
    if (juce::MessageManager::existsAndIsCurrentThread())
        learnRecord (index, newValue);
}

void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    std::array<std::uint8_t, 16> snap {};
    for (int i = 0; i < 16; ++i) snap[(size_t) i] = midiRoute[(size_t) i].load (std::memory_order_relaxed);
    applyMidiRoute (midi, snap, routeScratch);

    const int pc = pendingPc.exchange (-1);
    if (pc >= 0)
        midi.addEvent (juce::MidiMessage::programChange (1, pc), 0);

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

juce::String CustosProcessor::innerSynthKey() const
{
    if (inner == nullptr) return {};
    if (auto* api = dynamic_cast<juce::AudioPluginInstance*> (inner.get()))
    {
        const auto id = api->getPluginDescription().createIdentifierString();
        if (id.isNotEmpty()) return id;
    }
    return inner->getName();   // fallback (tests / non-plugin inners)
}

juce::MemoryBlock CustosProcessor::captureInnerState() const
{
    juce::MemoryBlock mb;
    if (inner != nullptr) inner->getStateInformation (mb);
    return mb;
}

bool CustosProcessor::restoreInnerState (const juce::MemoryBlock& state)
{
    if (inner == nullptr) return false;
    inner->setStateInformation (state.getData(), (int) state.getSize());
    return true;
}

void CustosProcessor::emitPresetRoot()
{
    if (outboundSink)
    {
        juce::OSCMessage m ("/custos/preset/root");
        m.addInt32 (identityN);
        m.addString (presetRootPath);
        outboundSink (m);
    }
}

void CustosProcessor::setPresetRoot (const juce::String& path)
{
    presetRootPath = path;
    const bool persisted = writePresetRoot (presetRootCfg, path);
    emitPresetRoot();
    if (! persisted)
        emitPresetError ("preset root not persisted");

    // The instrument/favourites DB follows the root: adopt what is already at the new location,
    // otherwise carry the current in-memory favourites there so the operator never loses them.
    juce::File newRoot (path);
    if (newRoot.getFullPathName().isNotEmpty())
    {
        auto target = instrumentsFileIn (newRoot);
        if (target.existsAsFile())
        {
            auto adopted = readFavorites (target);
            if (! adopted.empty())
                setFavorites (adopted);                 // adopt real data
            else if (! favorites.empty())
                persistFavorites();                     // target empty/corrupt but we have data -> carry, don't blank
            // else both empty -> nothing to do
        }
        else if (! favorites.empty())
        {
            persistFavorites();
        }
    }
}

bool CustosProcessor::persistFavorites()
{
    if (! writeInstruments (juce::File (presetRootPath), favorites))
    {
        emitPresetError ("favourites write failed");
        return false;
    }
    return true;
}

int CustosProcessor::indexOfPreset (const juce::String& name) const
{
    const auto names = listPresets();
    for (int i = 0; i < (int) names.size(); ++i)
        if (names[(size_t) i].equalsIgnoreCase (name)) return i;
    return -1;
}

std::vector<juce::String> CustosProcessor::listPresets() const
{
    const auto key = innerSynthKey();
    if (key.isEmpty() || presetRootPath.isEmpty()) return {};
    return custos::listPresets (juce::File (presetRootPath), key);
}

int CustosProcessor::savePreset (const juce::String& name)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return -1; }
    if (sanitizePresetName (name).isEmpty()) { emitPresetError ("invalid name"); return -1; }

    PresetData p;
    p.classId    = key;
    p.synthName  = innerSynthName();
    p.presetName = name;
    p.innerState = captureInnerState();
    if (! custos::savePreset (juce::File (presetRootPath), p)) { emitPresetError ("write failed"); return -1; }

    const int idx = indexOfPreset (name);
    emitPreset ("saved", name, idx);
    return idx;
}

bool CustosProcessor::loadPresetByName (const juce::String& name)
{
    if (loadInFlight()) { pendingRecall = PendingRecall { PendingRecall::LoadName, 0, name }; return false; }
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return false; }
    PresetData p;
    if (! custos::loadPreset (juce::File (presetRootPath), key, name, p))
        { emitPresetError ("preset not found"); return false; }
    // Defensive: the lookup above is folder-scoped by `key`, so a mismatch here means a
    // hand-edited/corrupt file, not the spec's cross-synth load path (which can't reach here).
    if (p.classId != key) { emitPresetError ("preset belongs to another synth"); return false; }
    restoreInnerState (p.innerState);
    emitPreset ("loaded", name, indexOfPreset (name));
    return true;
}

bool CustosProcessor::loadPresetAt (int index)
{
    if (loadInFlight()) { pendingRecall = PendingRecall { PendingRecall::LoadIdx, index, {} }; return false; }
    const auto names = listPresets();
    if (index < 0 || index >= (int) names.size()) { emitPresetError ("index out of range"); return false; }
    return loadPresetByName (names[(size_t) index]);
}

bool CustosProcessor::renamePreset (const juce::String& oldName, const juce::String& newName)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return false; }
    if (! custos::renamePreset (juce::File (presetRootPath), key, oldName, newName))
        { emitPresetError ("rename failed"); return false; }
    emitPreset ("renamed", newName, indexOfPreset (newName));
    return true;
}

bool CustosProcessor::deletePreset (const juce::String& name)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return false; }
    if (! custos::deletePreset (juce::File (presetRootPath), key, name))
        { emitPresetError ("delete failed"); return false; }
    emitPreset ("deleted", name, -1);
    return true;
}

void CustosProcessor::stepPreset (int delta)
{
    const auto names = listPresets();
    if (names.empty()) { emitPresetError ("no presets"); return; }
    const int n = (int) names.size();
    presetCursor = presetCursor < 0 ? (delta > 0 ? 0 : n - 1)
                                    : ((presetCursor + delta) % n + n) % n;   // wrap
    emitPreset ("browsing", names[(size_t) presetCursor], presetCursor);
    presetDebounce.cb = [this] { commitPresetLoad(); };
    presetDebounce.startTimer (400);
}

void CustosProcessor::commitPresetLoad()
{
    const auto names = listPresets();
    if (presetCursor >= 0 && presetCursor < (int) names.size())
        loadPresetByName (names[(size_t) presetCursor]);
}

void CustosProcessor::presetNext()
{
    if (loadInFlight()) { pendingRecall = PendingRecall { PendingRecall::Next, 0, {} }; return; }
    stepPreset (+1);
}

void CustosProcessor::presetPrev()
{
    if (loadInFlight()) { pendingRecall = PendingRecall { PendingRecall::Prev, 0, {} }; return; }
    stepPreset (-1);
}

void CustosProcessor::presetSet (int index)
{
    if (loadInFlight()) { pendingRecall = PendingRecall { PendingRecall::SetIdx, index, {} }; return; }
    presetDebounce.stopTimer();
    if (loadPresetAt (index))   // loads + emits; false (+ error) when out of range
        presetCursor = index;
}

void CustosProcessor::patchNext() { patchStep (+1); }
void CustosProcessor::patchPrev() { patchStep (-1); }

void CustosProcessor::patchStep (int delta)
{
    juce::String controlType = "PRESET";
    int pDown = 0, pUp = 0;
    for (const auto& f : favorites)
        if (f.path == currentSynthPath) { controlType = f.controlType; pDown = f.paramDown; pUp = f.paramUp; break; }

    switch (patchMethodFor (controlType))
    {
        case PatchMethod::Param:
            patchInjectParam (delta > 0 ? pUp : pDown);
            emitPatchStepped ("PARAM", delta > 0 ? "+" : "-");
            break;
        case PatchMethod::Pc:
            patchSendProgramChange (delta);
            emitPatchStepped ("PC", juce::String (pcProgram));
            break;
        case PatchMethod::PresetFallback:
            if (delta > 0) presetNext(); else presetPrev();
            break;
    }
}

void CustosProcessor::patchInjectParam (int paramIndex)
{
    releasePendingInject();   // release whatever the previous inject is still holding (reuse-safe)

    if (inner == nullptr) return;
    auto& params = inner->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return;   // out of range -> no-op

    params[paramIndex]->setValueNotifyingHost (1.0f);            // momentary press
    patchInjectIndex = paramIndex;
    patchInjectTimer.cb = [this]
    {
        if (inner != nullptr && patchInjectIndex >= 0 && patchInjectIndex < inner->getParameters().size())
            inner->getParameters()[patchInjectIndex]->setValueNotifyingHost (0.0f);   // release
        patchInjectIndex = -1;
    };
    patchInjectTimer.startTimer (150);   // release ~150 ms later (heavy synths need the hold)
}

void CustosProcessor::releasePendingInject()
{
    // Releases any pending PARAM inject on the CURRENT inner. Called before starting a new inject
    // (reuse-safe: alternating patchNext/patchPrev no longer strands the previous param at 1.0) and
    // before loadInner swaps the inner pointer (swap-safe: the release lands on the OLD synth, not
    // whatever ends up loaded next). No-op when nothing is pending.
    if (patchInjectIndex >= 0 && inner != nullptr && patchInjectIndex < inner->getParameters().size())
        inner->getParameters()[patchInjectIndex]->setValueNotifyingHost (0.0f);
    patchInjectTimer.stopTimer();
    patchInjectIndex = -1;
}

void CustosProcessor::patchSendProgramChange (int delta)
{
    pcProgram = ((pcProgram + delta) % 128 + 128) % 128;   // wrap 0..127
    pendingPc.store (pcProgram);
}

void CustosProcessor::emitPatchStepped (const juce::String& controlType, const juce::String& detail)
{
    if (outboundSink) outboundSink (buildPatchStepped (identityN, controlType, detail));
}

bool CustosProcessor::loadInFlight() const noexcept { return browseDebounce.isTimerRunning(); }

void CustosProcessor::drainPendingRecall()
{
    if (! pendingRecall) return;
    if (inner == nullptr) { pendingRecall.reset(); return; }   // load failed -> drop the pending recall
    const auto pr = *pendingRecall;
    pendingRecall.reset();                                     // clear before replay (no re-buffer/recursion)
    switch (pr.kind)
    {
        case PendingRecall::Next:     presetNext();               break;
        case PendingRecall::Prev:     presetPrev();               break;
        case PendingRecall::SetIdx:   presetSet (pr.index);       break;
        case PendingRecall::LoadName: loadPresetByName (pr.name); break;
        case PendingRecall::LoadIdx:  loadPresetAt (pr.index);    break;
    }
}

void CustosProcessor::emitPreset (const juce::String& verb, const juce::String& name, int idx)
{
    if (! outboundSink) return;
    juce::OSCMessage m ("/custos/preset/" + verb);
    m.addInt32 (identityN);
    m.addString (name);
    m.addInt32 (idx);
    outboundSink (m);
}

void CustosProcessor::emitPresetError (const juce::String& reason)
{
    if (! outboundSink) return;
    juce::OSCMessage m ("/custos/preset/error");
    m.addInt32 (identityN);
    m.addString (reason);
    outboundSink (m);
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
        windowMode = WinBorderless;
    }
    refreshEditor();
}

void CustosProcessor::showSynthWindowBorderless (bool movable)   // "Open" with fixed=on
{
    showSynthWindow();                       // borderless, natural size, centred (the proven path)
    windowBorderlessMovable = movable;
    if (synthWindow != nullptr) synthWindow->setDraggable (movable);
}

void CustosProcessor::showSynthWindowTitled()
{
    if (inner == nullptr) return;                          // nothing to show
    if (titledWindow != nullptr) { titledWindow->toFront (true); return; }
    if (auto* ed = inner->createEditorAndMakeActive())     // null if the synth has no editor
    {
        auto w = std::make_unique<TitledSynthWindow> (inner->getName(), ed);
        w->setAlwaysOnTop (onTopMode == OnTopInstrument);
        // Native title-bar close. NEVER delete the window synchronously here: closeButtonPressed()
        // runs from inside the window's own event handling, and JUCE keeps touching the window after
        // this returns -> use-after-free. Defer the teardown to the next message loop, guarded by
        // aliveToken so a queued callback no-ops if the processor was destroyed meanwhile.
        w->onCloseButton = [this, weak = std::weak_ptr<bool> (aliveToken)]
        {
            juce::MessageManager::callAsync ([this, weak]
            {
                if (weak.lock() == nullptr) return;   // processor gone -> no-op
                titledWindow.reset();
                windowMode = WinNone;
                refreshEditor();
            });
        };
        titledWindow = std::move (w);
        windowMode = WinTitled;
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
    windowMode = WinNone;
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
