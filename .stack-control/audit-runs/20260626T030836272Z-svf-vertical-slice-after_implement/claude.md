I'll review this chunk of the `svf-vertical-slice` diff as an independent audit reviewer. Walking the eight files for correctness, RT-safety, the cross-thread parameter handoff, and descriptor/behavior consistency.

### Advertised cutoff range (20 kHz) is unreachable at standard sample rates — silent SSOT mismatch

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:163-172 (clampedCutoff) vs :92-94 (kParams cutoff descriptor)

The cutoff descriptor is the declared single source of parameter truth (FR-003 / SC-006) and advertises `cutoff: [20, 20000] Hz`. But `clampedCutoff()` caps the effective frequency at `sampleRate_ * 0.32f`. At the canonical 48 kHz that ceiling is **15 360 Hz**, so the entire top ~23% of the advertised knob (15.36 kHz → 20 kHz) silently collapses to a single value. A host or adapter that trusts the descriptor — draws a 20 kHz label, automates cutoff to its maximum, or asserts the realized cutoff matches the requested plain value — gets a quietly wrong result, and nothing in the descriptor expresses the sample-rate-dependent cap. The cap itself is correct and necessary (DaisySP's Svf requires cutoff < sr/3), so the defect is not the clamp but the *gap between the advertised range and the achievable range* in the surface that the whole feature calls authoritative.

Blast radius: an unattended consumer building on the "single source of truth" descriptor reads a range the effect does not honor; tests written against the descriptor's max diverge from runtime, and UI/automation maps a dead zone. A careful human reading the inline comment resolves it, which keeps this out of `high` — but it is a real honesty gap in the load-bearing SSOT claim. A reasonable fix is to either derive the descriptor's `max` from the achievable bound at `prepare()` time (and expose it), or document the sample-rate-dependent ceiling in the descriptor/contract rather than only in a private method comment.

### `clampedCutoff` hardcodes the 20 Hz floor, duplicating the descriptor minimum

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:165-170

`clampedCutoff()` re-clamps the lower bound with a literal `if (f < 20.0f) f = 20.0f;`. That `20.0f` is the cutoff descriptor's `min` (line 92), copied as a magic number into a second location. `cutoffHz_` already arrives bounded to `[min, max]` from `denormalize`, so this floor is normally dead, but if anyone raises the descriptor floor (e.g. to 30 Hz) the two surfaces silently drift and the effect would re-admit values the descriptor forbids. Blast radius is small (the value is currently consistent and the path is internal), so this is hygiene — but it's exactly the kind of duplicated constant that compounds. Reference `kParams[kCutoff].min` instead of the literal.

### Teensy toolchain sets `FIND_ROOT_PATH_MODE_LIBRARY ONLY` with no root path / sysroot

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    cmake/toolchains/teensy.cmake:36-40

The toolchain sets `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY`, `..._INCLUDE ONLY`, and `..._PACKAGE ONLY`, but never defines `CMAKE_FIND_ROOT_PATH` or `CMAKE_SYSROOT`. With `MODE ONLY` and an empty root path, `find_library`/`find_path`/`find_package` are restricted to searching under a root that is effectively empty, so any future dependency lookup in this preset will fail to locate libraries that are actually present in the ARM toolchain's sysroot. The current SVF build is header-only and links only `nano.specs`/`nosys.specs`, so nothing exercises this yet — hence `low` — but the first time someone adds a `find_library` for a Teensy-side dependency it will silently fail to find it and report "not found" for something installed. A fix is to set `CMAKE_FIND_ROOT_PATH` (or `CMAKE_SYSROOT`) to the ARM toolchain root alongside these mode settings, or to leave the library mode at the CMake default until there is a real cross-find to constrain.

### C++17 `span` polyfill converting constructor is under-constrained (data() only, no size())

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    core/dsp/span.h:35-39

The Teensy-path polyfill's container constructor SFINAE-guards only on `static_cast<T*>(c.data())` but the body also calls `c.size()`. A type exposing a convertible `data()` but no `size()` would fail as a hard error inside the constructor body rather than being cleanly excluded from overload resolution, and the constructor is broader than `std::span`'s contiguous-range constraint (it will bind any container with a convertible `data()`, including owning containers, risking a dangling view). This only affects the C++17 Teensy build (desktop/tests/Daisy use real `std::span`), and `core/` currently only ever constructs the span from the `kParams` `std::array`, so there is no live misuse — purely informational. If hardening is wanted, add `decltype(std::declval<Container&>().size())` to the SFINAE clause so the guard matches what the body requires.

---

### What I checked and found clean

The cross-thread parameter handoff in `svf-effect.h` is correct: the producer's relaxed `pendingBits_` store is ordered before the release `pendingDirty_` store, and the audio thread's `exchange(0, acquire)` on `pendingDirty_` synchronizes-with it, so the subsequent relaxed `pendingValue()` load is guaranteed to observe the published bits — no torn read, no lost update under last-writer-wins, and `is_always_lock_free` is statically asserted. The constructor explicitly zero-stores both atomic arrays, closing the pre-C++20 indeterminate-init gap. `clamp01` correctly neutralizes NaN/±inf via its branch structure. The discrete `normalize`/`denormalize` round-trip lands mid-bucket and survives the `norm == 1.0` edge (clamped to `count-1`). `process()` performs no allocation and bounds channels by `min(io.numChannels(), numChannels_)`. The compile-time `isValidDescriptor` static_assert over `kParams` and the descriptor invariants are sound. Had I found a missing acquire/release pairing, a non-lock-free degradation, an unguarded NaN reaching filter state, or a `process()` allocation, I would have rated it `high`/`blocking`.