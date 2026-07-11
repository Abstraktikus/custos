#pragma once
#include <array>
#include <juce_osc/juce_osc.h>
#include "FavoritesStore.h"

namespace custos
{
class CustosProcessor;

struct Command
{
    enum Kind { Load, Clear, Hello, Params, Volume, FavBegin, FavEntry, FavEnd,
                WindowShow, WindowTitled, WindowHide, WindowRect, MidiRoute, MidiQuery,
                BrowseNext, BrowsePrev, BrowseSet, InstrumentLoad,
                PresetSetRoot, PresetSave, PresetList, PresetLoad, PresetNext, PresetPrev,
                PresetSet, PresetRename, PresetDelete, MainLR, MainLRQuery, PresetQueryRoot,
                LearnStart, LearnStop,
                PatchNext, PatchPrev, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params; count also = FavEnd count
    float gainDb = 0.0f;        // Volume
    bool mainLROn = false;      // MainLR: fold all inner outputs onto stereo Out 1
    Favorite fav;               // FavEntry
    int rx = 0, ry = 0, rw = 0, rh = 0;   // WindowRect (physical px)
    bool movable = false;                 // WindowRect
    bool clamp = false;                   // WindowRect: constrain to the monitor work area (config phase)
    bool fit = false;                     // WindowRect: treat the rect as an available area — fit the editor
                                          //   into it preserving aspect ratio, centred (docking)
    int marginLogical = 0;                // WindowRect fit: frame (logical px) left on all sides of the area
    std::array<int, 16> route {};   // MidiRoute: target output per input channel (0 = drop)
    juce::String presetName, presetNewName, rootPath;   // preset verbs
    int presetIndex = -1;                               // PresetSet, or PresetLoad-by-index
    int scope = 0;              // BrowseNext/Prev: 0 = favourites, 1 = all
};

// Pure dispatch: map an OSC message to a Command (no side effects) — unit-testable without a socket.
Command parseCommand (const juce::OSCMessage& msg);

// GP mirror policy (pure, unit-testable). The GP-Script drives the Voice-Selector AND direct
// instrument load autonomously (no KM in the loop), so it needs the browse preview, the
// load-ready signal, instance liveness, and load FAILURES — but not the success acks
// (/custos/loaded already conveys success) nor the param-dump flood. Everything else stays
// KM-hub-only. `ackText` is only consulted for /custos/ack (error → mirror).
inline bool gpMirrorsFeedback (const juce::String& addr, const juce::String& ackText)
{
    if (addr == "/custos/browsing" || addr == "/custos/loaded" || addr == "/custos/here"
        || addr == "/custos/patch/stepped")
        return true;
    if (addr == "/custos/preset/browsing" || addr == "/custos/preset/loaded"
        || addr == "/custos/preset/error")
        return true;
    if (addr == "/custos/ack")
        return ackText.startsWith ("error");
    return false;
}

// Binds CUSTOS_OSC_PORT, turns /custos/load|/custos/clear into processor calls, acks to KM.
// Owned by CustosProcessor. A failed bind is logged, not fatal.
class CustosOscServer : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit CustosOscServer (CustosProcessor&);
    ~CustosOscServer() override;

    // (Re)bind the OSC receiver to BASE+n. Returns false on invalid n or a port clash (N collision).
    // On success, announces /custos/here. Message thread only.
    bool bindToIdentity (int n);

private:
    void oscMessageReceived (const juce::OSCMessage&) override;
    void ack (const juce::String& text);
    void announceHere();
    void maybeMirrorToGp (const juce::OSCMessage&);   // GP :54344, gated by gpMirrorsFeedback
    void emitPresetList();   // builds /custos/preset/list (identity + count + names) to the KM hub

    CustosProcessor& proc;
    juce::OSCReceiver receiver;
    juce::OSCSender   ackSender;   // KM hub :8000 (all feedback)
    juce::OSCSender   gpSender;    // GP OSC-in :54344 (mirrors browsing + loaded + here + error-acks)
    bool ackReady = false;
    bool gpReady  = false;
    int  currentN = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosOscServer)
};
}
