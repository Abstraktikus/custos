#include "SynthWindow.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace custos
{
SynthWindow::SynthWindow (juce::Component* editor)
    : juce::ResizableWindow ("Custos Synth", juce::Colour (0xff2b2b2b), true)   // addToDesktop; no title bar
{
    setContentOwned (editor, true);   // window tracks the editor's native size until applyRect overrides it
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void SynthWindow::applyRect (juce::Rectangle<int> logical, bool movable)
{
    const juce::ScopedValueSetter<bool> guard (applyingRect, true);   // resized() won't re-emit; caller emits once
    if (auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
    {
        if (ed->isResizable())
        {
            ed->setTransform ({});                                    // clear any prior scale
            ed->setSize (logical.getWidth(), logical.getHeight());
        }
        else if (ed->getWidth() > 0 && ed->getHeight() > 0)
        {
            ed->setTransform (juce::AffineTransform::scale (
                (float) logical.getWidth()  / (float) ed->getWidth(),
                (float) logical.getHeight() / (float) ed->getHeight()));
        }
    }
    setBounds (logical);
    draggable = movable;
}

void SynthWindow::moved()
{
    if (onReadout) onReadout();     // live x/y readout follows a drag
}

void SynthWindow::resized()
{
    juce::ResizableWindow::resized();                       // lay out the hosted editor first
    if (onReadout) onReadout();                             // live w/h readout (incl. inner-synth zoom)
    if (! applyingRect && onCommit) onCommit();             // a content-driven resize reports to KM
}

void SynthWindow::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    if (draggable) dragger.startDraggingComponent (this, e);
}

void SynthWindow::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggable) return;
    dragger.dragComponent (this, e, nullptr);   // moved() fires the live readout
    dragged = true;
}

void SynthWindow::mouseUp (const juce::MouseEvent&)
{
    if (draggable && dragged && onCommit) onCommit();   // report the final position once
    dragged = false;
}
}
