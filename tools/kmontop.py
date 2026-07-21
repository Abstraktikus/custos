#!/usr/bin/env python3
"""kmontop.py — rig-test driver for Custos "dock on-top mode B" (no KM needed).

Custos' docked synth window has two on-top strategies (contract: /custos/window/ontop):
  Mode A (default) — forced always-on-top (today's behaviour).
  Mode B (this tool) — the docked window follows KM's foreground state.

KM normally sends /custos/window/ontop on every focus change AND every ~2 s while
alive; that state message doubles as a heartbeat. If Custos hears nothing for 5 s it
treats KM as gone and drops the window out of on-top. So a ONE-SHOT "on top" lasts
only ~5 s — to hold the window up you must keep the heartbeat going. This tool does
that for you, so you can exercise Mode B on the rig before the KM sender exists.

Addressing: sends to 127.0.0.1:(9100 + N), N = the instance id set in the Custos UI
(same scheme as kmfav.py / the KM inbound contract). --id accepts one id, a range
("1-10"), or a list ("1,3,5") so you can drive the whole rig at once.

Fire-and-forget: Custos does not reply to these verbs, so nothing is listened for.
Localhost UDP only — this is a developer/rig utility, not part of the plugin.

TYPICAL RIG SESSION (one instance, e.g. N=10):
  # 1. put a docked window on screen to observe (show + a fit area at x y w h):
  python kmontop.py --id 10 dock 200 200 900 600
  # 2. hold it on top with a live 2 s heartbeat, Ctrl-C to stop:
  python kmontop.py --id 10 hold on
  #    -> now Alt-Tab away: it should stay up only while you send "on".
  # 3. drop it behind (KM not foreground):
  python kmontop.py --id 10 hold off
  # 4. watchdog fail-safe: send ONCE and watch it fall behind after ~5 s:
  python kmontop.py --id 10 once on
  # 5. hand control back to Mode A (always-on-top, today's behaviour):
  python kmontop.py --id 10 handoff
  # 6. realistic follow: send 1 while a chosen window is frontmost, else 0:
  python kmontop.py --id 10 auto --anchor Notepad

Commands:
  dock X Y W H [MARGIN]   show the synth window + fit it into the area (fit=1) so
                          there is a docked window Mode B can act on. MARGIN px, default 0.
  show | hide             open / close the borderless synth window.
  once on|off             send /custos/window/ontop 1|0 exactly once (no heartbeat) —
                          use "once on" to watch the 5 s watchdog drop the window.
  hold on|off             send 1|0 every --interval s until Ctrl-C (the Mode B heartbeat).
  handoff                 send -1 once: hands control back to Mode A (always-on-top).
  auto                    poll the Windows foreground window every --poll s; send 1 while
                          its title contains --anchor, else 0; always (re)send at least
                          every --interval s so the heartbeat never starves. Ctrl-C stops.

Options:
  --id IDS         instance id(s): "10", "1-10", or "1,3,5". Required.
  --interval SEC   heartbeat period (default 2.0). MUST stay < 5 s (Custos' watchdog).
  --poll SEC       auto: foreground poll period (default 0.4).
  --anchor TEXT    auto: substring of the "KM stand-in" window title (default "Notepad").
  --ip ADDR        target host (default 127.0.0.1).
"""
import argparse
import sys
import time

from pythonosc.udp_client import SimpleUDPClient

ONTOP = "/custos/window/ontop"
STATE = {"on": 1, "off": 0}


def parse_ids(spec):
    """"10" -> [10]; "1-10" -> [1..10]; "1,3,5" -> [1,3,5]."""
    ids = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            lo, hi = part.split("-", 1)
            ids.extend(range(int(lo), int(hi) + 1))
        elif part:
            ids.append(int(part))
    if not ids:
        raise ValueError(f"no ids in {spec!r}")
    for n in ids:
        if not (1 <= n <= 15):
            raise ValueError(f"id {n} out of range 1..15")
    return ids


def send_ontop(clients, state):
    for n, c in clients:
        c.send_message(ONTOP, int(state))  # int32 — Custos rejects a non-int arg


def stamp():
    return time.strftime("%H:%M:%S")


def foreground_title():
    """Title of the current Windows foreground window ('' if none/unavailable)."""
    import ctypes  # Windows-only; imported lazily so the other commands stay portable
    u = ctypes.windll.user32
    hwnd = u.GetForegroundWindow()
    if not hwnd:
        return ""
    n = u.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    u.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


