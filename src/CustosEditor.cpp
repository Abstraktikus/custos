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
    setLookAndFeel (&lnf);

    // ---- section headers + the preset-row labels Martin asked for ----
    addAndMakeVisible (instrumentHeader);
    addAndMakeVisible (presetHeader);
    addAndMakeVisible (displayHeader);
    presetLabel.setText ("Preset", juce::dontSendNotification);
    addAndMakeVisible (presetLabel);
    newPresetLabel.setText ("New", juce::dontSendNotification);
    addAndMakeVisible (newPresetLabel);

    brandLabel.setText ("Brand", juce::dontSendNotification);
    brandLabel.setInterceptsMouseClicks (true, false);
    brandLabel.onDoubleClick = [this] { revealed = ! revealed; refresh(); };   // reveal the hidden Id+Trace footer
    addAndMakeVisible (brandLabel);
    brandFilter.onChange = [this] { rebuildInstrumentList(); };   // re-filter only; never loads
    addAndMakeVisible (brandFilter);

    instrLabel.setText ("Instrument", juce::dontSendNotification);
    instrLabel.setInterceptsMouseClicks (true, false);
    instrLabel.onDoubleClick = [this] { proc.toggleSynthWindow(); refresh(); };   // hidden window toggle
    addAndMakeVisible (instrLabel);
    favPicker.setTextWhenNothingSelected (juce::String());
    favPicker.onChange = [this]
    {
        const int i = favPicker.getSelectedItemIndex();
        if (i >= 0 && i < (int) filtered.size()) proc.load (filtered[(size_t) i].path);
    };
    addAndMakeVisible (favPicker);
    // Single Open/Close. "fixed" (footer) chooses the window kind: unchecked = titled; checked = borderless.
    openButton.onClick = [this]
    {
        if (proc.isSynthWindowVisible())       proc.hideSynthWindow();
        else if (fixedToggle.getToggleState()) proc.showSynthWindowBorderless (testMovable.getToggleState());
        else                                   proc.showSynthWindowTitled();
        refresh();
    };
    addAndMakeVisible (openButton);

    presetPicker.setTextWhenNothingSelected (juce::String());
    presetPicker.onChange = [this]
    {
        const int i = presetPicker.getSelectedItemIndex();
        if (i >= 0) proc.loadPresetAt (i);
    };
    addAndMakeVisible (presetPicker);
    presetNameField.setTextToShowWhenEmpty ("preset name", theme::muted);
    addAndMakeVisible (presetNameField);
    savePresetButton.onClick = [this]
    {
        // The name stays in the field after saving (prefilled = one-click update of the loaded preset);
        // the processor's refreshEditor() covers the list rebuild + field sync.
        const auto name = presetNameField.getText().trim();
        if (name.isNotEmpty()) proc.savePreset (name);
    };
    addAndMakeVisible (savePresetButton);
    deletePresetButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.85f));
    deletePresetButton.onClick = [this]
    {
        const auto name = presetPicker.getText();   // the selected preset's name ("" if none selected)
        if (name.isNotEmpty()) proc.deletePreset (name);   // its refreshEditor() rebuilds the list
    };
    addAndMakeVisible (deletePresetButton);

    // ---- Section 2: Audio Settings ---------------------------------------
    addAndMakeVisible (audioHeader);

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

    onTopLabel.setText ("On top", juce::dontSendNotification);
    addAndMakeVisible (onTopLabel);
    onTopBox.addItem ("off", 1);
    onTopBox.addItem ("Custos", 2);
    onTopBox.addItem ("Instrument", 3);
    onTopBox.onChange = [this] { proc.setOnTopMode ((OnTopMode) onTopBox.getSelectedItemIndex()); };
    addAndMakeVisible (onTopBox);

    // ---- Section 3: MIDI Channels -> Out ---------------------------------
    addAndMakeVisible (midiHeader);
    for (int ch = 0; ch < 16; ++ch)
    {
        auto& cap = routeChanLabel[(size_t) ch];
        cap.setText (juce::String (ch + 1), juce::dontSendNotification);
        cap.setJustificationType (juce::Justification::centred);
        cap.setColour (juce::Label::textColourId, theme::muted);
        cap.setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (cap);

        auto& b = routeBox[(size_t) ch];
        b.addItem ("M", 1);                                                            // id 1 = mute (route 0)
        for (int out = 1; out <= 16; ++out) b.addItem (juce::String (out), out + 1);   // ids 2..17
        b.onChange = [this] { gatherRouteFromBoxes(); };
        addAndMakeVisible (b);
    }

    // ---- Section 4: Advanced & Debug footer ------------------------------
    mainLR.onClick = [this] { proc.setMainLROnly (mainLR.getToggleState()); };
    addAndMakeVisible (mainLR);

    auto setupNum = [this] (juce::TextEditor& t, const juce::String& placeholder)
    {
        t.setInputRestrictions (5, "0123456789");
        t.setTextToShowWhenEmpty (placeholder, theme::muted);
        t.setJustification (juce::Justification::centred);
        addAndMakeVisible (t);
    };
    setupNum (testX, "x"); setupNum (testY, "y"); setupNum (testW, "w"); setupNum (testH, "h");

    idLabel.setText ("Id", juce::dontSendNotification);
    idLabel.setColour (juce::Label::textColourId, theme::muted);
    addAndMakeVisible (idLabel);
    idField.setInputRestrictions (2, "0123456789");
    idField.setJustification (juce::Justification::centred);
    idField.onTextChange = [this] { commitIdentity(); };
    idField.onReturnKey  = [this] { commitIdentity(); };
    addAndMakeVisible (idField);
    { const int n0 = proc.identity();
      idField.setText ((n0 >= 1 && n0 <= 15) ? juce::String (n0) : juce::String(), juce::dontSendNotification); }

    addAndMakeVisible (fixedToggle);
    addAndMakeVisible (testMovable);
    addAndMakeVisible (testClamp);

    traceToggle.setColour (juce::ToggleButton::textColourId, theme::muted);   // dimmed debug feature
    traceToggle.onClick = [this]
    {
        custos::setTraceEnabled (traceToggle.getToggleState());
        traceToggle.setColour (juce::ToggleButton::textColourId,
                               traceToggle.getToggleState() ? theme::accent : theme::muted);   // glows when active
        traceToggle.repaint();
    };
    addAndMakeVisible (traceToggle);

    scaleDown.onClick = [this] { scaleWindow (1.0 / 1.1); };
    scaleUp.onClick   = [this] { scaleWindow (1.1); };
    addAndMakeVisible (scaleDown);
    addAndMakeVisible (scaleUp);

    setSize (380, 660);
    refresh();
}

