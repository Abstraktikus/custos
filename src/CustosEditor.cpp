#include "CustosEditor.h"
#include "CustosProcessor.h"
#include "MidiRouteMatrix.h"
#include "InstrumentBrowser.h"
#include "HostTrace.h"
#include <cmath>

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
    favPicker.setTextWhenNothingSelected (juce::String());
    favPicker.onChange = [this]
    {
        const int i = favPicker.getSelectedItemIndex();
        if (i >= 0 && i < (int) filtered.size()) proc.load (filtered[(size_t) i].path);
    };
    addAndMakeVisible (favPicker);
    // Single Open/Close. "fixed" (below) chooses the window kind: unchecked = titled; checked = borderless
    // placed at the x/y/w/h rect with movable/clamp. One window at a time; Close tears down whichever is open.
    openButton.onClick = [this]
    {
        if (proc.isSynthWindowVisible())     proc.hideSynthWindow();
        else if (fixedToggle.getToggleState()) proc.showSynthWindowBorderless (testMovable.getToggleState());
        else                                 proc.showSynthWindowTitled();
        refresh();
    };
    addAndMakeVisible (openButton);

    // Preset name field + Save button + preset picker (select-to-load).
    addAndMakeVisible (presetNameField);
    presetNameField.setTextToShowWhenEmpty ("preset name", juce::Colours::grey);
    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this]
    {
        const auto name = presetNameField.getText().trim();
        if (name.isNotEmpty()) { proc.savePreset (name); presetNameField.clear(); rebuildPresetList(); }
    };
    addAndMakeVisible (presetPicker);
    presetPicker.onChange = [this]
    {
        const int i = presetPicker.getSelectedItemIndex();
        if (i >= 0) proc.loadPresetAt (i);
    };

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
    scaleDown.onClick = [this] { scaleWindow (1.0 / 1.1); };
    scaleUp.onClick   = [this] { scaleWindow (1.1); };
    addAndMakeVisible (scaleDown);
    addAndMakeVisible (scaleUp);
    addAndMakeVisible (fixedToggle);

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

    // Local audio-fold toggle: sum all inner outputs onto stereo Out 1 (no OSC).
    mainLR.onClick = [this] { proc.setMainLROnly (mainLR.getToggleState()); };
    addAndMakeVisible (mainLR);

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

    // Hidden runtime host-trace toggle (revealed together with the id field).
    traceToggle.onClick = [this] { custos::setTraceEnabled (traceToggle.getToggleState()); };
    addAndMakeVisible (traceToggle);

    // MIDI route matrix (local test convenience; drives proc.setMidiRoute). M = mute (route 0).
    midiLabel.setText ("MIDI ch -> out", juce::dontSendNotification);
    addAndMakeVisible (midiLabel);
    for (int ch = 0; ch < 16; ++ch)
    {
        routeChanLabel[(size_t) ch].setText (juce::String (ch + 1), juce::dontSendNotification);
        routeChanLabel[(size_t) ch].setJustificationType (juce::Justification::centred);
        routeChanLabel[(size_t) ch].setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (routeChanLabel[(size_t) ch]);

        auto& b = routeBox[(size_t) ch];
        b.addItem ("M", 1);                                        // id 1 = mute (route 0)
        for (int out = 1; out <= 16; ++out) b.addItem (juce::String (out), out + 1);  // ids 2..17
        b.onChange = [this] { gatherRouteFromBoxes(); };
        addAndMakeVisible (b);
    }

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
    rebuildPresetList();

    // Reflect the current route map into the selectors (identity by default, or whatever KM/state set).
    const auto routeIds = itemIdsFromRoute (proc.getMidiRoute());
    for (int i = 0; i < 16; ++i)
        routeBox[(size_t) i].setSelectedId (routeIds[(size_t) i], juce::dontSendNotification);

    mainLR.setToggleState (proc.mainLROnly(), juce::dontSendNotification);

    // Identity visibility + adaptive window height.
    const bool showId = idVisible();
    idLabel.setVisible (showId);
    idField.setVisible (showId);
    traceToggle.setVisible (showId);   // hidden feature, revealed with the id field
    traceToggle.setToggleState (custos::isTraceEnabled(), juce::dontSendNotification);
    const int targetH = (showId ? 240 : 208) + 104 + 28 + 64;   // + MIDI matrix section + Main-L/R toggle row + preset rows
    if (getHeight() != targetH) setSize (360, targetH);
}