def main():
    ap = argparse.ArgumentParser(
        description="Rig-test driver for Custos dock on-top Mode B.")
    ap.add_argument("cmd", choices=["dock", "show", "hide", "once", "hold", "handoff", "auto"])
    ap.add_argument("rest", nargs="*", help="command args (see the module docstring)")
    ap.add_argument("--id", dest="ids", required=True, help='instance id(s): "10", "1-10", "1,3,5"')
    ap.add_argument("--interval", type=float, default=2.0, help="heartbeat period s (must be < 5)")
    ap.add_argument("--poll", type=float, default=0.4, help="auto: foreground poll period s")
    ap.add_argument("--anchor", default="Notepad", help='auto: "KM" window title substring')
    ap.add_argument("--ip", default="127.0.0.1")
    a = ap.parse_args()

    ids = parse_ids(a.ids)
    clients = [(n, SimpleUDPClient(a.ip, 9100 + n)) for n in ids]
    targets = ", ".join(f"N{n}:{9100 + n}" for n in ids)

    if a.interval >= 5.0 and a.cmd in ("hold", "auto"):
        print(f"!! --interval {a.interval}s >= 5s watchdog: the window WILL flicker/drop between beats",
              file=sys.stderr)

    if a.cmd == "dock":
        if len(a.rest) not in (4, 5):
            ap.error("dock needs X Y W H [MARGIN]")
        x, y, w, h = (int(v) for v in a.rest[:4])
        margin = int(a.rest[4]) if len(a.rest) == 5 else 0
        for n, c in clients:
            c.send_message("/custos/window", "show")
            # x,y,w,h treated as an available AREA; fit=1 docks the editor into it (movable=0, clamp=0).
            c.send_message("/custos/window/rect", [x, y, w, h, 0, 0, 1, margin])
        print(f"[{stamp()}] dock -> ({targets})  area=({x},{y},{w},{h}) margin={margin}")
        print("   note: still Mode A until you send an ontop state (hold/once/auto).")
        return

    if a.cmd in ("show", "hide"):
        for n, c in clients:
            c.send_message("/custos/window", a.cmd)
        print(f"[{stamp()}] window {a.cmd} -> ({targets})")
        return

    if a.cmd == "handoff":
        send_ontop(clients, -1)
        print(f"[{stamp()}] ontop -1 (hands off -> Mode A / always-on-top) -> ({targets})")
        return

    if a.cmd == "once":
        if not a.rest or a.rest[0] not in STATE:
            ap.error("once needs on|off")
        s = STATE[a.rest[0]]
        send_ontop(clients, s)
        print(f"[{stamp()}] ontop {s} (once, no heartbeat) -> ({targets})")
        if s == 1:
            print("   watch the docked window: it should fall behind after ~5 s (watchdog).")
        return

    if a.cmd == "hold":
        if not a.rest or a.rest[0] not in STATE:
            ap.error("hold needs on|off")
        s = STATE[a.rest[0]]
        print(f"[{stamp()}] hold ontop {s} every {a.interval}s -> ({targets})   (Ctrl-C to stop)")
        try:
            while True:
                send_ontop(clients, s)
                print(f"[{stamp()}] .. beat {s}", flush=True)
                time.sleep(a.interval)
        except KeyboardInterrupt:
            print(f"\n[{stamp()}] stopped. Window stays in Mode B; send 'handoff' for Mode A, "
                  f"or it drops behind ~5 s after this last beat.")
        return

    if a.cmd == "auto":
        print(f"[{stamp()}] auto-follow: ontop 1 while a window titled ~'{a.anchor}' is frontmost, "
              f"else 0; heartbeat every {a.interval}s -> ({targets})   (Ctrl-C to stop)")
        last_state = None
        last_beat = 0.0
        try:
            while True:
                fg = foreground_title()
                want = 1 if a.anchor.lower() in fg.lower() else 0
                now = time.monotonic()
                if want != last_state or (now - last_beat) >= a.interval:
                    send_ontop(clients, want)
                    last_beat = now
                    if want != last_state:
                        print(f"[{stamp()}] foreground={fg!r} -> ontop {want}", flush=True)
                    last_state = want
                time.sleep(a.poll)
        except KeyboardInterrupt:
            print(f"\n[{stamp()}] stopped. Send 'handoff' to return to Mode A.")
        return


if __name__ == "__main__":
    main()