CustosEditor::~CustosEditor() { setLookAndFeel (nullptr); }

void CustosEditor::refresh()
{
    openButton.setButtonText (proc.isSynthWindowVisible() ? "Close" : "Open");
    openButton.setEnabled (proc.hasInnerSynth());
    onTopBox.setSelectedItemIndex ((int) proc.getOnTopMode(), juce::dontSendNotification);
    volumeFader.setValue (proc.volumeDb(), juce::dontSendNotification);
    dbLabel.setText (juce::String (proc.volumeDb(), 1) + " dB", juce::dontSendNotification);

    updateRectReadout();   // reflect the window's physical rect (incl. OSC-set / dragged) into the fields

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

    // Prefill the name field with the loaded/saved preset so Save = one-click update. Sync only when
    // the processor's revision moved (a load/save/rename/delete/swap EVENT — incl. a same-name reload),
    // so a half-typed save-as-new name is never clobbered by unrelated refreshes.
    if (const int rev = proc.presetNameRevision(); rev != lastPresetNameRev)
    {
        presetNameField.setText (proc.loadedPresetName(), false);
        lastPresetNameRev = rev;
    }

    // Reflect the current route map into the selectors (identity by default, or whatever KM/state set).
    const auto routeIds = itemIdsFromRoute (proc.getMidiRoute());
    for (int i = 0; i < 16; ++i)
        routeBox[(size_t) i].setSelectedId (routeIds[(size_t) i], juce::dontSendNotification);

    mainLR.setToggleState (proc.mainLROnly(), juce::dontSendNotification);

    const bool traceOn = custos::isTraceEnabled();
    traceToggle.setToggleState (traceOn, juce::dontSendNotification);
    traceToggle.setColour (juce::ToggleButton::textColourId, traceOn ? theme::accent : theme::muted);

    // Footer (Id + Trace) is a hidden debug row: shown only when N is unset or explicitly revealed.
    // The editor shrinks when it is hidden (and paint() drops the divider).
    const bool fv = footerVisible();
    idLabel.setVisible (fv);
    idField.setVisible (fv);
    traceToggle.setVisible (fv);
    const int targetH = fv ? 660 : 604;
    if (getHeight() != targetH) setSize (380, targetH);
}

bool CustosEditor::footerVisible() const
{
    const int n = proc.identity();
    return (n < 1 || n > 15) || revealed;
}

void CustosEditor::scaleWindow (double factor)
{
    const auto r = proc.currentSynthWindowRect();
    if (r.isEmpty()) return;   // no window open
    const int nw = juce::jmax (60, (int) std::lround (r.getWidth()  * factor));
    const int nh = juce::jmax (60, (int) std::lround (r.getHeight() * factor));
    proc.setSynthWindowRect (r.getX(), r.getY(), nw, nh,
                             testMovable.getToggleState(), testClamp.getToggleState());
}

void CustosEditor::updateRectReadout()
{
    if (! proc.isSynthWindowVisible()) return;
    const auto rp = proc.currentSynthWindowRect();
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
    const auto cur = proc.loadedPresetName();
    int id = 1, selId = 0;
    for (const auto& n : proc.listPresets())
    {
        if (n == cur) selId = id;   // keep the picker showing the loaded/saved preset across rebuilds
        presetPicker.addItem (n, id++);
    }
    presetPicker.setSelectedId (selId, juce::dontSendNotification);   // 0 = none
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
    if (n >= 1 && n <= 15) { proc.setIdentity (n); revealed = false; }   // hide the footer once N is set
    refresh();
}

