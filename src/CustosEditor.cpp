#include "CustosEditor.h"
#include "CustosProcessor.h"
#include "OscContract.h"

namespace custos
{
CustosEditor::CustosEditor (CustosProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // Identity (top-right).
    idLabel.setText ("Id", juce::dontSendNotification);
    idLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (idLabel);

    idField.setInputRestrictions (2, "0123456789");
    idField.setJustification (juce::Justification::centred);
    // Commit on every edit — a plugin editor can't rely on focus-loss (Enter is host-dependent).
    idField.onTextChange = [this] { commitIdentity(); };
    idField.onReturnKey  = [this] { commitIdentity(); };
    addAndMakeVisible (idField);
    { const int n0 = proc.identity();
      idField.setText ((n0 >= 1 && n0 <= 15) ? juce::String (n0) : juce::String(), juce::dontSendNotification); }

    idStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (idStatus);

    // Brand filter.
    brandLabel.setText ("Brand", juce::dontSendNotification);
    addAndMakeVisible (brandLabel);
    brandFilter.onChange = [this] { rebuildInstrumentList(); };   // re-filter only; never loads
    addAndMakeVisible (brandFilter);

    // Instrument picker + open button.
    instrLabel.setText ("Instrument", juce::dontSendNotification);
    addAndMakeVisible (instrLabel);

    favPicker.setTextWhenNothingSelected ("Instrument…");
    favPicker.onChange = [this]
    {
        const int i = favPicker.getSelectedItemIndex();
        if (i >= 0 && i < (int) filtered.size())
            proc.load (filtered[(size_t) i].path);
    };
    addAndMakeVisible (favPicker);

    openButton.onClick = [this] { proc.toggleSynthWindow(); refresh(); };
    addAndMakeVisible (openButton);

    // Keep-on-top toggle.
    onTopToggle.onClick = [this] { proc.setSynthWindowOnTop (onTopToggle.getToggleState()); };
    addAndMakeVisible (onTopToggle);

    refresh();
    setSize (360, 176);
}

CustosEditor::~CustosEditor() = default;

void CustosEditor::refresh()
{
    // Identity status.
    const int n = proc.identity();
    if (n < 1 || n > 15)
        idStatus.setText ("unassigned", juce::dontSendNotification);
    else if (! proc.identityBound())
        idStatus.setText (":" + juce::String (oscPortForIdentity (n)) + "  N in use", juce::dontSendNotification);
    else
        idStatus.setText (":" + juce::String (oscPortForIdentity (n)), juce::dontSendNotification);

    // Open button + keep-on-top.
    openButton.setButtonText (proc.isSynthWindowVisible() ? "Close" : "Open");
    openButton.setEnabled (proc.hasInnerSynth());
    onTopToggle.setToggleState (proc.isSynthWindowOnTop(), juce::dontSendNotification);

    // Brand filter (preserve the selected brand across rebuilds).
    const juce::String selBrand = (brandFilter.getSelectedId() > 1) ? brandFilter.getText() : juce::String();
    brandFilter.clear (juce::dontSendNotification);
    brandFilter.addItem ("All", 1);
    juce::StringArray brands;
    for (const auto& f : proc.getFavorites())
        if (f.brand.isNotEmpty()) brands.addIfNotAlreadyThere (f.brand);
    brands.sort (true);
    int bid = 2;
    for (const auto& b : brands) brandFilter.addItem (b, bid++);
    int selId = 1;   // "All"
    for (int i = 0; i < brands.size(); ++i) if (brands[i] == selBrand) selId = i + 2;
    brandFilter.setSelectedId (selId, juce::dontSendNotification);

    rebuildInstrumentList();
}

void CustosEditor::rebuildInstrumentList()
{
    const juce::String brand = (brandFilter.getSelectedId() > 1) ? brandFilter.getText() : juce::String();
    filtered.clear();
    for (const auto& f : proc.getFavorites())
        if (brand.isEmpty() || f.brand == brand)
            filtered.push_back (f);

    favPicker.clear (juce::dontSendNotification);   // clear() alone does not fire onChange
    int id = 1;
    for (const auto& f : filtered) favPicker.addItem (f.name, id++);

    // Reflect the loaded synth: select it if it's in the (filtered) list, else show its name.
    const auto path = proc.currentPath();
    int sel = 0;
    for (int i = 0; i < (int) filtered.size(); ++i)
        if (filtered[(size_t) i].path == path) sel = i + 1;

    if (sel > 0)
        favPicker.setSelectedId (sel, juce::dontSendNotification);
    else
    {
        favPicker.setSelectedId (0, juce::dontSendNotification);
        favPicker.setTextWhenNothingSelected (proc.hasInnerSynth() ? proc.innerSynthName()
                                                                    : juce::String ("Instrument…"));
    }
}

void CustosEditor::commitIdentity()
{
    const int n = idField.getText().getIntValue();
    if (n >= 1 && n <= 15)
        proc.setIdentity (n);
    refresh();
}

void CustosEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    // Top row: identity, pushed to the right.
    auto top = r.removeFromTop (22);
    auto idBox = top.removeFromRight (96);
    idField.setBounds (idBox.removeFromRight (40));
    idBox.removeFromRight (4);
    idLabel.setBounds (idBox);
    idStatus.setBounds (top);
    r.removeFromTop (10);

    // Brand row.
    auto brandRow = r.removeFromTop (24);
    brandLabel.setBounds  (brandRow.removeFromLeft (84));
    brandFilter.setBounds (brandRow.removeFromLeft (170));
    r.removeFromTop (8);

    // Instrument row: label + picker + open.
    auto instrRow = r.removeFromTop (26);
    instrLabel.setBounds (instrRow.removeFromLeft (84));
    openButton.setBounds (instrRow.removeFromRight (60));
    instrRow.removeFromRight (8);
    favPicker.setBounds  (instrRow);
    r.removeFromTop (10);

    // Keep-on-top row.
    onTopToggle.setBounds (r.removeFromTop (24));
}

void CustosEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}
}
