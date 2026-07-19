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

static bool fitsWithin (int ew, int eh, juce::Rectangle<int> area)
{
    return ew <= area.getWidth() + 2 && eh <= area.getHeight() + 2;   // +-2: window-border layout slack
}

juce::Rectangle<int> SynthWindow::contentArea() const
{
    return getContentComponentBorder().subtractedFrom (getLocalBounds());
}

void SynthWindow::applyRect (juce::Rectangle<int> logical, bool movable, bool sticky)
{
    const juce::ScopedValueSetter<bool> guard (applyingRect, true);   // resized()/childBoundsChanged() won't re-emit
    draggable      = movable;
    fitActive      = sticky;
    fitLogical     = logical;
    scaleAttempted = false;    // each apply may spend ONE content-scale attempt (here or on a re-assert)

    setBounds (logical);          // pin first: the content area derives from the real bounds
    negotiateContentSize (true);  // polite resize + (on synchronous refusal) the scale attempt
    layoutContent();
}

void SynthWindow::negotiateContentSize (bool politeResize)
{
    auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent());
    if (ed == nullptr) return;
    const auto area = contentArea();
    if (area.isEmpty()) return;

    if (appliedContentScale != 1.0f)   // fresh baseline for a new negotiation
    {
        ed->setScaleFactor (1.0f);
        appliedContentScale = 1.0f;
    }

    if (politeResize && ed->isResizable())
    {
        // ONE polite ask at the exact content size. Editors that report resizable but clamp to
        // fixed zoom steps (Roland, Arturia — live 2026-07-19) snap back synchronously or
        // re-assert their preferred size ~20 ms later; both paths land below / in
        // childBoundsChanged. Never ask twice — that ping-pongs.
        ed->setTransform ({});
        ed->setSize (area.getWidth(), area.getHeight());
    }

    if (fitsWithin (ed->getWidth(), ed->getHeight(), area)) return;

    // Oversized: ONE content-scale attempt (VST3 IPlugView setContentScaleFactor via the JUCE
    // hosting wrapper's setScaleFactor, which re-reads the plugin's size). Editors that honour
    // it re-render smaller. A JUCE AffineTransform can NOT do this: the hosted editor is a
    // native child HWND that blits at its own size regardless (live-verified TR-909/Jup-8000).
    if (! scaleAttempted)
    {
        scaleAttempted = true;
        const int ew = ed->getWidth(), eh = ed->getHeight();
        if (ew > 0 && eh > 0)
        {
            const float s = juce::jmin ((float) area.getWidth()  / (float) ew,
                                        (float) area.getHeight() / (float) eh);
            ed->setScaleFactor (s);
            if (fitsWithin (ed->getWidth(), ed->getHeight(), area))
            {
                appliedContentScale = s;   // honoured: plugin re-rendered into the area
                return;
            }
            ed->setScaleFactor (1.0f);     // ignored -> roll back; clip-centre placement remains
        }
    }
}

void SynthWindow::layoutContent()
{
    if (inLayout) return;                                     // a layout pass never re-enters itself
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    auto* ed = getContentComponent();
    if (ed == nullptr) return;
    const auto area = contentArea();
    const int ew = ed->getWidth(), eh = ed->getHeight();
    if (ew <= 0 || eh <= 0 || area.isEmpty()) return;

    if (juce::isWithin (ew, area.getWidth(), 2) && juce::isWithin (eh, area.getHeight(), 2))
    {
        ed->setTopLeftPosition (area.getPosition());   // matches: default placement, pixel-sharp
        return;
    }
    // Centre the editor AS IS: letterbox when it is smaller than the area, symmetric clip (the
    // MIDDLE of the GUI stays visible) when the plugin's minimum exceeds it. Never scaled by
    // transform — the hosted editor is a native child HWND that ignores JUCE transforms.
    ed->setTopLeftPosition (area.getX() + (area.getWidth()  - ew) / 2,
                            area.getY() + (area.getHeight() - eh) / 2);
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
    if (inLayout) return;   // layoutContent() only moves the editor — its callbacks are noise
    if (child != getContentComponent())
    {
        juce::ResizableWindow::childBoundsChanged (child);
        return;
    }
    if (applyingRect) return;   // inside applyRect / a sticky re-pin: geometry is being imposed

    if (fitActive)
    {
        // Sticky dock fit: the editor changed size on its own (init / settle / preset-load /
        // ~20 ms clamp re-assert). Never fight it — re-pin the window, spend the negotiation's
        // scale attempt if it is still available, and place the achieved size.
        const juce::ScopedValueSetter<bool> guard (applyingRect, true);   // internal re-pin: no commit
        setBounds (fitLogical);
        negotiateContentSize (false);   // no repeated setSize ask — scale attempt / rollback only
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
