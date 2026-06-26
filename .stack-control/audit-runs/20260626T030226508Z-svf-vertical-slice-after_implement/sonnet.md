### `process()` callable before `init()` — DaisySP filter in uninitialized state

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:41-53

`SvfPrimitive::process()` calls `svf_.Process(in)` followed by `svf_.High()`/`Band()`/`Low()`. DaisySP's `Svf` requires an explicit `Init(sampleRate)` call before `Process()` to set up its internal coefficients and zero its delay lines. The default member initializer `daisysp::Svf svf_{}` only zero-initializes the struct; it does not run the coefficient math that `Init()` does. If `process()` is called before `init()` (plausible in a test harness, a new adapter, or a unit that constructs an `EffectNode<SvfEffect>` and processes a block before `prepare()` arrives), DaisySP operates on an uninitialized coefficient set and produces undefined output — likely NaN propagation or unbounded gain, both of which can produce harmful audio.

`reset()` at line 38 also calls `svf_.Init(sampleRate_)` using the cached `sampleRate_` default of `48000.0f`. Calling `reset()` before `init()` silently initializes DaisySP at 48 kHz even when the host will later call `prepare()` at a different rate. The primitive accumulates inconsistent state that only clears if `init()` is called again.

A minimal fix is to track an `initialized_` flag and either early-return with silence from `process()` before init, or add a precondition assertion (`assert(initialized_)`). Either the API should be made fail-safe or the precondition should be documented and enforced at the call site in `SvfEffect`.

---

### `check-portability.sh` check #2 does not detect DaisySP includes in `core/`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:29-35, core/primitives/svf-primitive.h:5

Check #2 (`== 2. No platform headers in core/ ==`) uses the pattern `juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>`. `core/primitives/svf-primitive.h` at line 5 includes `"Filters/svf.h"` — a DaisySP header. DaisySP is not in the pattern; the check emits `OK: core/ is platform-independent` while DaisySP is live in `core/`. 

The stated invariant is that `core/` is free of platform headers so the DSP logic is portable across desktop, Daisy, and Teensy targets. Whether DaisySP satisfies that invariant is a design question (it is claimed to be "platform-independent pure-DSP math" in the header comment), but that claim is unverified by the gate. If DaisySP later introduces ARM intrinsics or Cortex-M-specific `#pragma` blocks (which its upstream repo does, for CMSIS-DSP acceleration), the check will not catch the regression. The check's stated guarantee and its actual coverage diverge at this point.

Adding `daisysp|Filters/svf` to the grep pattern — or carving out an explicit allow-list — would make the check's scope honest. Alternatively, if DaisySP is intentionally permitted in `core/`, the gate comment should say so.

---

### `#ifdef` platform-fork check only covers `core/effects/`, omits `core/primitives/`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:43

Check #4 (`== 4. One-source-many-targets ==`) greps for per-target `#ifdef` forks with the path `core/effects/`. The actual effect source lives at `core/effects/svf/svf-effect.h`, so the check covers the right file for the current feature. But `core/primitives/` now contains `svf-primitive.h` and will accumulate future primitives. An `#ifdef __arm__` or `#ifdef DAISY` in `core/primitives/` would be silently invisible to this gate — the operator would get `OK: no per-target #ifdef forks in core/effects/` while a fork exists one directory over.

The fix is to broaden the path argument to cover all of `core/` (or at minimum `core/effects/ core/primitives/ core/dsp/`). Since the invariant is "the entire core is one-source", the scope of the check should match.

---

### `check-portability.sh` check #4 searches for `acfx_core` but adapters may link the `acfx::core` alias

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:49-54

The loop at lines 49–54 checks each adapter CMakeLists.txt with `grep -rq 'acfx_core'`. CMake permits two spellings: the raw target name `acfx_core` and the namespaced alias `acfx::core`. The `host/processor-node/CMakeLists.txt` (also in this diff) uses `acfx_core` directly, so the convention in this repo appears to be the raw name — but the alias pattern is idiomatic CMake and a future adapter or a copy-paste from another project could use `acfx::core`. The grep would then report `FAIL: adapters/<x> does not link acfx_core` for an adapter that is correctly linked via the alias. A developer seeing a spurious portability failure would either widen the pattern or disable the check; both erode the gate's signal value.

A simple fix: `grep -rqE 'acfx_core|acfx::core'` in that loop. Alternatively, document that only the non-namespaced form is used in this repo and add a check for the alias form elsewhere.