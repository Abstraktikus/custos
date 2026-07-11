#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <memory>

using namespace custos;

namespace
{
// A fake inner whose parameter list can GROW after construction, mirroring how
// Roland ZenCore plugins populate their VST3 params only once their engine has
// initialised (later than createPluginInstance returns). grow(..., notify=true)
// also raises the VST3 restartComponent(kParamTitlesChanged) analogue.
struct GrowableInner : custos::test::FakeInnerProcessor
{
    explicit GrowableInner (int n) : FakeInnerProcessor (n) {}

    void grow (int extra, bool notify)
    {
        const int base = getParameters().size();
        for (int i = 0; i < extra; ++i)
            addParameter (new juce::AudioParameterFloat (
                juce::ParameterID { "grow" + juce::String (base + i), 1 },
                "Grow " + juce::String (base + i), 0.0f, 1.0f, 0.25f));
        if (notify)
            updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}
                                   .withParameterInfoChanged (true));
    }
};
}

TEST_CASE ("CustosProcessor re-binds when the inner announces params after the initial bind")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 128);

    auto fake = std::make_unique<GrowableInner> (0);   // cold: reports 0 params at load time
    auto* fakePtr = fake.get();
    proc.loadInner (std::move (fake), "C:/x/Roland.vst3");
    REQUIRE (proc.boundParamCount() == 0);             // the frozen-0 symptom: nothing to bind yet

    fakePtr->grow (5, /*notify*/ true);                // params populate + restartComponent(kParamTitlesChanged)
    REQUIRE (proc.boundParamCount() == 5);             // Custos must re-bind automatically
}

TEST_CASE ("CustosProcessor idempotent load recovers a bind frozen at cold-load")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 128);

    auto fake = std::make_unique<GrowableInner> (0);
    auto* fakePtr = fake.get();
    proc.loadInner (std::move (fake), "C:/x/Roland.vst3");
    REQUIRE (proc.boundParamCount() == 0);

    fakePtr->grow (7, /*notify*/ false);               // params populated, but NO notification arrived
    REQUIRE (proc.boundParamCount() == 0);             // still frozen

    const auto r = proc.load ("C:/x/Roland.vst3");     // GP-Script re-sends the same synth on gig-open
    REQUIRE (r.ok);
    REQUIRE (r.message.startsWith ("already loaded"));
    REQUIRE (proc.boundParamCount() == 7);             // the idempotent re-send recovered the bind
}
