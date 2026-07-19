#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "SynthWindow.h"
#include "FakeInnerProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

using namespace custos;

// Dock-fit behaviour against the THREE editor personalities seen live (2026-07-19):
//  - compliant (JD-800): accepts the polite setSize -> identity placement, pixel-sharp.
//  - clamping (TR-909, Jup-8000): reports resizable but snaps back to its fixed size; a JUCE
//    AffineTransform cannot scale these (native child HWND renders at its own size) -> the
//    window must negotiate: one IPlugView content-scale attempt, else centre the visible region.
//  - scale-honouring: accepts setScaleFactor (setContentScaleFactor) and re-renders smaller.

namespace
{
// Accepts any size; can later "settle" back to natural (async re-assert ~20 ms after a fit).
struct FakeHostedEditor : juce::AudioProcessorEditor
{
    FakeHostedEditor (juce::AudioProcessor& p, bool resizable, int natW, int natH)
        : juce::AudioProcessorEditor (p), naturalW (natW), naturalH (natH)
    {
        setResizable (resizable, false);
        setSize (natW, natH);
    }
    void settleToNatural() { setSize (naturalW, naturalH); }   // simulate the hosted synth's self-resize
    void setScaleFactor (float f) override { scaleCalls.push_back (f); }   // base: record, ignore
    int naturalW, naturalH;
    std::vector<float> scaleCalls;
};

// Synchronously snaps back to natural whenever any other size is imposed (fixed-zoom GUI).
struct ClampingEditor : FakeHostedEditor
{
    using FakeHostedEditor::FakeHostedEditor;
    void resized() override
    {
        if (! snapping && (getWidth() != naturalW || getHeight() != naturalH))
        {
            const juce::ScopedValueSetter<bool> guard (snapping, true);
            setSize (naturalW, naturalH);
        }
    }
    bool snapping = false;
};

// Clamps like above, but honours a content-scale factor by re-rendering (resizing) to natural*f
// — the shape of JUCE's VST3PluginWindow::setScaleFactor (setContentScaleFactor + resizeToFit).
struct ScaleHonouringEditor : ClampingEditor
{
    using ClampingEditor::ClampingEditor;
    void setScaleFactor (float f) override
    {
        scaleCalls.push_back (f);
        const juce::ScopedValueSetter<bool> guard (snapping, true);   // scaled size is now "natural"
        setSize (juce::roundToInt ((float) naturalW * f), juce::roundToInt ((float) naturalH * f));
    }
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
    // window would follow it down to 396x149; the sticky fit must keep the window pinned.
    ed->settleToNatural();
    REQUIRE (win.getBounds() == fitTo);

    // A repeated late settle (cold Roland settles more than once) must also hold.
    ed->settleToNatural();
    REQUIRE (win.getBounds() == fitTo);
}

TEST_CASE ("A compliant resizable editor stays pixel-sharp: no scale calls, size taken")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 400, 300);
    SynthWindow win (ed);

    win.applyRect ({ 0, 0, 800, 600 }, false, /*sticky*/ true);
    REQUIRE (std::abs (ed->getWidth() - 800) <= 2);   // editor accepted the size (window border slack)
    REQUIRE (ed->scaleCalls.empty());                 // no content-scale negotiation needed
}

TEST_CASE ("A clamping editor that ignores content scale is clip-centred, window pinned")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new ClampingEditor (proc, /*resizable*/ true, /*nat*/ 1490, 556);
    SynthWindow win (ed);

    const juce::Rectangle<int> fitTo (100, 150, 748, 280);   // content area 746x278
    win.applyRect (fitTo, false, /*sticky*/ true);

    REQUIRE (win.getBounds() == fitTo);      // pinned regardless
    REQUIRE (ed->getWidth()  == 1490);       // clamped back — we do not fight it
    REQUIRE (ed->getHeight() == 556);

    // One scale attempt (~0.5) was made and, unhonoured, rolled back to 1.
    REQUIRE (ed->scaleCalls.size() == 2);
    REQUIRE (std::abs (ed->scaleCalls[0] - 0.5f) < 0.05f);
    REQUIRE (std::abs (ed->scaleCalls[1] - 1.0f) < 0.001f);

    // The visible region is the MIDDLE of the GUI: symmetric clip -> negative origin.
    const auto pos = ed->getPosition();
    REQUIRE (std::abs (pos.x - (1 + (746 - 1490) / 2)) <= 2);
    REQUIRE (std::abs (pos.y - (1 + (278 - 556) / 2)) <= 2);
}

TEST_CASE ("A scale-honouring editor re-renders smaller and fits the dock")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new ScaleHonouringEditor (proc, /*resizable*/ true, /*nat*/ 1490, 556);
    SynthWindow win (ed);

    const juce::Rectangle<int> fitTo (100, 150, 748, 280);   // content area 746x278
    win.applyRect (fitTo, false, /*sticky*/ true);

    REQUIRE (win.getBounds() == fitTo);
    REQUIRE (! ed->scaleCalls.empty());
    REQUIRE (std::abs (ed->scaleCalls.back() - 0.5f) < 0.05f);   // attempt honoured, NOT rolled back
    REQUIRE (ed->getWidth()  <= 748);                            // re-rendered into the area
    REQUIRE (ed->getHeight() <= 280);
    REQUIRE (ed->getPosition().x >= 0);                          // fits -> no clip offset
}

TEST_CASE ("An async re-assert after an accepted fit negotiates once, then clip-centres")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new FakeHostedEditor (proc, /*resizable*/ true, /*nat*/ 1490, 556);
    SynthWindow win (ed);

    const juce::Rectangle<int> fitTo (100, 150, 748, 280);
    win.applyRect (fitTo, false, /*sticky*/ true);   // fake ACCEPTS 746x278 (like the live wrapper)

    ed->settleToNatural();                           // ~20 ms later: plugin re-asserts 1490x556
    REQUIRE (win.getBounds() == fitTo);              // pinned
    REQUIRE (ed->getWidth() == 1490);                // adopted, not fought
    REQUIRE (! ed->scaleCalls.empty());              // the scale attempt happened on the re-assert
    REQUIRE (ed->getPosition().x < 0);               // clip-centred (middle of the GUI visible)
}

TEST_CASE ("An achieved size smaller than the area is letterboxed (centred, unscaled)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::FakeInnerProcessor proc;
    auto* ed = new ClampingEditor (proc, /*resizable*/ false, /*nat*/ 400, 150);
    SynthWindow win (ed);

    win.applyRect ({ 0, 0, 800, 300 }, false, /*sticky*/ false);
    REQUIRE (ed->getWidth()  == 400);                       // untouched (non-resizable, fits)
    REQUIRE (ed->getHeight() == 150);
    const auto pos = ed->getPosition();
    REQUIRE (std::abs (pos.x - 200) <= 2);                  // centred in the 798x298 content area
    REQUIRE (std::abs (pos.y - 75)  <= 2);
    REQUIRE (ed->scaleCalls.empty());                       // fits -> no negotiation
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
