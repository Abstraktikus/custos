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
    return { Command::Unknown, {} };
}

CustosOscServer::CustosOscServer (CustosProcessor& p) : proc (p)
{
    ackReady = ackSender.connect (CUSTOS_ACK_HOST, CUSTOS_ACK_PORT);
    proc.outboundSink = [this] (const juce::OSCMessage& m) { if (ackReady) ackSender.send (m); };
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
                                   proc.boundParamCount(), oscPortForIdentity (currentN)));
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
        case Command::Unknown:
        default:
            ack ("error unknown " + msg.getAddressPattern().toString());
            break;
    }
}
}
