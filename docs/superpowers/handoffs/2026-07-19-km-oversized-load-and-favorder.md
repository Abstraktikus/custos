# KM handoff — oversized-load prevention + instrument-DB push completeness

**From:** Custos session 2026-07-19 (incident analysis + facade guard, PR Abstraktikus/custos#17)
**To:** a Kapellmeister session (`C:\dev\kapellmeister`)
**Custos-side state:** guard deployed to the rig 2026-07-19 (Release ladder, includes PR #16 prefill).

---

Paste everything below into a fresh Kapellmeister session.

---

## Context (what happened, what Custos now does)

On 2026-07-19 an OSC-directed load put a big synth into a small-facade Custos. The heavy VST3
instantiation runs synchronously on the ONE message thread all Custos instances share inside
Gig Performer; it froze, GP hung (WER `AppHangB1`), and every instance's OSC went dead.

Custos now refuses such loads at every load path (`/custos/load`, `/custos/instrument/load`,
browse commit): if the instrument list entry for the requested path has `slots > facadeCap`,
Custos answers

```
/custos/ack "error instrument too large (slots <N> > facade <M>)"
```

(also mirrored to GP :54344) and does NOT instantiate. `slots <= 0` (unknown) still loads —
an unpushed DB must not block the rig. **That last rule is why KM's data duties below matter:
the guard is only as good as the `slots` data KM supplies.**

## KM duties

### 1. Authoring-time slot-fit (prevention at the source)

KM must never author/direct a synth into a Custos whose facade is too small:

- Each instance's `facadeCap` arrives as the 7th arg of `/custos/here` (the rig ladder is
  1000/2000/3000/4000/5000/10000).
- Each synth's inner param count belongs in the KM DB `slots` field — the Parameter
  Measurement project (branch `feat/param-measurement`, milestones B2–B5 still open) is the
  authoritative filler; `/custos/loaded`'s 4th arg (`innerTotal`) is the live source.
- Layer view / any synth-assignment UI should refuse or warn when `slots > facadeCap` of the
  target instance, BEFORE anything is sent.

### 2. DB push completeness (protect the curated favourites)

`/custos/favorites/begin … /custos/favorite … /custos/favorites/end` is a **full replace**
that Custos persists to `<presetRoot>/instruments.json`. Consequences:

- Every push must carry the complete record per entry: `name, path, favOrder, gainDb, brand,
  slots, controlType, paramDown, paramUp`. A push with `favOrder=0` everywhere silently wipes
  the operator's curated favourites (currently 47 live); a push with `slots=0` everywhere
  disarms the facade guard rig-wide.
- KM's `VstDatabase`/`instruments.km.json` FavOrder editing UI is still TODO — until it
  exists, do not push favOrder-less data over the live DB.
- No mandatory session-start push: Custos persists the DB and self-loads it at boot. Push on
  config change only. (Optional nicety: on connect, sanity-check via `/custos/here` and warn
  on suspicious emptiness instead of auto-pushing.)

### 3. Handle the refusal ack

Surface `error instrument too large …` to the operator as a config error (wrong synth→slot
assignment). Do not blind-retry — the answer will not change.

### 4. Sequencing reminder (unchanged, now with a why)

OSC to a Custos instance queues behind an in-flight load (message-thread serialization,
process-wide). Keep loads sequential and ack-gated (`/custos/loaded` before the next), as
established 2026-07-06.

## Explicitly NOT in KM's scope

- Custos boot robustness against an empty read of an existing `instruments.json`
  (OneDrive hydration) — Custos follow-up.
- Async load — deferred by Martin 2026-07-19 until more data ("der Unfall war ein Versagen
  der Umsysteme").
