#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FacadeParameter.h"
#include <vector>

namespace custos
{
// Points facade[0..n-1] at the inner processor's parameters (host order) and clears
// facade[n..end] to inert, where n = min(inner param count, facade size).
class InnerBinding
{
public:
    static int  bind (juce::AudioProcessor& inner,
                      const std::vector<FacadeParameter*>& facade) noexcept;
    static void unbindAll (const std::vector<FacadeParameter*>& facade) noexcept;
};
}
