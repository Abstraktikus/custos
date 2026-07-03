#include "FacadeParameter.h"
#include "HostTrace.h"

namespace custos
{
FacadeParameter::FacadeParameter (int i)
    : index (i), paramID ("custos_" + juce::String (i)) {}

void FacadeParameter::bind (juce::AudioProcessorParameter* p) noexcept { inner.store (p); }
juce::AudioProcessorParameter* FacadeParameter::boundParameter() const noexcept { return inner.load(); }

juce::String FacadeParameter::getParameterID() const { return paramID; }

float FacadeParameter::getValue() const
{
    if (auto* p = inner.load()) return p->getValue();
    return 0.0f;
}
void FacadeParameter::setValue (float v)
{
    if (auto* p = inner.load()) p->setValue (v);
}
float FacadeParameter::getDefaultValue() const
{
    if (auto* p = inner.load()) return p->getDefaultValue();
    return 0.0f;
}
juce::String FacadeParameter::getName (int len) const
{
    static std::atomic<bool> firstLogged { false };
    bool expected = false;
    if (firstLogged.compare_exchange_strong (expected, true))
        trace ("first FacadeParameter::getName (index=" + juce::String (index) + ")");

    if (auto* p = inner.load()) return p->getName (len);
    return {};
}
juce::String FacadeParameter::getLabel() const
{
    if (auto* p = inner.load()) return p->getLabel();
    return {};
}
int FacadeParameter::getNumSteps() const
{
    if (auto* p = inner.load()) return p->getNumSteps();
    return juce::AudioProcessorParameter::getNumSteps();
}
bool FacadeParameter::isDiscrete() const
{
    if (auto* p = inner.load()) return p->isDiscrete();
    return false;
}
bool FacadeParameter::isBoolean() const
{
    if (auto* p = inner.load()) return p->isBoolean();
    return false;
}
juce::String FacadeParameter::getText (float value, int len) const
{
    if (auto* p = inner.load()) return p->getText (value, len);
    return {};
}
float FacadeParameter::getValueForText (const juce::String& text) const
{
    if (auto* p = inner.load()) return p->getValueForText (text);
    return 0.0f;
}
}
