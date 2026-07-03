#include "CustosProcessor.h"

namespace custos
{
CustosProcessor::CustosProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    facade.reserve (kFacadeParamCount);
    for (int i = 0; i < kFacadeParamCount; ++i)
    {
        auto* p = new FacadeParameter (i);
        addParameter (p);          // AudioProcessor takes ownership
        facade.push_back (p);
    }
}

void CustosProcessor::prepareToPlay (double, int) {}
void CustosProcessor::releaseResources() {}

void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();   // M1 Task 1: silence until inner synth is attached (Task 4/5)
}

bool CustosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
}
