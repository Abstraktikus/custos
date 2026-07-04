#include "CustosEditor.h"
#include "CustosProcessor.h"
#include "OscContract.h"

namespace custos
{
CustosEditor::CustosEditor (CustosProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    titleLabel.setText (kProduct, juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    synthButton.onClick = [this]
    {
        proc.toggleSynthWindow();
        refresh();
    };
    addAndMakeVisible (synthButton);

    idLabel.setText ("Id", juce::dontSendNotification);
    idLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (idLabel);

    idField.setInputRestrictions (2, "0123456789");
    idField.setJustification (juce::Justification::centred);
    idField.onReturnKey = [this] { commitIdentity(); };
    idField.onFocusLost = [this] { commitIdentity(); };
    addAndMakeVisible (idField);

    idStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (idStatus);

    refresh();
    setSize (360, 172);
}

CustosEditor::~CustosEditor() = default;

void CustosEditor::refresh()
{
    statusLabel.setText ("Synth: " + proc.innerSynthName(), juce::dontSendNotification);
    synthButton.setButtonText (proc.isSynthWindowVisible() ? "Hide Synth" : "Show Synth");
    synthButton.setEnabled (proc.hasInnerSynth());

    const int n = proc.identity();
    if (n < 1 || n > 15)
        idStatus.setText ("unassigned", juce::dontSendNotification);
    else if (! proc.identityBound())
        idStatus.setText (":" + juce::String (oscPortForIdentity (n)) + "  PORT IN USE — pick another N",
                          juce::dontSendNotification);
    else
        idStatus.setText (":" + juce::String (oscPortForIdentity (n)), juce::dontSendNotification);

    const juce::String nText = (n >= 1 && n <= 15) ? juce::String (n) : juce::String();
    if (idField.getText() != nText) idField.setText (nText, juce::dontSendNotification);
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
    titleLabel.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    statusLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    synthButton.setBounds (r.removeFromTop (32).removeFromLeft (140));
    r.removeFromTop (12);
    auto idRow = r.removeFromTop (24);
    idLabel.setBounds  (idRow.removeFromLeft (28));
    idField.setBounds  (idRow.removeFromLeft (48));
    idRow.removeFromLeft (8);
    idStatus.setBounds (idRow);
}

void CustosEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}
}
