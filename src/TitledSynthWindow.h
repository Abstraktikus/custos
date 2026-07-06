#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace custos
{
// A normal titled window (native title bar + close button) hosting the inner synth's editor.
// Counterpart to the borderless SynthWindow: "Open" uses this, "Open fixed" uses SynthWindow.
class TitledSynthWindow : public juce::DocumentWindow
{
public:
    TitledSynthWindow (const juce::String& name, juce::Component* editor)
        : juce::DocumentWindow (name.isNotEmpty() ? name : juce::String ("Custos Synth"),
                                juce::Colour (0xff2b2b2b), juce::DocumentWindow::closeButton, true)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);   // window tracks the editor's native size
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    std::function<void()> onCloseButton;   // native title-bar close -> owner tears the window down
    void closeButtonPressed() override { if (onCloseButton) onCloseButton(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitledSynthWindow)
};
}
