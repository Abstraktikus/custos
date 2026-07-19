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

void SynthWindow::applyRect (juce::Rectangle<int> logical, bool movable, bool sticky)
{
    const juce::ScopedValueSetter<bool> guard (applyingRect, true);   // resized()/childBoundsChanged() won't re-fit or re-emit
    draggable  = movable;
    // Remember (or release) the sticky fit. While active, childBoundsChanged() keeps the window
    // pinned to this rect whenever the hosted editor resizes itself (init/settle/preset-load).
    fitActive  = sticky;
    fitLogical = logical;

    if (auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
        if (ed->isResizable())
        {
            // ONE polite attempt at the exact content size (rect minus the window border). Editors
            // that report resizable but clamp to fixed zoom steps (Roland, Arturia — live-verified
            // 2026-07-19) may refuse or re-assert their preferred size; layoutContent() then scales
            // the ACHIEVED size instead of asking again — never a setSize fight.
            const auto area = getContentComponentBorder()
                                  .subtractedFrom (juce::Rectangle<int> (logical.getWidth(), logical.getHeight()));
            ed->setTransform ({});
            ed->setSize (area.getWidth(), area.getHeight());
        }

    setBounds (logical);   // -> resized() -> layoutContent()
    layoutContent();       // also when setBounds was a no-op (same rect re-applied)
}

void SynthWindow::layoutContent()
{
    if (inLayout) return;                                     // a layout pass never re-enters itself
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    auto* ed = getContentComponent();
    if (ed == nullptr) return;
    const auto area = getContentComponentBorder().subtractedFrom (getLocalBounds());
    const int ew = ed->getWidth(), eh = ed->getHeight();
    if (ew <= 0 || eh <= 0 || area.isEmpty()) return;

    if (juce::isWithin (ew, area.getWidth(), 2) && juce::isWithin (eh, area.getHeight(), 2))
    {
        ed->setTransform ({});                       // matches (within layout rounding): native pixels
        ed->setTopLeftPosition (area.getPosition());
        return;
    }
    // Achieved-size fallback: scale the editor AS IS into the content area — uniform (aspect-
    // preserved), centred. Compliant editors never get here; clamping ones converge in one step.
    const float s = juce::jmin ((float) area.getWidth()  / (float) ew,
                                (float) area.getHeight() / (float) eh);
    ed->setTopLeftPosition (0, 0);   // position is pre-scaled by the transform -> placement lives in it
    ed->setTransform (juce::AffineTransform::scale (s)
        .translated ((float) area.getX() + ((float) area.getWidth()  - (float) ew * s) * 0.5f,
                     (float) area.getY() + ((float) area.getHeight() - (float) eh * s) * 0.5f));
}

void SynthWindow::moved()
{
    if (onReadout) onReadout();     // live x/y readout follows a drag
}

void SynthWindow::resized()
{
    // Deliberately NOT ResizableWindow::resized(): its layout force-resizes the content to the
    // border area (setBoundsInset) on every pass — exactly the size fight a clamping editor
    // escalates. This borderless window has no resizer border/corner, so the base layout is
    // only that content sizing; layoutContent() replaces it without ever resizing the editor.
    layoutContent();
    if (onReadout) onReadout();                             // live w/h readout (incl. inner-synth zoom)
    if (! applyingRect && onCommit) onCommit();             // a content-driven resize reports to KM
}

void SynthWindow::childBoundsChanged (juce::Component* child)
{
    if (inLayout) return;   // layoutContent() only moves/transforms — its callbacks are noise
    if (child != getContentComponent())
    {
        juce::ResizableWindow::childBoundsChanged (child);
        return;
    }

    if (fitActive)
    {
        if (applyingRect) return;   // our own layout move / polite resize attempt -> already handled
        // Sticky dock fit: the editor changed size on its own (init / settle / preset-load).
        // Never fight it — keep its achieved size, re-pin the window, re-fit via transform.
        const juce::ScopedValueSetter<bool> guard (applyingRect, true);   // internal re-pin: no commit
        setBounds (fitLogical);
        layoutContent();
        return;
    }
    // Undocked window: default resize-to-fit-content behaviour so synth-zoom is tracked.
    juce::ResizableWindow::childBoundsChanged (child);
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
