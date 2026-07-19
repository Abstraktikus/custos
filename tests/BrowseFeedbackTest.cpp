#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"

using namespace custos;

// An empty (or effectively empty) browse set must SAY so, not step a bogus cursor. Before this
// fix, scope-0 browsing over a list with no matching favourite emitted /custos/browsing with a
// wrapped=1 flag and an arbitrary NON-matching entry (and armed the deferred load for it); a
// fully empty list was silently ignored. Both hid the 2026-07-19 runtime state (in-memory DB
// empty) from KM/GP entirely.

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
};

Favorite fav (const juce::String& name, int favOrder, int slots)
{
    Favorite f;
    f.name = name; f.path = "C:/x/" + name + ".vst3"; f.favOrder = favOrder; f.slots = slots;
    return f;
}
}

TEST_CASE ("browse on an empty instrument list emits an explicit error ack, no browsing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.acks ("instrument list empty") == 1);
    REQUIRE (sink.count ("/custos/browsing") == 0);
}

TEST_CASE ("instrument/set on an empty list emits the same explicit error ack")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.setBrowseIndex (0);
    REQUIRE (sink.acks ("instrument list empty") == 1);
    REQUIRE (sink.count ("/custos/browsing") == 0);
}

TEST_CASE ("scope-0 browse with zero favourites among entries reports instead of stepping bogus")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ fav ("NotAFav", 0, 100), fav ("AlsoNot", 0, 100) });
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.acks ("no browsable instrument") == 1);
    REQUIRE (sink.count ("/custos/browsing") == 0);   // no bogus cursor step / deferred load
}

TEST_CASE ("scope-0 browse where every favourite is oversized reports instead of stepping bogus")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    const int cap = proc.facadeSize();
    proc.setFavorites ({ fav ("TooBig", 1, cap + 1) });
    Sink sink;
    proc.outboundSink = [&sink] (const juce::OSCMessage& m) { sink.sent.push_back (m); };

    proc.browseInstrument (+1, 0);
    REQUIRE (sink.acks ("no browsable instrument") == 1);
    REQUIRE (sink.count ("/custos/browsing") == 0);
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
