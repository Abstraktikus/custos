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
                BrowseNext, BrowsePrev, BrowseSet, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params; count also = FavEnd count
    float gainDb = 0.0f;        // Volume
    Favorite fav;               // FavEntry
    int rx = 0, ry = 0, rw = 0, rh = 0;   // WindowRect (physical px)
    bool movable = false;                 // WindowRect
    bool clamp = false;                   // WindowRect: constrain to the monitor work area (config phase)
    std::array<int, 16> route {};   // MidiRoute: target output per input channel (0 = drop)
};

// Pure dispatch: map an OSC message to a Command (no side effects) — unit-testable without a socket.
Command parseCommand (const juce::OSCMessage& msg);

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

    CustosProcessor& proc;
    juce::OSCReceiver receiver;
    juce::OSCSender   ackSender;
    bool ackReady = false;
    int  currentN = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosOscServer)
};
}
