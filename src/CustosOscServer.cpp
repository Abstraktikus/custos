#include "CustosOscServer.h"
#include "CustosProcessor.h"
#include "OscContract.h"

#ifndef CUSTOS_OSC_PORT
 #define CUSTOS_OSC_PORT 9100
#endif
#ifndef CUSTOS_ACK_HOST
 #define CUSTOS_ACK_HOST "127.0.0.1"
#endif
#ifndef CUSTOS_ACK_PORT
 #define CUSTOS_ACK_PORT 8000
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
    if (addr == "/custos/instrument/next")
        return { Command::BrowseNext, {} };
    if (addr == "/custos/instrument/prev")
        return { Command::BrowsePrev, {} };
    if (addr == "/custos/instrument/set")
    {
        if (msg.size() >= 1 && msg[0].isInt32())
        {
            Command c; c.kind = Command::BrowseSet; c.count = msg[0].getInt32(); return c;
        }
        return { Command::Unknown, {} };
    }
    return { Command::Unknown, {} };
}

CustosOscServer::CustosOscServer (CustosProcessor& p) : proc (p)
{
    ackReady = ackSender.connect (CUSTOS_ACK_HOST, CUSTOS_ACK_PORT);
    proc.outboundSink = [this] (const juce::OSCMessage& m) { if (ackReady) ackSender.send (m); };
    proc.setFavorites (readFavorites (favoritesConfigFile()));   // boot-load the shared machine config
}

CustosOscServer::~CustosOscServer()
{
    proc.outboundSink = nullptr;
    receiver.removeListener (this);
    receiver.disconnect();
    ackSender.disconnect();
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

void CustosOscServer::announceHere()
{
    if (ackReady)
        ackSender.send (buildHere (currentN, proc.modeString(), proc.innerSynthName(),
                                   proc.boundParamCount(), oscPortForIdentity (currentN), proc.facadeSize()));
}

void CustosOscServer::ack (const juce::String& text)
{
    if (ackReady)
        ackSender.send (buildAck (currentN, text));
}

void CustosOscServer::oscMessageReceived (const juce::OSCMessage& msg)
{
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
        case Command::FavBegin:
            proc.favoritesBegin();
            break;
        case Command::FavEntry:
            proc.favoritesAdd (cmd.fav);
            break;
        case Command::FavEnd:
            proc.favoritesEnd();
            writeFavorites (favoritesConfigFile(), proc.getFavorites());   // shared machine config
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
            proc.browseInstrument (+1);
            break;
        case Command::BrowsePrev:
            proc.browseInstrument (-1);
            break;
        case Command::BrowseSet:
            proc.setBrowseIndex (cmd.count);
            break;
        case Command::MidiQuery:
            proc.emitMidiRoute();
            break;
        case Command::Unknown:
        default:
            ack ("error unknown " + msg.getAddressPattern().toString());
            break;
    }
}
}
