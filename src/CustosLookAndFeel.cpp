#include "CustosLookAndFeel.h"

namespace custos
{
using namespace theme;

static juce::Font uiFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

CustosLookAndFeel::CustosLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);

    setColour (juce::ComboBox::backgroundColourId, box);
    setColour (juce::ComboBox::textColourId,       text);
    setColour (juce::ComboBox::outlineColourId,    divider);
    setColour (juce::ComboBox::arrowColourId,      muted);

    setColour (juce::PopupMenu::backgroundColourId,            box);
    setColour (juce::PopupMenu::textColourId,                 text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    setColour (juce::TextButton::buttonColourId,  box);
    setColour (juce::TextButton::textColourOnId,  text);
    setColour (juce::TextButton::textColourOffId, text);

    setColour (juce::TextEditor::backgroundColourId,      box);
    setColour (juce::TextEditor::textColourId,            text);
    setColour (juce::TextEditor::outlineColourId,         divider);
    setColour (juce::TextEditor::focusedOutlineColourId,  accent);
    setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.30f));
    setColour (juce::CaretComponent::caretColourId,       accent);

    setColour (juce::Label::textColourId, text);

    setColour (juce::Slider::backgroundColourId, divider);
    setColour (juce::Slider::trackColourId,      accent);
    setColour (juce::Slider::thumbColourId,      accent);

    setColour (juce::ToggleButton::textColourId,         text);
    setColour (juce::ToggleButton::tickColourId,         accent);
    setColour (juce::ToggleButton::tickDisabledColourId, muted);
}

juce::Font CustosLookAndFeel::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f); }
juce::Font CustosLookAndFeel::getLabelFont    (juce::Label&)    { return uiFont (13.0f); }

void CustosLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& cb)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (border * 0.5f);
    g.setColour (box);
    g.fillRoundedRectangle (r, corner);
    g.setColour (cb.hasKeyboardFocus (true) ? accent : divider);
    g.drawRoundedRectangle (r, corner, border);

    // thin "V" arrow
    const float cx = (float) width - 13.0f;
    const float cy = (float) height * 0.5f;
    juce::Path v;
    v.startNewSubPath (cx - 4.0f, cy - 2.0f);
    v.lineTo          (cx,        cy + 2.5f);
    v.lineTo          (cx + 4.0f, cy - 2.0f);
    g.setColour (muted);
    g.strokePath (v, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CustosLookAndFeel::positionComboBoxText (juce::ComboBox& cb, juce::Label& label)
{
    label.setBounds (8, 0, cb.getWidth() - 26, cb.getHeight());
    label.setFont (getComboBoxFont (cb));
}

void CustosLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                              const juce::Colour& backgroundColour, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (border * 0.5f);
    auto fill = backgroundColour;
    if (down)        fill = fill.darker (0.20f);
    else if (highlighted) fill = fill.brighter (0.12f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (backgroundColour == danger ? danger : divider);
    g.drawRoundedRectangle (r, corner, border);
}

void CustosLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.findColour (juce::TextButton::textColourOffId));
    g.setFont (uiFont (13.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

void CustosLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const float sz = 16.0f;
    auto bounds = b.getLocalBounds().toFloat();
    auto check = juce::Rectangle<float> (0.0f, (bounds.getHeight() - sz) * 0.5f, sz, sz);

    g.setColour (box);
    g.fillRoundedRectangle (check, 3.0f);
    g.setColour (b.getToggleState() ? accent : (highlighted ? muted : divider));
    g.drawRoundedRectangle (check.reduced (border * 0.5f), 3.0f, border);

    if (b.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (check.getX() + 4.0f,      check.getCentreY());
        tick.lineTo          (check.getCentreX() - 1.0f, check.getBottom() - 4.5f);
        tick.lineTo          (check.getRight() - 3.5f,   check.getY() + 4.5f);
        g.setColour (accent);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (b.findColour (juce::ToggleButton::textColourId));
    g.setFont (uiFont (13.0f));
    g.drawFittedText (b.getButtonText(),
                      b.getLocalBounds().withTrimmedLeft ((int) sz + 6),
                      juce::Justification::centredLeft, 1);
}

void CustosLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy    = (float) y + (float) height * 0.5f;
    const float left  = (float) x;
    const float right = (float) (x + width);

    g.setColour (divider);
    g.fillRoundedRectangle (left, cy - 1.5f, (float) width, 3.0f, 1.5f);
    g.setColour (accent);
    g.fillRoundedRectangle (left, cy - 1.5f, juce::jlimit (0.0f, (float) width, sliderPos - left), 3.0f, 1.5f);

    const float rad = 6.0f;
    g.setColour (accent);
    g.fillEllipse (juce::jlimit (left, right - 2.0f * rad, sliderPos - rad), cy - rad, 2.0f * rad, 2.0f * rad);
}

void CustosLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (box);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (border * 0.5f), corner);
}

void CustosLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te)
{
    g.setColour (te.hasKeyboardFocus (true) ? accent : divider);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (border * 0.5f), corner, border);
}

// ---- SectionHeader --------------------------------------------------------

void SectionHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto iconBox = r.removeFromLeft (20.0f).withSizeKeepingCentre (16.0f, 16.0f);
    drawIcon (g, iconBox, iconTint);
    r.removeFromLeft (8.0f);
    g.setColour (iconTint);
    g.setFont (uiFont (13.0f, true));
    g.drawText (titleText, r, juce::Justification::centredLeft);
}

void SectionHeader::drawIcon (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) const
{
    g.setColour (c);
    const float sw = 1.4f;
    const float bx = b.getX(), by = b.getY(), bw = b.getWidth(), bh = b.getHeight();

    switch (iconKind)
    {
        case Icon::Instrument:   // a synth module: bordered body + two knobs
        {
            g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, sw);
            g.fillEllipse (bx + bw * 0.30f - 2.0f, by + bh * 0.60f - 2.0f, 4.0f, 4.0f);
            g.fillEllipse (bx + bw * 0.62f - 2.0f, by + bh * 0.60f - 2.0f, 4.0f, 4.0f);
            g.drawLine (bx + bw * 0.24f, by + bh * 0.34f, bx + bw * 0.76f, by + bh * 0.34f, sw);
            break;
        }
        case Icon::Presets:      // a floppy disk (save): body with a cut corner + shutter + label
        {
            juce::Path fl;
            const float cut = bw * 0.24f;
            fl.startNewSubPath (bx + 1.0f, by + 1.0f);
            fl.lineTo (b.getRight() - 1.0f - cut, by + 1.0f);
            fl.lineTo (b.getRight() - 1.0f, by + 1.0f + cut);
            fl.lineTo (b.getRight() - 1.0f, b.getBottom() - 1.0f);
            fl.lineTo (bx + 1.0f, b.getBottom() - 1.0f);
            fl.closeSubPath();
            g.strokePath (fl, juce::PathStrokeType (sw));
            g.fillRect (bx + bw * 0.28f, by + bh * 0.55f, bw * 0.44f, bh * 0.30f);   // label
            g.fillRect (bx + bw * 0.55f, by + 2.0f, bw * 0.14f, bh * 0.22f);          // shutter
            break;
        }
        case Icon::Audio:        // speaker + wave
        {
            juce::Path sp;
            sp.startNewSubPath (bx + bw * 0.05f, by + bh * 0.38f);
            sp.lineTo          (bx + bw * 0.22f, by + bh * 0.38f);
            sp.lineTo          (bx + bw * 0.45f, by + bh * 0.15f);
            sp.lineTo          (bx + bw * 0.45f, by + bh * 0.85f);
            sp.lineTo          (bx + bw * 0.22f, by + bh * 0.62f);
            sp.lineTo          (bx + bw * 0.05f, by + bh * 0.62f);
            sp.closeSubPath();
            g.fillPath (sp);
            juce::Path arc;
            arc.addCentredArc (bx + bw * 0.52f, by + bh * 0.5f, bw * 0.26f, bh * 0.30f, 0.0f, -0.9f, 0.9f, true);
            g.strokePath (arc, juce::PathStrokeType (sw));
            break;
        }
        case Icon::Display:      // a window: screen + title bar
        {
            g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, sw);
            g.drawLine (bx + 1.0f, by + bh * 0.30f, b.getRight() - 1.0f, by + bh * 0.30f, sw);
            g.fillEllipse (bx + bw * 0.20f - 1.2f, by + bh * 0.17f - 1.2f, 2.4f, 2.4f);
            break;
        }
        case Icon::Midi:         // piano keys
        {
            const float ky = by + 2.0f, kh = bh - 4.0f;
            const int   keys = 4;
            const float kw = bw / (float) keys;
            for (int i = 0; i < keys; ++i)
                g.drawRoundedRectangle (bx + (float) i * kw + 0.5f, ky, kw - 1.5f, kh, 1.0f, sw);
            for (int i = 0; i < keys - 1; ++i)
                g.fillRect (bx + (float) (i + 1) * kw - kw * 0.28f, ky, kw * 0.56f, kh * 0.60f);
            break;
        }
    }
}
}
