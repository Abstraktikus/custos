#include <catch2/catch_test_macros.hpp>
#include "MidiRoute.h"
#include <array>

using namespace custos;

static std::array<uint8_t, 16> identity()
{
    std::array<uint8_t, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = (uint8_t) (i + 1); return r;
}

TEST_CASE ("applyMidiRoute passes identity unchanged")
{
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::noteOn (8, 60, (juce::uint8) 100), 0);
    applyMidiRoute (midi, identity(), scratch);
    bool found = false;
    for (const auto meta : midi) if (meta.getMessage().getChannel() == 8) found = true;
    REQUIRE (found);
}

TEST_CASE ("applyMidiRoute remaps input channel to target")
{
    auto r = identity(); r[7] = 1;   // input ch 8 -> out ch 1
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::noteOn (8, 60, (juce::uint8) 100), 0);
    applyMidiRoute (midi, r, scratch);
    for (const auto meta : midi) REQUIRE (meta.getMessage().getChannel() == 1);
}

TEST_CASE ("applyMidiRoute drops a channel mapped to 0")
{
    auto r = identity(); r[8] = 0;   // input ch 9 dropped
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::noteOn (9, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    applyMidiRoute (midi, r, scratch);
    int n = 0; for (const auto meta : midi) { juce::ignoreUnused (meta); ++n; }
    REQUIRE (n == 1);   // ch9 dropped, ch1 kept
}

TEST_CASE ("applyMidiRoute passes non-channel messages through")
{
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::midiClock(), 0);
    applyMidiRoute (midi, identity(), scratch);
    int n = 0; for (const auto meta : midi) { juce::ignoreUnused (meta); ++n; }
    REQUIRE (n == 1);
}

#include "CustosProcessor.h"

TEST_CASE ("CustosProcessor MIDI route defaults to identity and round-trips")
{
    custos::CustosProcessor proc;
    auto def = proc.getMidiRoute();
    REQUIRE (def[0] == 1);
    REQUIRE (def[15] == 16);

    std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = 16 - i;
    proc.setMidiRoute (r);
    REQUIRE (proc.getMidiRoute() == r);

    std::array<int, 16> bad {}; bad[0] = 99; bad[1] = -1;   // clamped to 0..16
    proc.setMidiRoute (bad);
    REQUIRE (proc.getMidiRoute()[0] == 16);
    REQUIRE (proc.getMidiRoute()[1] == 0);
}