void CustosEditor::resized()
{
    constexpr int pad = 16, gapS = 20, gapR = 6, hdrH = 22, rowH = 26, labW = 70, btnW = 56;
    auto r = getLocalBounds().reduced (pad);

    auto row  = [&r] (int h) { return r.removeFromTop (h); };
    auto skip = [&r] (int h) { r.removeFromTop (h); };

    // ── Instrument ──
    instrumentHeader.setBounds (row (hdrH));  skip (8);
    { auto br = row (rowH); brandLabel.setBounds (br.removeFromLeft (labW)); brandFilter.setBounds (br); }
    skip (gapR);
    { auto ir = row (rowH); instrLabel.setBounds (ir.removeFromLeft (labW));
      openButton.setBounds (ir.removeFromRight (btnW)); ir.removeFromRight (6); favPicker.setBounds (ir); }
    skip (gapS);

    // ── Presets ──  (Delete sits right of the preset picker; Save right of the new-name field)
    presetHeader.setBounds (row (hdrH));  skip (8);
    { auto pr = row (rowH); presetLabel.setBounds (pr.removeFromLeft (labW));
      deletePresetButton.setBounds (pr.removeFromRight (btnW)); pr.removeFromRight (6); presetPicker.setBounds (pr); }
    skip (gapR);
    { auto nr = row (rowH); newPresetLabel.setBounds (nr.removeFromLeft (labW));
      savePresetButton.setBounds (nr.removeFromRight (btnW)); nr.removeFromRight (6); presetNameField.setBounds (nr); }
    skip (gapS);

    // ── Audio ──  (Volume + Main L/R only)
    audioHeader.setBounds (row (hdrH));  skip (8);
    { auto vr = row (rowH); volumeLabel.setBounds (vr.removeFromLeft (labW));
      dbLabel.setBounds (vr.removeFromRight (btnW)); vr.removeFromRight (6); volumeFader.setBounds (vr); }
    skip (gapR);
    { auto mr = row (rowH); mr.removeFromLeft (labW); mainLR.setBounds (mr.removeFromLeft (150)); }
    skip (gapS);

    // ── MIDI Channels -> Out ──
    midiHeader.setBounds (row (hdrH));  skip (8);
    for (int block = 0; block < 2; ++block)
    {
        auto capRow = row (14);
        auto boxRow = row (24);
        const int colW = capRow.getWidth() / 8;
        for (int col = 0; col < 8; ++col)
        {
            const int ch = block * 8 + col;
            routeChanLabel[(size_t) ch].setBounds (capRow.removeFromLeft (colW).reduced (1, 0));
            routeBox[(size_t) ch].setBounds       (boxRow.removeFromLeft (colW).reduced (1, 0));
        }
        skip (4);
    }
    skip (gapS);

    // ── Display Options (synth window) ──  (On top + rect + fixed/movable/clamp + scale)
    displayHeader.setBounds (row (hdrH));  skip (8);
    { auto tr = row (rowH); onTopLabel.setBounds (tr.removeFromLeft (labW)); onTopBox.setBounds (tr.removeFromLeft (140)); }
    skip (gapR);
    { auto cr = row (rowH); cr.removeFromLeft (labW);
      const int cw = (cr.getWidth() - 3 * 4) / 4;
      testX.setBounds (cr.removeFromLeft (cw)); cr.removeFromLeft (4);
      testY.setBounds (cr.removeFromLeft (cw)); cr.removeFromLeft (4);
      testW.setBounds (cr.removeFromLeft (cw)); cr.removeFromLeft (4);
      testH.setBounds (cr.removeFromLeft (cw)); }
    skip (gapR);
    { auto orow = row (rowH);
      scaleUp.setBounds   (orow.removeFromRight (28)); orow.removeFromRight (4);
      scaleDown.setBounds (orow.removeFromRight (28)); orow.removeFromRight (10);
      fixedToggle.setBounds (orow.removeFromLeft (64));
      testMovable.setBounds (orow.removeFromLeft (82));
      testClamp.setBounds   (orow.removeFromLeft (68)); }
    // ── Footer: Id + Trace only ── hidden until revealed; the hairline shows only with the footer.
    if (footerVisible())
    {
        skip (gapS - 4);
        dividerY = r.getY();   // paint() draws the hairline here
        skip (14);
        auto fr = row (rowH);
        idLabel.setBounds (fr.removeFromLeft (22));
        idField.setBounds (fr.removeFromLeft (40));
        traceToggle.setBounds (fr.removeFromRight (80));
    }
    else
    {
        dividerY = -1;   // no footer -> no divider
    }
}

void CustosEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    if (dividerY >= 0)
    {
        g.setColour (theme::divider);
        g.fillRect (16, dividerY, getWidth() - 32, 1);
    }
}
}
