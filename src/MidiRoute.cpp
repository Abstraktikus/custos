#include "MidiRoute.h"

namespace custos
{
void applyMidiRoute (juce::MidiBuffer& midi, const std::array<std::uint8_t, 16>& route, juce::MidiBuffer& scratch)
{
    scratch.clear();   // keeps capacity across blocks -> no per-block allocation after warmup
    for (const auto meta : midi)
    {
        auto m = meta.getMessage();
        const int ch = m.getChannel();               // 1..16 for channel-voice; 0 otherwise
        if (ch > 0)
        {
            const int t = route[(size_t) (ch - 1)];
            if (t == 0) continue;                    // dropped
            if (t != ch) m.setChannel (t);           // remapped
        }
        scratch.addEvent (m, meta.samplePosition);
    }
    midi.swapWith (scratch);
}
}
