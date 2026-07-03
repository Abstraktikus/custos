#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace custos
{
// One facade parameter at a fixed index with a stable VST3 id ("custos_<index>").
// M1 Task 1: inert only. Task 2 adds delegation to a bound inner parameter.
class FacadeParameter : public juce::HostedAudioProcessorParameter
{
public:
    explicit FacadeParameter (int index);

    juce::String getParameterID() const override;

    float getValue() const override;
    void  setValue (float newValue) override;
    float getDefaultValue() const override;
    juce::String getName (int maximumStringLength) const override;
    juce::String getLabel() const override;
    float getValueForText (const juce::String& text) const override;

private:
    const int index;
    const juce::String paramID;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FacadeParameter)
};
}
