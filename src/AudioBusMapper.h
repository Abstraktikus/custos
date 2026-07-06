#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace custos
{
// Fold the inner synth's output channels into Custos's fixed 5 stereo output buses (out ch 0..9).
// Default: inner pair k -> bus k (k=1..4); pairs >=5 summed into bus 5. mainLROnly: sum all -> bus 1.
// A lone/mono trailing channel is duplicated to L+R of its target bus. `out` must have >=10 channels.
inline void mapInnerToOutputs (const juce::AudioBuffer<float>& inner,
                               juce::AudioBuffer<float>& out, bool mainLROnly)
{
    const int n  = out.getNumSamples();
    const int ic = inner.getNumChannels();
    out.clear();

    auto addPair = [&] (int bus, int lSrc, int rSrc)
    {
        if (lSrc >= 0 && lSrc < ic) out.addFrom (bus * 2,     0, inner, lSrc, 0, n);
        if (rSrc >= 0 && rSrc < ic) out.addFrom (bus * 2 + 1, 0, inner, rSrc, 0, n);
    };

    if (mainLROnly)
    {
        for (int c = 0; c < ic; ++c) out.addFrom (c % 2, 0, inner, c, 0, n);   // L->0, R->1, alternating
        return;
    }

    for (int pair = 0; pair * 2 < ic; ++pair)
    {
        const int l = pair * 2, r = pair * 2 + 1;
        const int bus = pair < 4 ? pair : 4;                 // pairs >=5 fold into bus 5 (index 4)
        const int rSrc = (r < ic) ? r : l;                   // mono -> duplicate L to R
        addPair (bus, l, rSrc);
    }
}
}
