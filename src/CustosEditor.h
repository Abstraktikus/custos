#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "FavoritesStore.h"
#include <vector>

namespace custos
{
class CustosProcessor;

// Compact plugin editor: a top-right identity field, a Brand filter, an Instrument picker (favourites,
// filtered by brand, reflecting the loaded synth) + Open button, and a keep-on-top toggle. Intercepts
// the host's generic 5000-slider view.
class CustosEditor : public juce::AudioProcessorEditor
{
public:
    explicit CustosEditor (CustosProcessor&);
    ~CustosEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // Sync all controls from processor state (favourites, loaded synth, identity, window state).
    void refresh();

private:
    void commitIdentity();
    void rebuildInstrumentList();   // filter favourites by the selected brand -> favPicker (+ reflect loaded synth)

    CustosProcessor& proc;

    juce::Label      idLabel;      // "Id"
    juce::TextEditor idField;      // N (1..15)
    juce::Label      idStatus;     // ":<port>" / collision / unassigned

    juce::Label      brandLabel;   // "Brand"
    juce::ComboBox   brandFilter;  // "All" + distinct brands

    juce::Label      instrLabel;   // "Instrument"
    juce::ComboBox   favPicker;    // favourites (filtered by brand); shows the loaded synth
    juce::TextButton openButton { "Open" };          // open/close the synth window

    juce::ToggleButton onTopToggle { "Keep window on top" };

    std::vector<Favorite> filtered;   // favourites currently shown (after the brand filter)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosEditor)
};
}
