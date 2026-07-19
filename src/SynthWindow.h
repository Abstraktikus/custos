#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace custos
{
// A borderless top-level window hosting a hosted plugin's editor. No title bar, no close button;
// closed by the owner (CustosProcessor) via OSC / the editor. Optionally draggable by its body.
class SynthWindow : public juce::ResizableWindow
{
public:
    explicit SynthWindow (juce::Component* editor);   // takes ownership of the editor (content)

    void setDraggable (bool shouldBeDraggable) noexcept { draggable = shouldBeDraggable; }
    // Place the window at the rect and fit the editor into it: one polite setSize when it reports
    // resizable, then layoutContent() scales the ACHIEVED size uniformly (aspect-preserved,
    // centred) — editors that clamp/refuse sizes are never fought over (no setSize ping-pong).
    // sticky=true (docking/fit) keeps this rect against the hosted editor's own later resizes
    // (init/settle/preset-load) instead of following it back to natural size. sticky=false (plain
    // move / undock) restores the default resize-to-fit-content behaviour (synth-zoom follows).
    void applyRect (juce::Rectangle<int> logical, bool movable, bool sticky);

    std::function<void()> onReadout;   // live x/y/w/h readout — fired on every move AND resize
    std::function<void()> onCommit;    // OSC position/size feedback — drag-end + content-driven resize (synth zoom)

    void moved()   override;
    void resized() override;
    void childBoundsChanged (juce::Component* child) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    juce::ComponentDragger dragger;
    bool draggable    = false;
    bool dragged      = false;   // did the current gesture actually move the window?
    bool applyingRect = false;   // inside applyRect() → suppress the resize-driven commit (avoids double-emit)
    bool inLayout     = false;   // inside layoutContent() → its own move/transform callbacks are swallowed
    bool fitActive    = false;   // a sticky (docked) fit is in force → re-pin it when the content self-resizes
    juce::Rectangle<int> fitLogical;   // the rect to re-pin while fitActive

    // Position/scale the editor within the window WITHOUT resizing it: identity + default placement
    // when its size matches the content area (within layout rounding), else a uniform centred
    // AffineTransform fit of the achieved size. Replaces ResizableWindow's force-sizing layout.
    void layoutContent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWindow)
};
}
