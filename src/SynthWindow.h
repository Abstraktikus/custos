#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace custos
{
// A Custos-owned top-level window that hosts a hosted plugin's editor component (takes ownership
// of it via setContentOwned). Closing it (title bar) invokes onClose asynchronously; the owner
// (CustosProcessor) uses that to destroy this window. The hosted synth itself is unaffected.
class SynthWindow : public juce::DocumentWindow
{
public:
    SynthWindow (const juce::String& title, juce::Component* editor, std::function<void()> onClose);

    void closeButtonPressed() override;

private:
    std::function<void()> onCloseCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWindow)
};
}
