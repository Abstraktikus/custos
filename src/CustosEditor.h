#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace custos
{
class CustosProcessor;

// Minimal plugin editor: product name + "Synth: <name>" status + a Show/Hide toggle button that
// opens/closes the inner synth's own GUI in the processor-owned floating window. Deliberately tiny
// so the host shows this instead of its generic 5000-slider parameter view.
class CustosEditor : public juce::AudioProcessorEditor
{
public:
    explicit CustosEditor (CustosProcessor&);
    ~CustosEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // Sync status text + button label from processor state. Called on button clicks, and by the
    // processor when the synth window is closed externally (its own title-bar close box).
    void refresh();

private:
    CustosProcessor& proc;
    juce::Label      titleLabel;
    juce::Label      statusLabel;
    juce::TextButton synthButton { "Show Synth" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosEditor)
};
}
