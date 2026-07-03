# Experiment: GP parameter-count timing at boot

**Question (from the spec, §5):** Does Gig Performer read a plugin's parameter info at boot
*before* or *after* `setStateInformation`, and will it adopt an *effective* (non-5000) count?
The answer decides whether the **resident** mode (report the inner synth's real count) is feasible,
or whether every wrapper must keep the fixed 5000 facade.

## Setup
1. Build the plugin with tracing on (default) and a real hard-coded synth:
   `.\scripts\configure.cmd "<path-to-synth.vst3>"`
   `.\scripts\build.cmd Custos`
2. Install `Custos.vst3`, rescan in GP.
3. Delete any old log: `%TEMP%\custos-hosttrace.log`.
4. Use a **single** Custos instance. The "first getName" latch is process-global, so with two
   instances loaded it fires only for whichever GP queries first and the ordering is meaningless.
   (Also: don't run `ctest` between GP runs without re-deleting the log — the trace unit test
   writes to the same file.)

## Procedure
- **Run A (fresh add):** add Custos to a rackspace, then read the log.
- **Run B (gig reload):** save the gig, quit GP, delete the log, relaunch GP so it loads the saved
  gig, then read the log.

## What to record
Paste the ordered log lines for each run. The key comparison in Run B is the order of:
`setStateInformation` vs `first FacadeParameter::getName`.

- If `first getName` appears **after** `setStateInformation` → GP reads parameter info post-state →
  a resident wrapper could load its synth in `setStateInformation` and report the effective count.
- If `first getName` appears **before** `setStateInformation` (or `getName` is only ever called
  once at first add and never re-read) → GP caches the count/names too early → resident effective
  count is **not** feasible via state; keep the fixed 5000 facade (graceful degradation, spec §5).

## Results
_Run A:_ (paste log)

_Run B:_ (paste log)

## Conclusion
_Feasible / not feasible for resident mode, with reasoning. Feeds the M5 go/no-go and the
count-tuning decision (does GP stay responsive with 5000 params?)._
