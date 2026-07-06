#include <catch2/catch_test_macros.hpp>
#include "AudioBusMapper.h"
#include <juce_audio_basics/juce_audio_basics.h>

using namespace custos;

// Fill each channel c with the constant value (c+1) so we can trace where it lands.
static juce::AudioBuffer<float> ramp (int chans, int n = 4)
{
    juce::AudioBuffer<float> b (chans, n);
    for (int c = 0; c < chans; ++c)
    {
        b.clear (c, 0, n);
        juce::FloatVectorOperations::add (b.getWritePointer (c), (float) (c + 1), n);
    }
    return b;
}
static float v (const juce::AudioBuffer<float>& b, int ch) { return b.getSample (ch, 0); }

TEST_CASE ("default mapping: pairs 1..4 -> buses 1..4, pairs >=5 summed into bus 5")
{
    auto inner = ramp (12);                 // 6 stereo pairs: ch values 1..12
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, false);
    REQUIRE (v (out, 0) == 1.0f);  REQUIRE (v (out, 1) == 2.0f);   // bus1 = pair1
    REQUIRE (v (out, 6) == 7.0f);  REQUIRE (v (out, 7) == 8.0f);   // bus4 = pair4
    // bus5 = pair5 + pair6 = (9+11, 10+12)
    REQUIRE (v (out, 8) == 20.0f); REQUIRE (v (out, 9) == 22.0f);
}

TEST_CASE ("default mapping: fewer than 5 pairs leaves upper buses silent")
{
    auto inner = ramp (2);                  // just the main pair
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, false);
    REQUIRE (v (out, 0) == 1.0f); REQUIRE (v (out, 1) == 2.0f);
    for (int c = 2; c < 10; ++c) REQUIRE (v (out, c) == 0.0f);
}

TEST_CASE ("Main L/R only: all channels summed into bus 1, buses 2..5 silent")
{
    auto inner = ramp (6);                  // ch 1..6
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, true);
    REQUIRE (v (out, 0) == 1.0f + 3.0f + 5.0f);   // all L channels
    REQUIRE (v (out, 1) == 2.0f + 4.0f + 6.0f);   // all R channels
    for (int c = 2; c < 10; ++c) REQUIRE (v (out, c) == 0.0f);
}

TEST_CASE ("mono lone channel duplicated to L+R of its bus")
{
    auto inner = ramp (1);                  // single channel value 1
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, false);
    REQUIRE (v (out, 0) == 1.0f); REQUIRE (v (out, 1) == 1.0f);
}

TEST_CASE ("empty inner -> all buses silent")
{
    juce::AudioBuffer<float> inner (0, 4), out (10, 4);
    mapInnerToOutputs (inner, out, false);
    for (int c = 0; c < 10; ++c) REQUIRE (v (out, c) == 0.0f);
}
