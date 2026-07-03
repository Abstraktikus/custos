#include "SynthWindow.h"

namespace custos
{
SynthWindow::SynthWindow (const juce::String& title, juce::Component* editor, std::function<void()> onClose)
    : juce::DocumentWindow (title, juce::Colour (0xff2b2b2b), juce::DocumentWindow::closeButton),
      onCloseCallback (std::move (onClose))
{
    jassert (editor != nullptr);             // the sole caller only constructs us with a real editor
    setUsingNativeTitleBar (true);
    setContentOwned (editor, true);          // takes ownership; window tracks the editor's size,
                                             // so a synth that resizes its own GUI resizes this window
    setResizable (false, false);             // no user drag-resize; content drives the size
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void SynthWindow::closeButtonPressed()
{
    // Do not delete ourselves inside our own callback; defer to the owner.
    if (onCloseCallback)
        juce::MessageManager::callAsync (onCloseCallback);
}
}
