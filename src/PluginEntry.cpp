#include "CustosProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new custos::CustosProcessor (true);   // enable the OSC receiver in the real plugin
}
