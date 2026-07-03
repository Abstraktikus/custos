#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace custos
{
// Facade parameter at a fixed index with a stable VST3 id ("custos_<index>").
// When bound to an inner parameter, mirrors it 1:1; when unbound, inert.
class FacadeParameter : public juce::HostedAudioProcessorParameter
{
public:
    explicit FacadeParameter (int index);

    void bind (juce::AudioProcessorParameter* inner) noexcept;   // nullptr = inert
    juce::AudioProcessorParameter* boundParameter() const noexcept;

    juce::String getParameterID() const override;

    float getValue() const override;
    void  setValue (float newValue) override;
    float getDefaultValue() const override;
    juce::String getName (int maximumStringLength) const override;
    juce::String getLabel() const override;
    int   getNumSteps() const override;
    bool  isDiscrete() const override;
    bool  isBoolean() const override;
    juce::String getText (float value, int maximumStringLength) const override;
    float getValueForText (const juce::String& text) const override;

private:
    const int index;
    const juce::String paramID;
    std::atomic<juce::AudioProcessorParameter*> inner { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FacadeParameter)
};
}
