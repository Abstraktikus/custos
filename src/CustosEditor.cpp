#include "CustosEditor.h"
#include "CustosProcessor.h"

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

    refresh();
    setSize (320, 120);
}

CustosEditor::~CustosEditor() = default;

void CustosEditor::refresh()
{
    statusLabel.setText ("Synth: " + proc.innerSynthName(), juce::dontSendNotification);
    synthButton.setButtonText (proc.isSynthWindowVisible() ? "Hide Synth" : "Show Synth");
    synthButton.setEnabled (proc.hasInnerSynth());
}

void CustosEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    titleLabel.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    statusLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    synthButton.setBounds (r.removeFromTop (32).removeFromLeft (140));
}

void CustosEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}
}
