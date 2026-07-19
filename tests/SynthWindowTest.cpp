#include <catch2/catch_test_macros.hpp>
#include "SynthWindow.h"
#include "FakeInnerProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

using namespace custos;

namespace
{
// A hosted editor we can tell to "settle" back to its natural size after the window has
// fitted it — reproducing the init / preset-load self-resize that undid the dock fit.
struct FakeHostedEditor : juce::AudioProcessorEditor
{
    FakeHostedEditor (juce::AudioProcessor& p, bool resizable, int natW, int natH)
        : juce::AudioProcessorEditor (p), naturalW (natW), naturalH (natH)
    {
        setResizable (resizable, false);
        setSize (natW, natH);
    }
    void settleToNatural() { setSize (naturalW, naturalH); }   // simulate the hosted synth's self-resize
    int naturalW, naturalH;
};
}

TEST_CASE ("Sticky fit holds when the hosted editor settles back to its natural size")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 396, 149);
    SynthWindow win (ed);   // takes ownership of the editor

    const juce::Rectangle<int> fitTo (100, 100, 1200, 452);
    win.applyRect (fitTo, /*movable*/ false, /*sticky*/ true);
    REQUIRE (win.getBounds() == fitTo);

    // The hosted synth now resizes itself to natural (init / preset-load). A non-sticky
    // window would follow it down to 396x149; the sticky fit must re-impose fitTo instead.
    ed->settleToNatural();
    REQUIRE (win.getBounds() == fitTo);

    // A repeated late settle (cold Roland settles more than once) must also hold.
    ed->settleToNatural();
    REQUIRE (win.getBounds() == fitTo);
}

TEST_CASE ("A non-sticky window still follows content-driven resizes (synth zoom)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 400, 300);
    SynthWindow win (ed);

    win.applyRect ({ 50, 50, 400, 300 }, /*movable*/ true, /*sticky*/ false);

    // Free (undocked) window: the user zooms the synth -> the window must track it so KM
    // gets the content-driven readout. Sticky handling must NOT hijack this case.
    ed->setSize (800, 600);
    REQUIRE (win.getContentComponent()->getWidth() == 800);
    REQUIRE (win.getWidth() >= 800);   // window grew to track the content (+ any content border)
}

TEST_CASE ("A plain (non-sticky) applyRect after a sticky fit releases the fit")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 396, 149);
    SynthWindow win (ed);

    win.applyRect ({ 100, 100, 1200, 452 }, false, /*sticky*/ true);
    win.applyRect ({ 0, 0, 396, 149 }, true, /*sticky*/ false);   // undock -> plain move

    // Sticky must be cleared: a later content self-resize is now followed, not re-fitted.
    ed->setSize (500, 200);
    REQUIRE (win.getContentComponent()->getWidth() == 500);
}
