#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

TEST_CASE ("Sticky re-impose never fights the editor: achieved size kept, scaled into the fit")
{
    // Live 2026-07-19 (TR-909 N=10, JD-800 N=9): "resizable" Roland/Arturia editors accept
    // setSize, then re-assert their preferred size ~20 ms later — re-imposing setSize would
    // ping-pong forever and the window ended up following the editor out of the dock. The fix:
    // on a content self-resize under sticky fit, ADOPT the achieved editor size and scale it
    // uniformly (aspect-preserved, centred) into the fit rect; the window stays pinned.
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 396, 149);
    SynthWindow win (ed);

    const juce::Rectangle<int> fitTo (100, 100, 1200, 452);
    win.applyRect (fitTo, false, /*sticky*/ true);

    ed->settleToNatural();               // the editor re-asserts 396x149 (refuses the fitted size)
    REQUIRE (win.getBounds() == fitTo);  // window pinned at the fit rect...
    REQUIRE (ed->getWidth()  == 396);    // ...and the editor KEEPS its size — no setSize fight
    REQUIRE (ed->getHeight() == 149);
    const auto t = ed->getTransform();
    const float s = juce::jmin (1200.0f / 396.0f, 452.0f / 149.0f);   // window-border slack -> loose tolerance
    REQUIRE (std::abs (t.mat00 - s) < 0.05f);   // uniform scale fits the achieved size into the rect
    REQUIRE (std::abs (t.mat11 - t.mat00) < 0.001f);   // ...and IS uniform (no stretch)
}

TEST_CASE ("A compliant resizable editor stays pixel-sharp: identity transform after the fit")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 400, 300);
    SynthWindow win (ed);

    win.applyRect ({ 0, 0, 800, 600 }, false, /*sticky*/ true);
    REQUIRE (std::abs (ed->getWidth() - 800) <= 2);   // editor accepted the size (window border slack)
    REQUIRE (ed->getTransform().isIdentity());        // no scaling layered on top -> pixel-sharp
}

TEST_CASE ("A non-resizable editor is scaled uniformly and centred, never stretched")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ false, /*nat*/ 400, 300);
    SynthWindow win (ed);

    win.applyRect ({ 0, 0, 800, 300 }, false, /*sticky*/ false);
    const auto t = ed->getTransform();
    REQUIRE (std::abs (t.mat00 - 1.0f) < 0.02f);       // uniform: min(~800/400, ~300/300) = ~1
    REQUIRE (std::abs (t.mat11 - t.mat00) < 0.001f);   // NOT the old anisotropic fill (x2 wide)
    REQUIRE (std::abs (t.mat02 - 200.0f) < 4.0f);      // centred horizontally in the rect (border slack)
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
