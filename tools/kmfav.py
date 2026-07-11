#!/usr/bin/env python3
"""kmfav.py — seed Custos favorites over OSC from a GP VstDatabase.txt.

Favorites are machine-level and shared: push once to ANY one reachable Custos
instance (favorites/begin ... favorite* ... favorites/end) and it writes the
shared %APPDATA%/Custos/favorites.json that every instance reads.

Wire contract (docs/osc-contract.md):
  /custos/favorites/begin   ()
  /custos/favorite          (idx:int, name:str, path:str, favOrder:int,
                             gainDb:float, brand:str[optional])
  /custos/favorite* are strictly typed on the Custos side — favOrder MUST be an
  int and gainDb MUST be a float, or the entry is dropped.
  /custos/favorites/end     (count:int)  -> Custos writes the shared config

Addressing: send to 127.0.0.1:(9100 + N), N = instance id set in the Custos UI.
Custos replies (/custos/here, /custos/ack) go to the fixed hub 127.0.0.1:8000;
if Kapellmeister/relay owns :8000 we can't bind it, so verification is done by
reading %APPDATA%/Custos/favorites.json after the push (--no-listen).

VstDatabase.txt line format (under [VST_DATABASE]):
  Manufacturer | Plugin | ControlType | ParamDown | ParamUp | Trim [| FavOrder] = Path
Trim is a LINEAR gain (1.000 = unity); it is converted to dB for gainDb.

Usage:
  python kmfav.py <path-to-VstDatabase.txt> --id 9
  python kmfav.py <db> --id 9 --favorites-only   # only rows with FavOrder>=1
  python kmfav.py <db> --id 9 --dry-run           # parse + print, send nothing
Options:
  --id N            Custos instance id (port = 9100+N). Required.
  --favorites-only  send only rows that already carry a FavOrder (>=1)
  --keep-favorder   use the DB FavOrder as-is (default: re-number 1..K in file order)
  --dry-run         parse and print the plan; send no OSC
  --gap MS          ms delay between UDP sends (default 4) to avoid drops
"""
import argparse
import math
import sys
import time

from pythonosc.udp_client import SimpleUDPClient

IP = "127.0.0.1"


def trim_to_db(trim: float) -> float:
    if trim <= 0.0:
        return -120.0
    return round(20.0 * math.log10(trim), 3)


def parse_db(path):
    """Return list of dicts {brand,name,path,trim,favOrder} in file order."""
    rows = []
    in_section = False
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            s = line.strip()
            if not s or s.startswith(";"):
                continue
            if s.startswith("["):
                in_section = s.upper() == "[VST_DATABASE]"
                continue
            if not in_section or "=" not in line:
                continue
            left, vst_path = line.split("=", 1)
            vst_path = vst_path.strip()
            fields = [f.strip() for f in left.split("|")]
            if len(fields) < 6:
                continue
            brand, name = fields[0], fields[1]
            try:
                trim = float(fields[5])
            except ValueError:
                trim = 1.0
            fav = 0
            if len(fields) >= 7 and fields[6] != "":
                try:
                    fav = int(fields[6])
                except ValueError:
                    fav = 0
            rows.append({"brand": brand, "name": name, "path": vst_path,
                         "trim": trim, "favOrder": fav})
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("db")
    ap.add_argument("--id", type=int, required=True)
    ap.add_argument("--favorites-only", action="store_true")
    ap.add_argument("--keep-favorder", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--gap", type=float, default=4.0)
    a = ap.parse_args()

    rows = parse_db(a.db)
    if a.favorites_only:
        rows = [r for r in rows if r["favOrder"] >= 1]
    if not rows:
        print("[!] no rows to send", file=sys.stderr)
        return 2

    # Assign the favOrder actually sent.
    for i, r in enumerate(rows):
        r["_favOrder"] = r["favOrder"] if a.keep_favorder else (i + 1)
        r["_gainDb"] = trim_to_db(r["trim"])

    port = 9100 + a.id
    print(f">> seeding {len(rows)} favourites to Custos id={a.id} "
          f"({IP}:{port}){'  [DRY-RUN]' if a.dry_run else ''}")
    for i, r in enumerate(rows):
        print(f"   [{i:3d}] favOrder={r['_favOrder']:3d} gainDb={r['_gainDb']:+.2f} "
              f"{r['brand']} | {r['name']}")

    if a.dry_run:
        return 0

    client = SimpleUDPClient(IP, port)
    client.send_message("/custos/favorites/begin", [])
    time.sleep(a.gap / 1000.0)
    for i, r in enumerate(rows):
        client.send_message("/custos/favorite",
                            [int(i), str(r["name"]), str(r["path"]),
                             int(r["_favOrder"]), float(r["_gainDb"]), str(r["brand"])])
        time.sleep(a.gap / 1000.0)
    client.send_message("/custos/favorites/end", [int(len(rows))])
    print(f"[i] sent begin + {len(rows)} favourite(s) + end(count={len(rows)}). "
          f"Verify %APPDATA%/Custos/favorites.json (acks go to hub :8000).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
