#include "FacadeParameter.h"

namespace custos
{
FacadeParameter::FacadeParameter (int i)
    : index (i), paramID ("custos_" + juce::String (i)) {}

juce::String FacadeParameter::getParameterID() const { return paramID; }

float FacadeParameter::getValue() const            { return 0.0f; }
void  FacadeParameter::setValue (float)            {}
float FacadeParameter::getDefaultValue() const     { return 0.0f; }
juce::String FacadeParameter::getName (int) const  { return {}; }
juce::String FacadeParameter::getLabel() const     { return {}; }
float FacadeParameter::getValueForText (const juce::String&) const { return 0.0f; }
}
