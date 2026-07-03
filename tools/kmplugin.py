#!/usr/bin/env python3
"""kmplugin.py — drive the GP per-plugin OSC harness from the outside.

Sends a /KM/Plugin/* (or /GH/RequestStatus) command to GP's OSC-in port and
listens on the hub port for the replies, printing everything received.

Self-contained: binds the hub port itself, so it does NOT need Kapellmeister or
the SwitchGPToGlobal relay running. If the hub port is already bound (KM/relay
up), pass --no-listen and read gp-trace-latest.log instead (PCount/PGet/PSet
echo to /GP/Trace which that log captures).

Usage:
  python kmplugin.py status
  python kmplugin.py list
  python kmplugin.py pcount "Custos"
  python kmplugin.py plist  "Custos" 0 1024
  python kmplugin.py pget   "Custos" 0
  python kmplugin.py pset   "Custos" 0 0.8
Options:
  --wait N        seconds to listen for replies (default 2.5)
  --cmd-port P    GP OSC-in port (default 54344)
  --hub-port P    hub reply port to bind (default 8000)
  --no-listen     just send; don't bind the hub port
"""
import argparse
import sys
import threading
import time

from pythonosc.udp_client import SimpleUDPClient
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import ThreadingOSCUDPServer

GP_IP = "127.0.0.1"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["status", "list", "pcount", "plist", "pget", "pset"])
    ap.add_argument("rest", nargs="*")
    ap.add_argument("--wait", type=float, default=2.5)
    ap.add_argument("--cmd-port", type=int, default=54344)
    ap.add_argument("--hub-port", type=int, default=8000)
    ap.add_argument("--no-listen", action="store_true")
    a = ap.parse_args()

    # Build the OSC address + typed args for the chosen command.
    if a.cmd == "status":
        addr, args = "/GH/RequestStatus", []
    elif a.cmd == "list":
        addr, args = "/KM/Plugin/List", []
    elif a.cmd == "pcount":
        addr, args = "/KM/Plugin/PCount", [str(a.rest[0])]
    elif a.cmd == "plist":
        addr, args = "/KM/Plugin/PList", [str(a.rest[0]), int(a.rest[1]), int(a.rest[2])]
    elif a.cmd == "pget":
        addr, args = "/KM/Plugin/PGet", [str(a.rest[0]), int(a.rest[1])]
    elif a.cmd == "pset":
        addr, args = "/KM/Plugin/PSet", [str(a.rest[0]), int(a.rest[1]), float(a.rest[2])]

    server = None
    got = []
    if not a.no_listen:
        disp = Dispatcher()

        def cb(address, *osc_args):
            got.append((address, list(osc_args)))
            print(f"<< {address}  {list(osc_args)}", flush=True)

        disp.set_default_handler(cb)
        try:
            server = ThreadingOSCUDPServer((GP_IP, a.hub_port), disp)
        except OSError as e:
            print(f"[!] could not bind hub :{a.hub_port} ({e}). "
                  f"Something else owns it (KM/relay?). Re-run with --no-listen "
                  f"and read gp-trace-latest.log for /GP/Trace echoes.", file=sys.stderr)
            return 2
        threading.Thread(target=server.serve_forever, daemon=True).start()

    print(f">> {addr}  {args}  -> {GP_IP}:{a.cmd_port}", flush=True)
    SimpleUDPClient(GP_IP, a.cmd_port).send_message(addr, args)

    if server is not None:
        time.sleep(a.wait)
        server.shutdown()
        print(f"[i] received {len(got)} reply message(s) in {a.wait}s", flush=True)
        if not got:
            print("[i] no replies — is GP running with the Global Rackspace + harness? "
                  "is the plugin name correct / loaded in a Global slot?", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
