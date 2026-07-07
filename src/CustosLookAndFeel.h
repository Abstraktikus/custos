#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Flat dark-mode look for the Custos editor, matching the Kapellmeister main app:
// #0B111A background, #161F2E boxes/inputs, #2A75D3 accent, #E2E8F0 text, #64748B muted,
// 1px borders, uniform 4px corner radius, thin "V" combo arrows, no shadows/gradients.
namespace custos::theme
{
    inline const juce::Colour bg       { 0xff0B111A };   // page background
    inline const juce::Colour box      { 0xff161F2E };   // boxes / inputs
    inline const juce::Colour accent   { 0xff2A75D3 };   // accent blue
    inline const juce::Colour text     { 0xffE2E8F0 };   // primary text
    inline const juce::Colour muted    { 0xff64748B };   // muted text
    inline const juce::Colour danger   { 0xffDC2626 };   // bypass/delete red
    inline const juce::Colour divider  { 0xff1E293B };   // hairline separators / borders

    inline constexpr float corner  = 4.0f;   // uniform radius
    inline constexpr float border  = 1.0f;   // max hairline stroke
}

namespace custos
{
// A flat 1px-bordered, 4px-radius theme with thin combo arrows and a blue slider.
class CustosLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustosLookAndFeel();

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline    (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

// A thematic section header: a small flat vector icon + a title, drawn in the accent tint.
// One icon per themed section.
class SectionHeader : public juce::Component
{
public:
    enum class Icon { Instrument, Presets, Audio, Display, Midi };

    SectionHeader (Icon icon, juce::String title, juce::Colour tint = theme::accent)
        : iconKind (icon), titleText (std::move (title)), iconTint (tint) {}

    void paint (juce::Graphics&) override;

private:
    void drawIcon (juce::Graphics&, juce::Rectangle<float> box, juce::Colour) const;

    Icon iconKind;
    juce::String titleText;
    juce::Colour iconTint;
};
}
