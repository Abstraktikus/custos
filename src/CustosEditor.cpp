#include "CustosEditor.h"
#include "CustosProcessor.h"

namespace custos
{
CustosEditor::CustosEditor (CustosProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // Brand filter — double-clicking the label reveals the identity field.
    brandLabel.setText ("Brand", juce::dontSendNotification);
    brandLabel.setInterceptsMouseClicks (true, false);
    brandLabel.onDoubleClick = [this] { idRevealed = ! idRevealed; refresh(); };
    addAndMakeVisible (brandLabel);
    brandFilter.onChange = [this] { rebuildInstrumentList(); };   // re-filter only; never loads
    addAndMakeVisible (brandFilter);

    // Instrument picker + open. Double-clicking the label toggles the synth window (hidden feature).
    instrLabel.setText ("Instrument", juce::dontSendNotification);
    instrLabel.setInterceptsMouseClicks (true, false);
    instrLabel.onDoubleClick = [this] { proc.toggleSynthWindow(); refresh(); };
    addAndMakeVisible (instrLabel);
    favPicker.setTextWhenNothingSelected ("Instrument…");
    favPicker.onChange = [this]
    {
        const int i = favPicker.getSelectedItemIndex();
        if (i >= 0 && i < (int) filtered.size()) proc.load (filtered[(size_t) i].path);
    };
    addAndMakeVisible (favPicker);
    openButton.onClick = [this] { proc.toggleSynthWindow(); refresh(); };
    addAndMakeVisible (openButton);

    // Test controls (dev-only): a physical rect + movable, opened borderless via "Open fixed".
    auto setupNum = [this] (juce::TextEditor& t, const juce::String& placeholder)
    {
        t.setInputRestrictions (5, "0123456789");
        t.setTextToShowWhenEmpty (placeholder, juce::Colours::grey);
        addAndMakeVisible (t);
    };
    setupNum (testX, "x"); setupNum (testY, "y"); setupNum (testW, "w"); setupNum (testH, "h");
    addAndMakeVisible (testMovable);
    addAndMakeVisible (testClamp);
    openFixedButton.onClick = [this]
    {
        proc.setSynthWindowRect (testX.getText().getIntValue(), testY.getText().getIntValue(),
                                 testW.getText().getIntValue(), testH.getText().getIntValue(),
                                 testMovable.getToggleState(), testClamp.getToggleState());
        refresh();
    };
    addAndMakeVisible (openFixedButton);

    // Master volume: label + horizontal fader + dB readout.
    volumeLabel.setText ("Volume", juce::dontSendNotification);
    addAndMakeVisible (volumeLabel);
    volumeFader.setRange (-60.0, 12.0, 0.1);
    volumeFader.onValueChange = [this]
    {
        proc.setVolumeDb ((float) volumeFader.getValue());
        dbLabel.setText (juce::String (volumeFader.getValue(), 1) + " dB", juce::dontSendNotification);
    };
    addAndMakeVisible (volumeFader);
    dbLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (dbLabel);

    // Keep-on-top selector.
    onTopLabel.setText ("On top", juce::dontSendNotification);
    addAndMakeVisible (onTopLabel);
    onTopBox.addItem ("off", 1);
    onTopBox.addItem ("Custos", 2);
    onTopBox.addItem ("Instrument", 3);
    onTopBox.onChange = [this] { proc.setOnTopMode ((OnTopMode) onTopBox.getSelectedItemIndex()); };
    addAndMakeVisible (onTopBox);

    // Identity (bottom-left; hidden once set).
    idLabel.setText ("Id", juce::dontSendNotification);
    addAndMakeVisible (idLabel);
    idField.setInputRestrictions (2, "0123456789");
    idField.setJustification (juce::Justification::centred);
    idField.onTextChange = [this] { commitIdentity(); };
    idField.onReturnKey  = [this] { commitIdentity(); };
    addAndMakeVisible (idField);
    { const int n0 = proc.identity();
      idField.setText ((n0 >= 1 && n0 <= 15) ? juce::String (n0) : juce::String(), juce::dontSendNotification); }

    refresh();
}

CustosEditor::~CustosEditor() = default;

bool CustosEditor::idVisible() const
{
    const int n = proc.identity();
    return (n < 1 || n > 15) || idRevealed;
}

void CustosEditor::refresh()
{
    openButton.setButtonText (proc.isSynthWindowVisible() ? "Close" : "Open");
    openButton.setEnabled (proc.hasInnerSynth());
    onTopBox.setSelectedItemIndex ((int) proc.getOnTopMode(), juce::dontSendNotification);
    volumeFader.setValue (proc.volumeDb(), juce::dontSendNotification);
    dbLabel.setText (juce::String (proc.volumeDb(), 1) + " dB", juce::dontSendNotification);

    updateRectReadout();   // reflect the window's physical rect (incl. OSC-set / dragged) into the test fields

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

    // Identity visibility + adaptive window height.
    const bool showId = idVisible();
    idLabel.setVisible (showId);
    idField.setVisible (showId);
    const int targetH = showId ? 210 : 178;
    if (getHeight() != targetH) setSize (360, targetH);
}

void CustosEditor::updateRectReadout()
{
    if (! proc.isSynthWindowVisible()) return;
    const auto rp = proc.currentSynthWindowPhysical();
    testX.setText (juce::String (rp.getX()),      juce::dontSendNotification);
    testY.setText (juce::String (rp.getY()),      juce::dontSendNotification);
    testW.setText (juce::String (rp.getWidth()),  juce::dontSendNotification);
    testH.setText (juce::String (rp.getHeight()), juce::dontSendNotification);
}

void CustosEditor::rebuildInstrumentList()
{
    const juce::String brand = (brandFilter.getSelectedId() > 1) ? brandFilter.getText() : juce::String();
    filtered.clear();
    for (const auto& f : proc.getFavorites())
        if (brand.isEmpty() || f.brand == brand) filtered.push_back (f);

    favPicker.clear (juce::dontSendNotification);   // clear() alone does not fire onChange
    int id = 1;
    for (const auto& f : filtered) favPicker.addItem (f.name, id++);

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
    {
        proc.setIdentity (n);
        idRevealed = false;   // hide once set
    }
    refresh();
}

void CustosEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto brandRow = r.removeFromTop (24);
    brandLabel.setBounds  (brandRow.removeFromLeft (84));
    brandFilter.setBounds (brandRow.removeFromLeft (170));
    r.removeFromTop (8);

