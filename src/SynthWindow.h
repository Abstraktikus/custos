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
    // Resize the editor to the logical size (if it is resizable) else scale it to fit; place the window.
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
    bool fitActive    = false;   // a sticky (docked) fit is in force → re-impose it when the content self-resizes
    juce::Rectangle<int> fitLogical;   // the rect to re-impose while fitActive

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWindow)
};
}
