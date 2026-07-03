#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Config.h"
#include "FacadeParameter.h"
#include "InnerBinding.h"
#include <vector>
#include <memory>

namespace custos
{
class SynthWindow;   // forward declaration (unique_ptr member; defined in SynthWindow.h)

class CustosProcessor : public juce::AudioProcessor
{
public:
    CustosProcessor();
    ~CustosProcessor() override;

    // Takes ownership of an inner processor, binds the facade to it, and prepares it
    // if this processor is already prepared. Pass nullptr to detach.
    void attachInner (std::unique_ptr<juce::AudioProcessor> newInner);

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return kProduct; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override
        { return inner != nullptr ? inner->getTailLengthSeconds() : 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;   // M3 (traced for the count-timing experiment)
    void setStateInformation (const void*, int) override;     // M3 (traced)

    int facadeSize() const noexcept { return (int) facade.size(); }
    int boundParamCount() const noexcept { return boundCount; }

    // M2 synth-window API — message thread only.
    void toggleSynthWindow();
    void showSynthWindow();
    void hideSynthWindow();
    bool isSynthWindowVisible() const noexcept { return synthWindow != nullptr; }
    bool hasInnerSynth() const noexcept { return inner != nullptr; }
    juce::String innerSynthName() const;

protected:
    std::vector<FacadeParameter*> facade;   // non-owning: AudioProcessor owns via addParameter

private:
    std::unique_ptr<juce::AudioProcessor> inner;
    std::unique_ptr<SynthWindow> synthWindow;   // M2, message-thread only; nullptr == hidden
    std::shared_ptr<bool> aliveToken { std::make_shared<bool> (true) };   // guards deferred close callbacks against use-after-free
    int boundCount = 0;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    bool isPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosProcessor)
};
}
