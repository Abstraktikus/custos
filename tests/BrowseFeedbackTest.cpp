#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "OscContract.h"   // kBrowseNoneName — arg2 of the no-instrument exit signal

using namespace custos;

// An empty (or effectively empty) browse set must SAY so, not step a bogus cursor. Before the
// 2026-07-19 robustness fix, scope-0 browsing over a list with no matching favourite emitted
// /custos/browsing with a wrapped=1 flag and an arbitrary NON-matching entry (and armed the
// deferred load for it); a fully empty list was silently ignored. Both hid the runtime state
// (in-memory DB empty) from KM/GP entirely.
//
// That fix returned early with an error-ack only — which stranded the GP rackspace: GP's ONLY
// exit from its browse submode is an inbound /custos/browsing with wrapped=1, and it has no
// handler for /custos/ack. So the error paths must ALSO emit a bare EXIT SIGNAL: one
// /custos/browsing, wrapped=1, all four args, no cursor step, no deferred load, and no bogus
// instrument name in arg2.

namespace
{
struct Sink
{
    std::vector<juce::OSCMessage> sent;
    int acks (const juce::String& contains) const
    {
        int n = 0;
        for (const auto& m : sent)
            if (m.getAddressPattern().toString() == "/custos/ack"
                && m.size() >= 2 && m[1].isString() && m[1].getString().contains (contains)) ++n;
        return n;
    }
    int count (const juce::String& addr) const
    {
        int n = 0;
        for (const auto& m : sent)
            if (m.getAddressPattern().toString() == addr) ++n;
        return n;
    }
    const juce::OSCMessage* first (const juce::String& addr) const
    {
        for (const auto& m : sent)
            if (m.getAddressPattern().toString() == addr) return &m;
        return nullptr;
    }
};

// The exit signal is a /custos/browsing that carries all four args (GP drops ArgCount < 4),
// wrapped=1 (GP's exit trigger), an untouched cursor and the explicit no-instrument marker
// instead of a name the operator could mistake for something loadable.
void requireExitSignal (const Sink& sink, int expectedIdentity)
{
    REQUIRE (sink.count ("/custos/browsing") == 1);
    const auto* m = sink.first ("/custos/browsing");
    REQUIRE (m != nullptr);
    REQUIRE (m->size() == 4);                       // GP discards anything shorter
    REQUIRE (m->operator[] (0).getInt32() == expectedIdentity);
    REQUIRE (m->operator[] (1).getInt32() == -1);   // cursor unchanged (never seeded, never stepped)
    REQUIRE (m->operator[] (2).getString() == custos::kBrowseNoneName);
    REQUIRE (m->operator[] (3).getInt32() == 1);    // wrapped=1
}

// The exit signal must not arm the 400 ms browse debounce — there is nothing to load.
void requireNoDeferredLoad (const CustosProcessor& proc)
{
    REQUIRE (proc.loadInFlight() == false);
}

Favorite fav (const juce::String& name, int favOrder, int slots)
{
    Favorite f;
    f.name = name; f.path = "C:/x/" + name + ".vst3"; f.favOrder = favOrder; f.slots = slots;
    return f;
}
}

TEST_CASE ("browse on an empty instrument list acks the error and signals the exit")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.acks ("instrument list empty") == 1);   // KM + trace log still get the diagnosis
    requireExitSignal (sink, proc.identity());        // GP still gets out of its browse submode
    requireNoDeferredLoad (proc);
}

TEST_CASE ("instrument/set on an empty list acks the error and signals the exit")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.setBrowseIndex (0);
    REQUIRE (sink.acks ("instrument list empty") == 1);
    requireExitSignal (sink, proc.identity());
    requireNoDeferredLoad (proc);
}

TEST_CASE ("scope-0 browse with zero favourites among entries signals the exit, steps nothing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ fav ("NotAFav", 0, 100), fav ("AlsoNot", 0, 100) });
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.acks ("no browsable instrument") == 1);
    requireExitSignal (sink, proc.identity());   // marker name, NOT "NotAFav"/"AlsoNot"
    requireNoDeferredLoad (proc);
}

TEST_CASE ("scope-0 browse where every favourite is oversized signals the exit, steps nothing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    const int cap = proc.facadeSize();
    proc.setFavorites ({ fav ("TooBig", 1, cap * 2) });   // grossly oversized (past the 10% tolerance)
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.acks ("no browsable instrument") == 1);
    requireExitSignal (sink, proc.identity());   // marker name, NOT "TooBig"
    requireNoDeferredLoad (proc);
}

TEST_CASE ("a browsable favourite still browses normally, with no error ack (regression)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ fav ("Fine", 1, 100) });
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.count ("/custos/browsing") == 1);
    REQUIRE (sink.count ("/custos/ack") == 0);
}

// Counterpart to requireNoDeferredLoad above: proves that probe actually discriminates, i.e. that a
// browse which DOES have somewhere to go leaves the debounce armed.
TEST_CASE ("a normal browse arms the deferred load")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ fav ("Fine", 1, 100) });

    REQUIRE (proc.loadInFlight() == false);
    proc.browseInstrument (+1, 0);
    REQUIRE (proc.loadInFlight() == true);
}

TEST_CASE ("scope-1 browse reaches non-favourites: no error when only favOrder=0 entries exist")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ fav ("NotAFav", 0, 100) });
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 1);
    REQUIRE (sink.count ("/custos/browsing") == 1);
    REQUIRE (sink.count ("/custos/ack") == 0);
}
