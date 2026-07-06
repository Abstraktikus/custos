#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

namespace custos
{
// Rewrite `midi` in place per `route`: for each channel-voice message on input channel c (1..16),
// route[c-1] == 0 drops it, else remaps the channel to route[c-1]. Non-channel messages pass through.
// `scratch` is a caller-owned reusable buffer (avoids audio-thread allocation); its contents are clobbered.
void applyMidiRoute (juce::MidiBuffer& midi, const std::array<std::uint8_t, 16>& route, juce::MidiBuffer& scratch);
}
