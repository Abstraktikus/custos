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
    void applyRect (juce::Rectangle<int> logical, bool movable);

    std::function<void()> onMoved;     // fired continuously while the body is dragged (live UI readout)
    std::function<void()> onDragEnd;   // fired once on mouse-up after a drag (emit OSC position feedback)

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    juce::ComponentDragger dragger;
    bool draggable = false;
    bool dragged   = false;   // did the current gesture actually move the window?

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWindow)
};
}