    auto instrRow = r.removeFromTop (26);
    instrLabel.setBounds (instrRow.removeFromLeft (84));
    openButton.setBounds (instrRow.removeFromRight (60));
    instrRow.removeFromRight (8);
    favPicker.setBounds  (instrRow);
    r.removeFromTop (8);

    auto volRow = r.removeFromTop (24);
    volumeLabel.setBounds (volRow.removeFromLeft (84));
    dbLabel.setBounds     (volRow.removeFromRight (56));
    volRow.removeFromRight (8);
    volumeFader.setBounds (volRow);
    r.removeFromTop (8);

    auto onTopRow = r.removeFromTop (24);
    onTopLabel.setBounds (onTopRow.removeFromLeft (84));
    onTopBox.setBounds   (onTopRow.removeFromLeft (130));
    r.removeFromTop (8);

    auto testRow = r.removeFromTop (24);
    testX.setBounds (testRow.removeFromLeft (40)); testRow.removeFromLeft (3);
    testY.setBounds (testRow.removeFromLeft (40)); testRow.removeFromLeft (3);
    testW.setBounds (testRow.removeFromLeft (40)); testRow.removeFromLeft (3);
    testH.setBounds (testRow.removeFromLeft (40)); testRow.removeFromLeft (6);
    openFixedButton.setBounds (testRow.removeFromRight (78));
    testRow.removeFromRight (6);
    testMovable.setBounds (testRow.removeFromLeft (72));
    testClamp.setBounds   (testRow.removeFromLeft (64));
    r.removeFromTop (8);

    auto idRow = r.removeFromTop (24);
    idLabel.setBounds (idRow.removeFromLeft (30));
    idField.setBounds (idRow.removeFromLeft (48));
}

void CustosEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}
}
