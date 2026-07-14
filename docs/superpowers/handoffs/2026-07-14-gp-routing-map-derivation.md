# Handoff → GP-Script session: derive HumanRoutingMap live from Custos

**Date:** 2026-07-14
**Custos side:** DONE — PR Abstraktikus/custos#15 (branch `feat/gp-mirror-midi-route`). `/custos/midi/route`
is now mirrored to GP's OSC-in `127.0.0.1:54344` (was KM-hub-only). Needs the new Custos build deployed
to the rig before the GP side can be tested live.
**GP side:** described below — lives entirely in the GP-Script repo; no Custos change required.

## Goal

The GP-Script's `HumanRoutingMap` (channel → synth) should be **derived live from Custos' real
`MidiRoute`** instead of being maintained statically in the script. That removes any drift between GP's
model and Custos' actual per-instance routing. Per the hard rule — **GP + Custos must perform live
without Kapellmeister** — this derivation lives in GP and pulls its data **directly from Custos, not via
KM**.

## What Custos now gives you (for free)

- **Instance identity `N`** (1..15) is set by the operator in each Custos UI and lives at UDP port
  `9100+N`. You already discover instances via `/custos/hello` → `/custos/here [N, protoVer, mode,
  inner, boundCount, port, facadeCap]` on `:54344`.
- **Route report:** `/custos/midi/route [N, t1..t16]` (17 args, leading `N` marks it a report). Each
  `t_i` is the output-channel remap for **input** channel `i` (positional, `t1` = input ch 1):
  `0` = that channel is dropped, `1..16` = remapped to that output channel.
- This report now arrives at **GP `:54344`** in two situations:
  1. **on request** — you send `/custos/midi/query` to `9100+N`, Custos replies with `/custos/midi/route`;
  2. **automatically** — after **every** `/custos/midi/route` *command* applied to that instance (whoever
     set it — KM, GP, or the Custos editor), Custos echoes the applied map. So you stay synced on every
     live route change with **no polling**.

## What to build in the GP-Script

1. **Listen** on GP OSC-in `:54344` for `/custos/midi/route` messages whose **first arg is an int `N`**
   and which carry **17 args total** (leading `N` + 16 targets). (16-arg `/custos/midi/route` is the
   inbound *command* form and won't arrive at GP; only guard defensively.)
2. **Derive** the map. Each Custos instance `N` hosts exactly one inner synth. For that instance:
   - for each input channel `c` in 1..16: `route[c-1] != 0` ⇒ **channel `c` drives synth(N)**.
   - GP only needs the `!= 0` test for the HumanRoutingMap; the actual remap target value is Custos'
     internal business (per-input remap), not GP's.
   - Merge across all live instances to get the full channel → synth(s) map.
3. **Populate on song-load.** After the on-song instance populate, send `/custos/midi/query` to each
   live instance's `9100+N` and build the map from the replies. (You already know each `N` from the
   `/custos/here` discovery flow.)
4. **Stay synced.** Keep updating the map from the automatic `/custos/midi/route` echoes — no periodic
   re-query needed. Key the map by `N` so a later report for the same `N` replaces that instance's row.

## Notes / gotchas

- **Write direction is unchanged and already KM-free:** GP can *set* a route by sending
  `/custos/midi/route [t1..t16]` (16 ints, no `N`) to `9100+N`. Custos applies it and echoes the
  17-arg report back — so a write-then-verify needs no polling either.
- **Host assumption:** Custos and GP are on the same host; the mirror target `127.0.0.1:54344` is
  hard-coded in Custos. Already true on Martin's rig.
- **KM independence:** none of this needs KM running. With KM up, KM still gets the same reports on
  `:8000`; GP is just an additional recipient. With KM down, the GP path is unaffected.
- **Empty instances:** an instance with no inner synth still has a (default/identity) route and will
  answer the query. Decide GP-side whether to include an instance whose `/custos/here.inner` is empty
  in the map (recommend: skip empties, or key the map by the loaded synth name from `/custos/here` /
  `/custos/loaded`).

## Reference

Full wire contract: `custos/docs/osc-contract.md` (§2 inbound `/custos/midi/route`|`/custos/midi/query`,
§3 the GP-mirror note + `Custos → KM` feedback row).
