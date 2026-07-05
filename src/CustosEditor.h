#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "FavoritesStore.h"
#include <array>
#include <vector>
#include <functional>

namespace custos
{
class CustosProcessor;

// A juce::Label that reports double-clicks (used as a hidden reveal for the identity field).
struct ClickableLabel : juce::Label
{
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
};

// Compact plugin editor: Brand filter, Instrument picker (favourites, reflecting the loaded synth) +
// Open, master Volume fader, keep-on-top selector, and an identity field that hides once set (revealed
// again by double-clicking the Brand label). Intercepts the host's generic 5000-slider view.
class CustosEditor : public juce::AudioProcessorEditor
{
public:
    explicit CustosEditor (CustosProcessor&);
    ~CustosEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    void updateRectReadout();   // update only the x/y/w/h fields from the live window rect (cheap; drag-safe)

private:
    void commitIdentity();
    void rebuildInstrumentList();
    bool idVisible() const;
    void scaleWindow (double factor);   // scale the synth window's current rect proportionally (keeps top-left)

    CustosProcessor& proc;

    ClickableLabel   brandLabel;   // "Brand" — double-click reveals the identity field
    juce::ComboBox   brandFilter;

    ClickableLabel   instrLabel;   // "Instrument" — double-click closes the synth window (hidden feature)
    juce::ComboBox   favPicker;
    juce::TextButton openButton { "Open" };

    juce::TextEditor testX, testY, testW, testH;   // physical rect for the "Open fixed" test
    juce::ToggleButton testMovable { "movable" };
    juce::ToggleButton testClamp   { "clamp" };    // constrain to monitor work area (config phase)
    juce::TextButton   openFixedButton { "Open fixed" };
    juce::TextButton   scaleDown { "-" }, scaleUp { "+" };   // proportional resize of the synth window

    juce::Label      volumeLabel;  // "Volume"
    juce::Slider     volumeFader { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label      dbLabel;      // "-3.0 dB"

    juce::Label      onTopLabel;   // "On top"
    juce::ComboBox   onTopBox;     // off / This / Instrument

    juce::Label      idLabel;      // "Id"
    juce::TextEditor idField;      // N (1..15); hidden once set

    juce::Label      midiLabel;                     // "MIDI ch -> out"
    std::array<juce::Label, 16>    routeChanLabel;  // input channel captions 1..16
    std::array<juce::ComboBox, 16> routeBox;        // per input channel: M / 1..16 (output)
    void gatherRouteFromBoxes();                    // read the 16 boxes -> proc.setMidiRoute

    bool idRevealed = false;
    std::vector<Favorite> filtered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosEditor)
};
}
