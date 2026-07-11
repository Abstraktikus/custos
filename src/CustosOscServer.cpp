#include "CustosOscServer.h"
#include "CustosProcessor.h"
#include "OscContract.h"
#include "HostTrace.h"

#ifndef CUSTOS_OSC_PORT
 #define CUSTOS_OSC_PORT 9100
#endif
#ifndef CUSTOS_ACK_HOST
 #define CUSTOS_ACK_HOST "127.0.0.1"
#endif
#ifndef CUSTOS_ACK_PORT
 #define CUSTOS_ACK_PORT 8000
#endif
// GP's OSC-in port: Custos mirrors the Voice-Selector feedback (/custos/browsing + /custos/loaded) here
// so the GP-Script can drive browsing autonomously, in addition to the KM hub on CUSTOS_ACK_PORT.
#ifndef CUSTOS_GP_FEEDBACK_PORT
 #define CUSTOS_GP_FEEDBACK_PORT 54344
#endif

namespace custos
{
Command parseCommand (const juce::OSCMessage& msg)
{
    const auto addr = msg.getAddressPattern().toString();
    if (addr == "/custos/load")
    {
        if (msg.size() >= 1 && msg[0].isString())
            return { Command::Load, msg[0].getString() };
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/clear")
        return { Command::Clear, {} };
    if (addr == "/custos/hello")
        return { Command::Hello, {} };
    if (addr == "/custos/params")
    {
        if (msg.size() >= 2 && msg[0].isInt32() && msg[1].isInt32())
            return { Command::Params, {}, msg[0].getInt32(), msg[1].getInt32() };
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/volume")
    {
        if (msg.size() >= 1 && msg[0].isFloat32())
            return { Command::Volume, {}, 0, 0, msg[0].getFloat32() };
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/mainlr")   // fold all inner outputs onto stereo Out 1 (audio setting; sibling of volume)
    { Command c; c.kind = Command::MainLR;
      if (msg.size() > 0 && msg[0].isInt32()) c.mainLROn = (msg[0].getInt32() != 0); return c; }
    if (addr == "/custos/mainlr/query")
        return { Command::MainLRQuery, {} };
    if (addr == "/custos/learn/start")
        return { Command::LearnStart, {} };
    if (addr == "/custos/learn/stop")
        return { Command::LearnStop, {} };
    if (addr == "/custos/favorites/begin")
        return { Command::FavBegin, {} };
    if (addr == "/custos/favorite")
    {
        if (msg.size() >= 5 && msg[0].isInt32() && msg[1].isString() && msg[2].isString()
            && msg[3].isInt32() && msg[4].isFloat32())
        {
            Command c;
            c.kind = Command::FavEntry;
            c.fav.name     = msg[1].getString();
            c.fav.path     = msg[2].getString();
            c.fav.favOrder = msg[3].getInt32();
            c.fav.gainDb   = msg[4].getFloat32();
            if (msg.size() >= 6 && msg[5].isString())   // brand optional (back-compat)
                c.fav.brand = msg[5].getString();
            if (msg.size() >= 7 && msg[6].isInt32())    // slots (param count) optional 7th arg
                c.fav.slots = msg[6].getInt32();
            if (msg.size() >= 8 && msg[7].isString())   // controlType optional 8th arg
                c.fav.controlType = msg[7].getString();
            if (msg.size() >= 9 && msg[8].isInt32())    // paramDown optional 9th arg
                c.fav.paramDown = msg[8].getInt32();
            if (msg.size() >= 10 && msg[9].isInt32())   // paramUp optional 10th arg
                c.fav.paramUp = msg[9].getInt32();
            return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/favorites/end")
    {
        if (msg.size() >= 1 && msg[0].isInt32())
        {
            Command c; c.kind = Command::FavEnd; c.count = msg[0].getInt32(); return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/window")
    {
        if (msg.size() >= 1 && msg[0].isString())
        {
            const auto m = msg[0].getString();
            if (m == "show")   return { Command::WindowShow, {} };     // borderless synth window
            if (m == "titled") return { Command::WindowTitled, {} };   // titled synth window (native title bar)
            if (m == "hide")   return { Command::WindowHide, {} };
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/window/rect")
    {
        if (msg.size() >= 5 && msg[0].isInt32() && msg[1].isInt32() && msg[2].isInt32()
            && msg[3].isInt32() && msg[4].isInt32())
        {
            Command c;
            c.kind = Command::WindowRect;
            c.rx = msg[0].getInt32(); c.ry = msg[1].getInt32();
            c.rw = msg[2].getInt32(); c.rh = msg[3].getInt32();
            c.movable = msg[4].getInt32() != 0;
            if (msg.size() >= 6 && msg[5].isInt32())   // clamp optional (back-compat): 1 = config phase
                c.clamp = msg[5].getInt32() != 0;
            return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/midi/route")
    {
        if (msg.size() == 16)
        {
            Command c; c.kind = Command::MidiRoute;
            for (int i = 0; i < 16; ++i)
            {
                if (! msg[i].isInt32()) return { Command::Unknown, {} };
                c.route[(size_t) i] = msg[i].getInt32();
            }
            return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/midi/query")
        return { Command::MidiQuery, {} };
    if (addr == "/custos/instrument/next" || addr == "/custos/instrument/prev")
    {
        Command c; c.kind = (addr.endsWith ("next")) ? Command::BrowseNext : Command::BrowsePrev;
        if (msg.size() >= 1 && msg[0].isInt32()) c.scope = msg[0].getInt32();
        return c;
    }
    if (addr == "/custos/instrument/set")
    {
        if (msg.size() >= 1 && msg[0].isInt32())
        {
            Command c; c.kind = Command::BrowseSet; c.count = msg[0].getInt32(); return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/instrument/load")
    {
        if (msg.size() >= 1 && msg[0].isString())
            return { Command::InstrumentLoad, msg[0].getString() };
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/patch/next") { Command c; c.kind = Command::PatchNext; return c; }
    if (addr == "/custos/patch/prev") { Command c; c.kind = Command::PatchPrev; return c; }
    if (addr == "/custos/preset/setroot")
    { Command c; c.kind = Command::PresetSetRoot;
      if (msg.size() > 0 && msg[0].isString()) c.rootPath = msg[0].getString(); return c; }
    if (addr == "/custos/preset/queryroot") { Command c; c.kind = Command::PresetQueryRoot; return c; }
    if (addr == "/custos/preset/save")
    { Command c; c.kind = Command::PresetSave;
      if (msg.size() > 0 && msg[0].isString()) c.presetName = msg[0].getString(); return c; }
    if (addr == "/custos/preset/list")   { Command c; c.kind = Command::PresetList; return c; }
    if (addr == "/custos/preset/next")   { Command c; c.kind = Command::PresetNext; return c; }
    if (addr == "/custos/preset/prev")   { Command c; c.kind = Command::PresetPrev; return c; }
    if (addr == "/custos/preset/set")
    { Command c; c.kind = Command::PresetSet;
      if (msg.size() > 0 && msg[0].isInt32()) c.presetIndex = msg[0].getInt32(); return c; }
    if (addr == "/custos/preset/load")
    { Command c; c.kind = Command::PresetLoad;
      if (msg.size() > 0 && msg[0].isString()) c.presetName = msg[0].getString();
      else if (msg.size() > 0 && msg[0].isInt32()) c.presetIndex = msg[0].getInt32(); return c; }
    if (addr == "/custos/preset/rename")
    { Command c; c.kind = Command::PresetRename;
      if (msg.size() > 1 && msg[0].isString() && msg[1].isString())
        { c.presetName = msg[0].getString(); c.presetNewName = msg[1].getString(); } return c; }
    if (addr == "/custos/preset/delete")
    { Command c; c.kind = Command::PresetDelete;
      if (msg.size() > 0 && msg[0].isString()) c.presetName = msg[0].getString(); return c; }
    return { Command::Unknown, {} };
}

CustosOscServer::CustosOscServer (CustosProcessor& p) : proc (p)
{
    ackReady = ackSender.connect (CUSTOS_ACK_HOST, CUSTOS_ACK_PORT);          // KM hub :8000
    gpReady  = gpSender.connect  (CUSTOS_ACK_HOST, CUSTOS_GP_FEEDBACK_PORT);  // GP OSC-in :54344
    proc.outboundSink = [this] (const juce::OSCMessage& m)
    {
        const auto addr = m.getAddressPattern().toString();
        if (ackReady)
        {
            ackSender.send (m);   // everything to the KM hub (unchanged)
            trace ("N" + juce::String (currentN) + "  TX " + addr + " -> :" + juce::String (CUSTOS_ACK_PORT));
        }
        maybeMirrorToGp (m);      // GP :54344, gated by gpMirrorsFeedback (browse/loaded/here/error-ack)
    };
    proc.setFavorites (loadInstrumentsWithSelfHeal (juce::File (proc.presetRoot()),
                                                    instrumentsConfigFile(), favoritesConfigFile()));
}

CustosOscServer::~CustosOscServer()
{
    proc.outboundSink = nullptr;
    receiver.removeListener (this);
    receiver.disconnect();
    ackSender.disconnect();
    gpSender.disconnect();
}

bool CustosOscServer::bindToIdentity (int n)
{
    receiver.removeListener (this);
    receiver.disconnect();
    currentN = n;

    const int port = oscPortForIdentity (n);
    if (port == 0)
        return false;                      // unassigned / out of range -> no OSC-in

    if (! receiver.connect (port))
    {
        juce::Logger::writeToLog ("Custos: OSC port " + juce::String (port)
                                  + " in use (N collision, N=" + juce::String (n) + ")");
        return false;                      // N collision -> UI shows a warning; no auto-fallback
    }

    receiver.addListener (this);
    announceHere();
    return true;
}

void CustosOscServer::maybeMirrorToGp (const juce::OSCMessage& m)
{
    if (! gpReady)
        return;
    const auto addr = m.getAddressPattern().toString();
    juce::String ackText;
    if (addr == "/custos/ack" && m.size() >= 2 && m[1].isString())
        ackText = m[1].getString();
    if (! gpMirrorsFeedback (addr, ackText))
        return;
    gpSender.send (m);
    trace ("N" + juce::String (currentN) + "  TX " + addr + " -> :" + juce::String (CUSTOS_GP_FEEDBACK_PORT));
}

void CustosOscServer::announceHere()
{
    const auto m = buildHere (currentN, proc.modeString(), proc.innerSynthName(),
                              proc.boundParamCount(), oscPortForIdentity (currentN), proc.facadeSize());
    if (ackReady)
        ackSender.send (m);
    maybeMirrorToGp (m);   // GP liveness/discovery for a KM-less flow
}

void CustosOscServer::ack (const juce::String& text)
{
    const auto m = buildAck (currentN, text);
    if (ackReady)
        ackSender.send (m);
    maybeMirrorToGp (m);   // mirrors error-acks only (success = /custos/loaded)
}

void CustosOscServer::oscMessageReceived (const juce::OSCMessage& msg)
{
    trace ("N" + juce::String (currentN) + "  RX " + msg.getAddressPattern().toString());
    const auto cmd = parseCommand (msg);
    switch (cmd.kind)
    {
        case Command::Load:
        {
            const auto r = proc.load (cmd.path);
            ack (r.ok ? ("loaded " + cmd.path + " count=" + juce::String (r.innerCount))
                      : r.message);
            break;
        }
        case Command::Clear:
            proc.clear();
            ack ("cleared");
            break;
        case Command::Hello:
            announceHere();
            break;
        case Command::Params:
            proc.dumpParams (cmd.start, cmd.count);
            break;
        case Command::Volume:
            proc.setVolumeDb (cmd.gainDb);
            break;
        case Command::MainLR:
            proc.setMainLROnly (cmd.mainLROn);
            proc.emitMainLR();   // confirm the applied fold
            break;
        case Command::MainLRQuery:
            proc.emitMainLR();
            break;
        case Command::LearnStart:
            proc.startLearn();
            break;
        case Command::LearnStop:
            proc.stopLearn ("stop");
            break;
        case Command::FavBegin:
            proc.favoritesBegin();
            break;
        case Command::FavEntry:
            proc.favoritesAdd (cmd.fav);
            break;
        case Command::FavEnd:
            proc.favoritesEnd();
            proc.persistFavorites();   // unified data root; failure surfaced via /custos/preset/error
            break;
        case Command::WindowShow:
            proc.showSynthWindow();
            break;
        case Command::WindowTitled:
            proc.showSynthWindowTitled();
            break;
        case Command::WindowHide:
            proc.hideSynthWindow();
            break;
        case Command::WindowRect:
            proc.setSynthWindowRect (cmd.rx, cmd.ry, cmd.rw, cmd.rh, cmd.movable, cmd.clamp);
            break;
        case Command::MidiRoute:
        {
            std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = cmd.route[(size_t) i];
            proc.setMidiRoute (r);
            proc.emitMidiRoute();   // confirm the applied map
            break;
        }
        case Command::BrowseNext:
            proc.browseInstrument (+1, cmd.scope);
            break;
        case Command::BrowsePrev:
            proc.browseInstrument (-1, cmd.scope);
            break;
        case Command::BrowseSet:
            proc.setBrowseIndex (cmd.count);
            break;
        case Command::InstrumentLoad:
        {
            const bool ok = proc.loadByName (cmd.path);
            if (! ok) ack ("error unknown instrument " + cmd.path);
            break;   // success is conveyed by /custos/loaded (emitted inside load())
        }
        case Command::PatchNext:
            proc.patchNext();
            break;
        case Command::PatchPrev:
            proc.patchPrev();
            break;
        case Command::MidiQuery:
            proc.emitMidiRoute();
            break;
        case Command::PresetSetRoot:
            proc.setPresetRoot (cmd.rootPath);
            break;
        case Command::PresetQueryRoot:
            proc.emitPresetRoot();
            break;
        case Command::PresetSave:
            proc.savePreset (cmd.presetName);
            break;
        case Command::PresetList:
            emitPresetList();
            break;
        case Command::PresetLoad:
            if (cmd.presetIndex >= 0) proc.loadPresetAt (cmd.presetIndex);
            else                      proc.loadPresetByName (cmd.presetName);
            break;
        case Command::PresetNext:
            proc.presetNext();
            break;
        case Command::PresetPrev:
            proc.presetPrev();
            break;
        case Command::PresetSet:
            proc.presetSet (cmd.presetIndex);
            break;
        case Command::PresetRename:
            proc.renamePreset (cmd.presetName, cmd.presetNewName);
            break;
        case Command::PresetDelete:
            proc.deletePreset (cmd.presetName);
            break;
        case Command::Unknown:
        default:
            ack ("error unknown " + msg.getAddressPattern().toString());
            break;
    }
}

void CustosOscServer::emitPresetList()
{
    if (! ackReady) return;
    const auto names = proc.listPresets();
    juce::OSCMessage m ("/custos/preset/list");
    m.addInt32 (proc.identity());
    m.addInt32 ((int) names.size());
    for (const auto& n : names) m.addString (n);
    ackSender.send (m);
}
}
