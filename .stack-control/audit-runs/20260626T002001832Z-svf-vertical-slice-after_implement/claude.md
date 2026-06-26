I'm being invoked as an independent audit reviewer in a multi-model barrage. I've walked chunk `e8e701847d91847a` carefully. Here are my findings, anchored to the diff.

---

### Discrete `mode` selection truncates instead of rounds — bandpass becomes effectively unreachable via normalized parameter input

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/effects/svf/svf-effect.h:60-71 (`toMode`) + setParameter kMode path (lines 92-95); cross-file with `denormalize` in core/dsp/parameter.h (chunk b04c878073d5d626)

`toMode` maps a plain-unit value to one of three `SvfMode`s via `static_cast<int>(index)` (line 62), which is **truncation toward zero**, not rounding. The `kMode` descriptor advertises `min=0, max=2, ParamKind::discrete` (line 49). The setParameter path is `mode_ = toMode(denormalize(kParams[kMode], normalized))` (line 93). If `denormalize` performs the natural linear map `value = min + normalized*(max-min) = normalized*2`, then bandpass (mode 2) is only produced when the product is `>= 2.0`, i.e. only at `normalized == 1.0` exactly. Every value in `[0.5, 1.0)` truncates to `1` (highpass). So a DAW automation lane or UI slider sweeping 0→1 yields lowpass for the first half, highpass for the second half, and bandpass only at the single endpoint.

Even if `denormalize` *does* snap discrete params (the descriptor carries a discrete count of `3`), truncation remains fragile: a value intended to be `2.0` but computed as `1.9999998f` through a skew/normalization round-trip truncates to `1` → highpass instead of bandpass. Rounding (`static_cast<int>(index + 0.5f)` or `std::lround`) is robust against both the linear-map and the FP-jitter case; truncation is robust against neither.

Blast radius: this is the *one* effect proving the cross-platform slice (FR-003, "one constexpr table drives every adapter"), and `mode` is one of its three parameters. The host-side tests (svf-reference.h, chunk 5d46bb00) almost certainly set mode directly rather than round-tripping through normalized input, so this defect ships green. An adopter wiring the effect into a plugin gets a silently-broken third mode. Fix: round in `toMode`, or have `denormalize` snap discrete kinds and clamp to `[0, count-1]` before the cast.

---

### `clampedCutoff` silently caps cutoff at `sampleRate*0.32`, so the descriptor's advertised 20 kHz max is unreachable at typical sample rates

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:75-83 (`clampedCutoff`) vs. kParams cutoff descriptor (line 47)

The cutoff descriptor advertises `max = 20000.0f` (line 47), but `clampedCutoff` caps the actual filter frequency at `sampleRate_ * 0.32f` (line 76). At 48 kHz that is 15360 Hz; at 44.1 kHz it is 14112 Hz. The top ~4.6 kHz of the advertised range is therefore silently unreachable: a host/UI reading the descriptor displays a 20 kHz endpoint and a normalized=1.0 automation point, but the audio never sees a cutoff above ~15.4 kHz. This is a descriptor-vs-behavior contradiction — the single source of parameter truth (SC-006) advertises a range the process path won't honor.

The DaisySP `f < sampleRate/3` stability bound is real and the clamp itself is correct DSP. The drift is that the *descriptor* lies about the reachable range. A reasonable fix is to make the descriptor's max sample-rate-aware (or document the cap explicitly), or at minimum acknowledge that the descriptor max is a nominal ceiling the effect deliberately narrows. As written, an unattended consumer mapping the descriptor range to a UI will mislabel the control.

Secondarily, the lower clamp hardcodes `20.0f` (line 80) rather than referencing `kParams[kCutoff].minValue`; if the descriptor min ever changes, this duplicate silently diverges (magic-number duplication).

---

### `process()` silently passes unfiltered audio for channels beyond the prepared count when a wider block arrives

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:73-81 (`process`)

`process` computes `channels = min(io.numChannels(), numChannels_)` and only iterates that many (line 74). `numChannels_` is frozen at `prepare()` time from `ctx.numChannels` (line 36). If the host later delivers a block with **more** channels than were prepared (a stereo effect prepared mono, then fed a 2-channel block, or any reconfiguration without a re-`prepare`), the extra channels fall outside the loop and pass through **completely unfiltered** — clean, un-attenuated dry signal mixed alongside filtered channels. There is no error, no log, no assertion: a silent partial-passthrough, which is exactly the "fallback that hides a failure mode" the project guidelines warn against.

The inverse case (block narrower than prepared) is benign. The defect is the wider-block case. The contract (prepare-before-process, fixed channel count) may make this a programming error, but the code handles it by silently producing wrong output rather than failing loud. A descriptive error or a clamp-with-diagnostic on a channel-count mismatch would surface the misconfiguration instead of burying it in the mix.

---

### Portability gate 4's adapter-link check is a substring grep — satisfiable by a comment, blind to the `acfx::core` alias and transitive `acfx::host` linkage

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:55-60 (gate 4 link loop); cross-file with host/processor-node/CMakeLists.txt:11 and the adapter CMakeLists (chunks 952c352c / ba29de07)

Gate 4 claims to enforce "every adapter links the same `acfx_core` (SC-001/SC-005)" via `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"` (line 56). This is a raw substring test, which is unsound in two directions:

1. **False pass.** Any occurrence of the literal `acfx_core` satisfies the grep — including a *comment*. An adapter whose CMakeLists says `# linked against acfx_core via the host target` but does **not** actually call `target_link_libraries(... acfx_core)` passes the gate while violating the invariant it claims to check.
2. **False fail / brittleness.** The host library exposes the alias `acfx::core` and `acfx::host` (host/processor-node/CMakeLists.txt uses `add_library(acfx::host ALIAS ...)` and links `acfx_core`). If the workbench/plugin adapters link via the alias (`acfx::core`) or transitively through `acfx::host` — the idiomatic modern-CMake way — the string `acfx_core` may not appear in their CMakeLists at all, and the gate reports a spurious FAIL.

A grep over CMakeLists text cannot verify an actual link edge; CMake itself can (e.g. a configure-time `get_target_property` assertion, or parsing the generated link line). As written, the gate gives false confidence that the one-source-many-targets invariant holds when it has only confirmed a substring is present somewhere in a text file. Since the whole point of these gates is "explicit, visible quality steps" replacing hooks (Constitution II), a gate that can be satisfied by a comment undermines that guarantee.

---

### `find` filter in the file-size gate misses `.hpp` / `.cc` / `.cxx` / `.c` sources

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:25 (gate 1 `find`)

Gate 1 enumerates only `-name '*.h' -o -name '*.cpp'` (line 25). The current tree uses exactly those extensions, so the gate is correct today. But the file-size budget (Constitution VII) is meant to apply to *all* source; a future `.hpp`, `.cc`, `.cxx`, or `.c` file would silently escape the 500-line hard cap with no warning. This is hygiene, not a present defect — flagged because a portability/quality gate that silently under-covers reads as "all files checked" when it isn't. Widening the pattern (or globbing by a maintained extension list) closes the gap.

---

I did **not** flag: the DaisySP dependency in `core/` (gate 2's pattern intentionally excludes `daisysp`/`Filters/svf.h`, consistent with the documented "DaisySP is platform-independent pure-DSP" decision); the `span` polyfill's converting constructor (the SFINAE correctly rejects const-stripping via `static_cast<T*>` and binds const lvalues fine); or the single-virtual-per-block dispatch in `EffectNode` (the returned `span` points to static `constexpr` storage with valid lifetime, and the hot path is the inlined concrete `fx_.process`). Those checks came back clean.