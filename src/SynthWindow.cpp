#include "SynthWindow.h"

namespace custos
{
SynthWindow::SynthWindow (const juce::String& title, juce::Component* editor, std::function<void()> onClose)
    : juce::DocumentWindow (title, juce::Colour (0xff2b2b2b), juce::DocumentWindow::closeButton),
      onCloseCallback (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    setContentOwned (editor, true);          // takes ownership; sizes the window to the editor
    setResizable (false, false);             // synth editors drive their own size; window follows
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
