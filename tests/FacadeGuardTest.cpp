#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "InstrumentBrowser.h"
#include "FakeInnerProcessor.h"

using namespace custos;

// Repro of the 2026-07-19 live incident (WER AppHangB1, GigPerformer5.exe): an OSC-initiated
// load directed a big synth at a small-facade Custos. The heavy VST3 instantiation runs
// synchronously inside the OSC callback ON THE MESSAGE THREAD; when it froze, GP's UI and the
// OSC handling of EVERY Custos instance in the process froze with it (all instances' callbacks
// share that one thread) — the observed "all 10 ports dead".
//
// The facade guard (favouriteFits) exists but only covers the browse cursor and the browse
// commit. The direct load paths — /custos/load (KM) and /custos/instrument/load (GP song load)
// — accept a known-oversized synth without a check. These tests pin the guard everywhere the
// instrument list gives Custos the knowledge to refuse.

static std::vector<Favorite> listWith (const juce::String& name, const juce::String& path, int slots)
{
    Favorite f;
    f.name = name; f.path = path; f.favOrder = 1; f.slots = slots;
    return { f };
}

// A real, tiny, instantiable VST3 for end-to-end loader tests: CUSTOS_TEST_SYNTH if set,
// else the local Tacet placeholder build. Empty string = neither available (tests self-skip).
static juce::String realTestSynthPath()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("CUSTOS_TEST_SYNTH", {});
    if (env.isNotEmpty()) return env;
    const juce::File tacet ("C:/dev/tacet/target/bundled/tacet.vst3");
    if (tacet.exists()) return tacet.getFullPathName();
    return {};
}

TEST_CASE ("load() rejects a list entry whose slots exceed the facade cap")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    const int cap = proc.facadeSize();
    proc.setFavorites (listWith ("BigSynth", "C:/x/BigSynth.vst3", cap * 2));

    const auto r = proc.load ("C:/x/BigSynth.vst3");
    REQUIRE (r.ok == false);
    REQUIRE (r.message.contains ("too large"));   // refused by the guard, NOT a failed load attempt
    REQUIRE (proc.hasInnerSynth() == false);
}

TEST_CASE ("load() tolerates up to 10% oversize — proceeds with a warning ack")
{
    // The live rig runs Jup-8000 V (3058) and Memory V (3168) in 3000 facades: the top params
    // stay unbound (binding clamps), which was always survivable. Refusing them took two slots
    // down on 2026-07-19 — small overshoot now loads and SAYS so.
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    const int cap = proc.facadeSize();
    proc.setFavorites (listWith ("SlightlyBig", "C:/x/SlightlyBig.vst3", cap + cap / 10));

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    const auto r = proc.load ("C:/x/SlightlyBig.vst3");
    REQUIRE (! r.message.contains ("too large"));   // NOT refused...
    REQUIRE (r.message.contains ("not found"));     // ...the loader was actually attempted (fake path)
    bool warned = false;
    for (const auto& m : sent)
        if (m.getAddressPattern().toString() == "/custos/ack" && m.size() >= 2 && m[1].isString()
            && m[1].getString().startsWith ("warning instrument oversized")) warned = true;
    REQUIRE (warned);
}

TEST_CASE ("load() refuses one param past the 10% tolerance")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    const int cap = proc.facadeSize();
    proc.setFavorites (listWith ("JustTooBig", "C:/x/JustTooBig.vst3", cap + cap / 10 + 1));

    const auto r = proc.load ("C:/x/JustTooBig.vst3");
    REQUIRE (r.ok == false);
    REQUIRE (r.message.contains ("too large"));
}

TEST_CASE ("load() at exactly the facade cap is allowed through to the loader")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    const int cap = proc.facadeSize();
    proc.setFavorites (listWith ("FitsSynth", "C:/x/FitsSynth.vst3", cap));

    // The fake path means SynthLoader fails with "not found" — the point is the guard must
    // NOT have refused it ("too large" never appears for a fitting synth).
    const auto r = proc.load ("C:/x/FitsSynth.vst3");
    REQUIRE (r.ok == false);
    REQUIRE (! r.message.contains ("too large"));
    REQUIRE (r.message.contains ("not found"));
}

TEST_CASE ("load() with unknown slots (0) is allowed — an empty/unpushed DB must not block loads")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites (listWith ("Mystery", "C:/x/Mystery.vst3", 0));

    const auto r = proc.load ("C:/x/Mystery.vst3");
    REQUIRE (! r.message.contains ("too large"));   // unknown -> benefit of the doubt (documented policy)
}

TEST_CASE ("load() of a path NOT in the list is allowed — no data, no guard")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;   // empty list
    const auto r = proc.load ("C:/x/Stranger.vst3");
    REQUIRE (! r.message.contains ("too large"));
}

TEST_CASE ("INCIDENT REPRO: /custos/load path instantiates a synth the list marks oversized")
{
    const auto synth = realTestSynthPath();
    if (synth.isEmpty()) { WARN ("no real test synth (set CUSTOS_TEST_SYNTH) - skipped"); return; }

    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    const int cap = proc.facadeSize();
    // The list says this synth does NOT fit this facade (the incident: big synth, small Custos).
    proc.setFavorites (listWith ("TooBig", synth, cap * 2));

    // Desired: refused before the (message-thread-blocking) instantiation ever starts.
    // Today: the load goes straight through and the synth is live — this is the hole.
    const auto r = proc.load (synth);
    REQUIRE (r.ok == false);
    REQUIRE (r.message.contains ("too large"));
    REQUIRE (proc.hasInnerSynth() == false);
}

TEST_CASE ("INCIDENT REPRO: /custos/instrument/load (GP song load) honours the size guard")
{
    const auto synth = realTestSynthPath();
    if (synth.isEmpty()) { WARN ("no real test synth (set CUSTOS_TEST_SYNTH) - skipped"); return; }

    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    const int cap = proc.facadeSize();
    proc.setFavorites (listWith ("TooBig", synth, cap * 2));

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.loadByName ("TooBig");   // name resolves; the guard must stop the load itself
    REQUIRE (proc.hasInnerSynth() == false);
    for (const auto& m : sent)
        REQUIRE (m.getAddressPattern().toString() != "/custos/loaded");
}

TEST_CASE ("an over-cap inner binds clamped, never past the facade (blast-radius floor)")
{
    // Even when a too-big synth DOES get in (unknown slots / direct path), binding must stay
    // clamped to the facade — the top params are unbound, nothing indexes out of range.
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    const int cap = proc.facadeSize();

    REQUIRE (proc.loadInner (std::make_unique<custos::test::FakeInnerProcessor> (cap + 250), "C:/x/Big.vst3"));
    REQUIRE (proc.boundParamCount() == cap);
    REQUIRE (proc.innerParamTotal() == cap + 250);
}