void CustosEditor::scaleWindow (double factor)
{
    const auto r = proc.currentSynthWindowPhysical();
    if (r.isEmpty()) return;   // no window open
    const int nw = juce::jmax (60, (int) std::lround (r.getWidth()  * factor));
    const int nh = juce::jmax (60, (int) std::lround (r.getHeight() * factor));
    proc.setSynthWindowRect (r.getX(), r.getY(), nw, nh,
                             testMovable.getToggleState(), testClamp.getToggleState());
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
    const int cap = proc.facadeSize();
    for (const auto& f : proc.getFavorites())
        if ((brand.isEmpty() || f.brand == brand) && favouriteFits (f.slots, cap))   // hide synths too big for this facade
            filtered.push_back (f);

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
                                                                    : juce::String());
    }
}

void CustosEditor::rebuildPresetList()
{
    presetPicker.clear (juce::dontSendNotification);   // clear() alone does not fire onChange
    int id = 1;
    for (const auto& n : proc.listPresets()) presetPicker.addItem (n, id++);
}

void CustosEditor::gatherRouteFromBoxes()
{
    std::array<int, 16> ids {};
    for (int i = 0; i < 16; ++i) ids[(size_t) i] = routeBox[(size_t) i].getSelectedId();
    proc.setMidiRoute (routeFromItemIds (ids));
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

    // Preset row 1: name field (fills) + Save button (right).
    auto presetNameRow = r.removeFromTop (24);
    savePresetButton.setBounds (presetNameRow.removeFromRight (84));
    presetNameRow.removeFromRight (8);
    presetNameField.setBounds (presetNameRow);
    r.removeFromTop (8);

    // Preset row 2: preset picker (select-to-load), fills the row.
    auto presetPickRow = r.removeFromTop (24);
    presetPicker.setBounds (presetPickRow);
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

    // MIDI route matrix: header + two rows of 8 (input ch caption over an output selector).
    auto midiHdr = r.removeFromTop (20);
    midiLabel.setBounds (midiHdr.removeFromLeft (140));
    r.removeFromTop (4);
    for (int row = 0; row < 2; ++row)
    {
        auto capRow = r.removeFromTop (14);
        auto boxRow = r.removeFromTop (22);
        const int colW = capRow.getWidth() / 8;
        for (int col = 0; col < 8; ++col)
        {
            const int ch = row * 8 + col;
            routeChanLabel[(size_t) ch].setBounds (capRow.removeFromLeft (colW).reduced (1, 0));
            routeBox[(size_t) ch].setBounds       (boxRow.removeFromLeft (colW).reduced (1, 0));
        }
        r.removeFromTop (4);
    }

    mainLR.setBounds (r.removeFromTop (22).removeFromLeft (160));
    r.removeFromTop (6);

    // Test row 1: physical rect fields + Open fixed.
    auto rectRow = r.removeFromTop (24);
    testX.setBounds (rectRow.removeFromLeft (44)); rectRow.removeFromLeft (4);
    testY.setBounds (rectRow.removeFromLeft (44)); rectRow.removeFromLeft (4);
    testW.setBounds (rectRow.removeFromLeft (44)); rectRow.removeFromLeft (4);
    testH.setBounds (rectRow.removeFromLeft (44));
    r.removeFromTop (6);

    // Test row 2: fixed / movable / clamp toggles + proportional scale.
    auto optRow = r.removeFromTop (24);
    fixedToggle.setBounds (optRow.removeFromLeft (58));
    testMovable.setBounds (optRow.removeFromLeft (78));
    testClamp.setBounds   (optRow.removeFromLeft (64));
    scaleUp.setBounds   (optRow.removeFromRight (30));
    optRow.removeFromRight (4);
    scaleDown.setBounds (optRow.removeFromRight (30));
    r.removeFromTop (8);

    auto idRow = r.removeFromTop (24);
    idLabel.setBounds (idRow.removeFromLeft (30));
    idField.setBounds (idRow.removeFromLeft (48));
    traceToggle.setBounds (idRow.removeFromRight (80));   // hidden trace on/off, next to the id field
}

void CustosEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}
}
