#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace custos::test
{
// Minimal AudioProcessor with N named float params. Records prepare/process calls
// and fills output channel 0 with 0.5 so passthrough is observable.
class FakeInnerProcessor : public juce::AudioProcessor
{
public:
    explicit FakeInnerProcessor (int numParams = 3)
        : juce::AudioProcessor (BusesProperties()
            .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < numParams; ++i)
            addParameter (new juce::AudioParameterFloat (
                juce::ParameterID { "fake" + juce::String (i), 1 },
                "Fake " + juce::String (i), 0.0f, 1.0f, 0.25f));
    }

    const juce::String getName() const override { return "FakeInner"; }
    void prepareToPlay (double sr, int block) override
        { prepared = true; lastSampleRate = sr; lastBlock = block; }
    void releaseResources() override { prepared = false; ++releaseCalls; if (releaseSink != nullptr) ++*releaseSink; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        ++processCalls;
        lastNumMidi = m.getNumEvents();
        if (b.getNumChannels() > 0)
            juce::FloatVectorOperations::fill (b.getWritePointer (0), 0.5f, b.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& mb) override
    {
        mb.replaceAll (stateMarker.toRawUTF8(), stateMarker.getNumBytesAsUTF8());
    }
    void setStateInformation (const void* d, int n) override
    {
        restoredMarker = juce::String::fromUTF8 (static_cast<const char*> (d), n);
    }
    juce::String stateMarker { "fake-state" };
    juce::String restoredMarker;

    bool prepared = false;
    double lastSampleRate = 0.0;
    int lastBlock = 0, processCalls = 0, lastNumMidi = 0;
    int releaseCalls = 0;
    int* releaseSink = nullptr;   // optional external counter that outlives this object (for swap tests)
};
}
