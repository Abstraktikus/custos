#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Config.h"
#include "FacadeParameter.h"
#include "InnerBinding.h"
#include "FavoritesStore.h"
#include <juce_osc/juce_osc.h>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <array>

namespace custos
{
class SynthWindow;       // forward declaration (unique_ptr member; defined in SynthWindow.h)
class CustosOscServer;   // forward declaration (unique_ptr member; defined in CustosOscServer.h)

struct CommandResult { bool ok = false; int innerCount = 0; juce::String message; };

// Which window (if any) to keep always-on-top.
enum OnTopMode { OnTopOff, OnTopCustos, OnTopInstrument };

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

    // F5: uniform output trim. dB -> linear; applied in processBlock. Message thread.
    void setVolumeDb (float db);
    float volumeDb() const noexcept { return juce::Decibels::gainToDecibels (masterGain.load()); }

    // F4 favourites (message thread). Push: begin -> add* -> end (commits + sorts). setFavorites for boot-load.
    void favoritesBegin();
    void favoritesAdd (const Favorite& f);
    void favoritesEnd();
    void setFavorites (std::vector<Favorite> favs);
    const std::vector<Favorite>& getFavorites() const noexcept { return favorites; }
    void loadFavorite (int index);   // loads getFavorites()[index]

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

    // Full parameter count of the loaded inner synth (0 if none). May exceed facadeSize():
    // then the top (innerParamTotal - facadeSize) params are unbound / uncontrollable.
    int innerParamTotal() const noexcept { return inner != nullptr ? (int) inner->getParameters().size() : 0; }

    // M2 synth-window API — message thread only.
    void toggleSynthWindow();
    void showSynthWindow();
    void hideSynthWindow();
    bool isSynthWindowVisible() const noexcept { return synthWindow != nullptr; }
    bool hasInnerSynth() const noexcept { return inner != nullptr; }
    juce::String innerSynthName() const;
    juce::String currentPath() const { return currentSynthPath; }   // path of the loaded synth ("" = none)

    // Keep-on-top mode: none, this (the Custos editor window), or the inner-synth window.
    void setOnTopMode (OnTopMode mode);
    OnTopMode getOnTopMode() const noexcept { return onTopMode; }

    // F3/F6: show (if needed) + place the synth window at a PHYSICAL-pixel rect (DPI-mapped). Message thread.
    // clamp = constrain the rect to the monitor work area (config phase; keeps the drag borders reachable).
    void setSynthWindowRect (int x, int y, int w, int h, bool movable, bool clamp = false);

    // The synth window's current bounds in PHYSICAL pixels (empty if hidden). For the editor's rect readout.
    juce::Rectangle<int> currentSynthWindowPhysical() const;

    // MIDI channel routing (message thread writes, audio thread reads). Persisted (state v3).
    void setMidiRoute (const std::array<int, 16>& route);   // values clamped to 0..16
    std::array<int, 16> getMidiRoute() const;

protected:
    std::vector<FacadeParameter*> facade;   // non-owning: AudioProcessor owns via addParameter

private:
    void refreshEditor();            // refresh the active CustosEditor (if any) after a window state change
    void updateEditorRectReadout();  // lightweight: update only the editor's x/y/w/h readout (live during drag)
    void emitWindowRect();           // send /custos/window/rect position feedback to KM (no-op if sink null)
    bool synthWindowMovable = false; // last movable flag applied to the synth window (for feedback)

    std::unique_ptr<juce::AudioProcessor> inner;
    mutable juce::SpinLock swapLock;  // guards the inner-pointer swap vs the audio thread (also getTailLengthSeconds)
    juce::String   currentSynthPath;  // path of the loaded synth ("" = none); persisted
    int  identityN = 0;        // operator-set; 0 = unassigned. Persisted (CUS v2).
    bool lastBindOk = false;   // did the OSC receiver bind BASE+N?
    std::atomic<float> masterGain { 1.0f };   // F5 linear output trim; 1.0 = unity (0 dB)
    std::vector<Favorite> favorites;       // committed, sorted by favOrder
    std::vector<Favorite> favAccumulator;  // during a begin..end push
    void applyVolumeDefault (const juce::String& path);   // set the trim from the matching favourite (unity if none)
    void emitLoaded();         // send /custos/loaded via outboundSink (no-op if null)
    void bindOsc();            // (re)bind the OSC server to BASE+identityN, if any
    std::unique_ptr<SynthWindow> synthWindow;   // M2, message-thread only; nullptr == hidden
    OnTopMode onTopMode = OnTopOff;             // keep-on-top target
    std::unique_ptr<CustosOscServer> oscServer; // M3; nullptr when OSC disabled or bind failed
    std::shared_ptr<bool> aliveToken { std::make_shared<bool> (true) };   // guards deferred close callbacks against use-after-free
    int boundCount = 0;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    bool isPrepared = false;
    std::array<std::atomic<std::uint8_t>, 16> midiRoute;   // target output per input channel; 0 = drop
    juce::MidiBuffer routeScratch;                          // reused by applyMidiRoute (no RT alloc)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustosProcessor)
};
}
