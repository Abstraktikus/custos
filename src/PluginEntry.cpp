#include "CustosProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new custos::CustosProcessor();
}
