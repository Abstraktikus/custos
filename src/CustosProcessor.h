#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Config.h"
#include "FacadeParameter.h"
#include "InnerBinding.h"
#include <juce_osc/juce_osc.h>
#include <vector>
#include <memory>
#include <functional>

namespace custos
{
class SynthWindow;       // forward declaration (unique_ptr member; defined in SynthWindow.h)
class CustosOscServer;   // forward declaration (unique_ptr member; defined in CustosOscServer.h)

struct CommandResult { bool ok = false; int innerCount = 0; juce::String message; };

class CustosProcessor : public juce::AudioProcessor
{
public:
    explicit CustosProcessor (bool enableOsc = false);
    ~CustosProcessor() override;

    // M3 safe runtime swap (message thread). loadInner(nullptr) == clear.
    bool loadInner (std::unique_ptr<juce::AudioProcessor> newInner, const juce::String& path = {});
    CommandResult load (const juce::String& path);
    void clear();
    void attachInner (std::unique_ptr<juce::AudioProcessor> newInner) { loadInner (std::move (newInner)); }

    // Addressing core (message thread). N in 1..15; 0 = unassigned.
    void setIdentity (int n);
    int  identity() const noexcept { return identityN; }
    bool identityBound() const noexcept { return lastBindOk; }
    juce::String modeString() const noexcept { return "replace"; }   // real toggle = F2

    // F1: stream the bound params in [start, start+count) (clamped to boundCount) via outboundSink,
    // then a /custos/params/done. Message thread. No-op if outboundSink is null.
    void dumpParams (int start, int count);

    // Set by CustosOscServer to send to the KM hub; null in unit tests (emission is then a no-op).
    std::function<void(const juce::OSCMessage&)> outboundSink;

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
    {
        // Guarded like processBlock: the host may query this on the audio thread while a swap
        // (loadInner) is mid-flight on the message thread — reading `inner` unguarded is a data race.
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        return (tl.isLocked() && inner != nullptr) ? inner->getTailLengthSeconds() : 0.0;
    }

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
    void refreshEditor();   // refresh the active CustosEditor (if any) after a window state change

    std::unique_ptr<juce::AudioProcessor> inner;
    mutable juce::SpinLock swapLock;  // guards the inner-pointer swap vs the audio thread (also getTailLengthSeconds)
    juce::String   currentSynthPath;  // path of the loaded synth ("" = none); persisted
    int  identityN = 0;        // operator-set; 0 = unassigned. Persisted (CUS v2).
    bool lastBindOk = false;   // did the OSC receiver bind BASE+N?
    void emitLoaded();         // send /custos/loaded via outboundSink (no-op if null)
    void bindOsc();            // (re)bind the OSC server to BASE+identityN, if any
    std::unique_ptr<SynthWindow> synthWindow;   // M2, message-thread only; nullptr == hidden
    std::unique_ptr<CustosOscServer> oscServer; // M3; nullptr when OSC disabled or bind failed
    std::shared_ptr<bool> aliveToken { std::make_shared<bool> (true) };   // guards deferred close callbacks against use-after-free
    int boundCount = 0;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    bool isPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosProcessor)
};
}
