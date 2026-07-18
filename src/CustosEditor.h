#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "FavoritesStore.h"
#include "CustosLookAndFeel.h"
#include <array>
#include <vector>
#include <functional>

namespace custos
{
class CustosProcessor;

// A juce::Label that reports double-clicks (the Instrument label uses it to toggle the synth window).
struct ClickableLabel : juce::Label
{
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
};

// Flat dark-mode plugin editor in four thematic sections (matching the Kapellmeister app):
// (1) Routing & Presets — Brand / Instrument+Open / Preset save-load-delete; (2) Audio Settings —
// Volume + On-top; (3) MIDI Channels -> Out — the 16-channel route matrix; (4) Advanced & Debug
// footer — Main L/R, window rect + Id, fixed/movable/clamp + Trace, and the proportional scale.
// Intercepts the host's generic 5000-slider view.
class CustosEditor : public juce::AudioProcessorEditor
{
public:
    explicit CustosEditor (CustosProcessor&);
    ~CustosEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    void updateRectReadout();   // update only the x/y/w/h fields from the live window rect (cheap; drag-safe)

    // Test seams for the preset-name field (prefill-on-load behavior).
    juce::String presetNameText() const { return presetNameField.getText(); }
    void setPresetNameText (const juce::String& s) { presetNameField.setText (s, false); }

private:
    void commitIdentity();
    bool footerVisible() const;   // Id+Trace shown when identity unset or explicitly revealed
    void rebuildInstrumentList();
    void rebuildPresetList();                               // fill presetPicker from proc.listPresets()
    void scaleWindow (double factor);   // scale the synth window's current rect proportionally (keeps top-left)
    void gatherRouteFromBoxes();                            // read the 16 boxes -> proc.setMidiRoute

    CustosProcessor&   proc;
    CustosLookAndFeel  lnf;   // flat dark theme; declared early so it outlives the child components

    // ── Instrument ──────────────────────────────────────────────
    SectionHeader    instrumentHeader { SectionHeader::Icon::Instrument, "Instrument" };
    ClickableLabel   brandLabel;   // "Brand" — double-click reveals the hidden Id+Trace footer
    juce::ComboBox   brandFilter;
    ClickableLabel   instrLabel;   // "Instrument" — double-click toggles the synth window (hidden feature)
    juce::ComboBox   favPicker;
    juce::TextButton openButton { "Open" };

    // ── Presets ─────────────────────────────────────────────────
    SectionHeader    presetHeader { SectionHeader::Icon::Presets, "Presets" };
    juce::Label      presetLabel;                           // "Preset"
    juce::ComboBox   presetPicker;                          // saved presets (select-to-load)
    juce::TextButton deletePresetButton { "Delete" };       // delete the picker's selected preset (immediate)
    juce::Label      newPresetLabel;                        // "New"
    juce::TextEditor presetNameField;                       // name for the next Save
    juce::TextButton savePresetButton { "Save" };

    // ── Audio ───────────────────────────────────────────────────
    SectionHeader    audioHeader { SectionHeader::Icon::Audio, "Audio" };
    juce::Label      volumeLabel;  // "Volume"
    juce::Slider     volumeFader { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label      dbLabel;      // "-3.0 dB"
    juce::ToggleButton mainLR { "Main L/R only" };   // local audio-fold toggle (drives proc.setMainLROnly)

    // ── Display Options (synth window) ──────────────────────────
    SectionHeader    displayHeader { SectionHeader::Icon::Display, "Display Options" };
    juce::Label      onTopLabel;   // "On top"
    juce::ComboBox   onTopBox;     // off / Custos / Instrument
    juce::TextEditor testX, testY, testW, testH;   // physical rect for the "Open fixed" window
    juce::ToggleButton testMovable { "movable" };
    juce::ToggleButton testClamp   { "clamp" };      // constrain to monitor work area
    juce::ToggleButton fixedToggle { "fixed" };      // true = Open opens the borderless/fixed window
    juce::TextButton   scaleDown { "-" }, scaleUp { "+" };   // proportional resize of the synth window

    // ── MIDI Channels -> Out ────────────────────────────────────
    SectionHeader    midiHeader { SectionHeader::Icon::Midi,
                                  juce::String::fromUTF8 ("MIDI Channels \xE2\x86\x92 Out") };
    std::array<juce::Label, 16>    routeChanLabel;  // input channel captions 1..16 (no boxes)
    std::array<juce::ComboBox, 16> routeBox;        // per input channel: M / 1..16 (output)

    // ── Footer (Id + Trace only) ────────────────────────────────
    juce::Label        idLabel;      // "Id"
    juce::TextEditor   idField;      // N (1..15)
    juce::ToggleButton traceToggle { "Trace" };      // runtime host-trace on/off (dimmed debug feature)

    std::vector<Favorite> filtered;
    int  lastPresetNameRev = -1;   // last processor presetNameRevision synced into presetNameField
    bool revealed = false;   // footer (Id+Trace) reveal toggle (double-click Brand)
    int  dividerY = -1;      // y of the hairline before the footer (set in resized, drawn in paint; -1 = hidden)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosEditor)
};
}
