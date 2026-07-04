#include "CustosOscServer.h"
#include "CustosProcessor.h"

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
    return { Command::Unknown, {} };
}

CustosOscServer::CustosOscServer (CustosProcessor& p) : proc (p)
{
    if (receiver.connect (CUSTOS_OSC_PORT))
        receiver.addListener (this);
    else
        juce::Logger::writeToLog ("Custos: OSC receiver could not bind port "
                                  + juce::String (CUSTOS_OSC_PORT));

    ackReady = ackSender.connect (CUSTOS_ACK_HOST, CUSTOS_ACK_PORT);
}

CustosOscServer::~CustosOscServer()
{
    receiver.removeListener (this);
    receiver.disconnect();
    ackSender.disconnect();
}

void CustosOscServer::ack (const juce::String& text)
{
    if (ackReady)
        ackSender.send ("/custos/ack", text);
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
        case Command::Unknown:
        default:
            ack ("error unknown " + msg.getAddressPattern().toString());
            break;
    }
}
}
