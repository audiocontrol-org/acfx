### CPM bootstrap re-downloads on every configure — no existence guard; breaks offline reconfigure and contradicts its own comment

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/CPM.cmake:18-29

The header comment claims CPM "downloads the pinned CPM package manager **on first configure**" and that the file "lands in the gitignored CPM cache." The code does not match: `file(DOWNLOAD ...)` (lines 24-29) is unconditional — there is no `if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")` guard around it, which is the load-bearing part of the canonical CPM bootstrap. As written, every `cmake` configure / reconfigure hits `github.com/cpm-cmake/.../releases/download/...` again, even when the pinned file is already present in the cache.

Blast radius: a downstream adopter who configures once online, then reconfigures **offline** (or behind a flaky/rate-limited GitHub), gets a fatal configure failure even though the correct, hash-verified CPM file is already on disk. CI that reconfigures per job pays a network round-trip and a GitHub-availability dependency on every build. The round-0 self-red-team lens applies here: this file is itself the fix that replaced the vendored CPM (commit `8e0e37b`), and the fix opened a new channel — repeated network fetches — that the prior vendored approach did not have. A reasonable fix wraps the download in `if(NOT (EXISTS "${CPM_DOWNLOAD_LOCATION}"))` (the upstream get_cpm pattern already re-verifies the hash on `include`), so the fetch truly happens only once and offline reconfigure works.

### Workbench `prepareToPlay` marks the source ready in the no-source case without surfacing the promised error

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:78-99 (source-selection block)

The comment block (lines 78-81) states the source-selection policy as "the built-in file player when `ACFX_WORKBENCH_FILE` … **else** the live device input, **else a surfaced error**." The code implements only the first two branches:

```cpp
if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) { source_.useFilePlayer(...); }
else if (inputs > 0) { source_.useLiveInput(inputs); }
source_.prepare(sampleRate, blockSize);
sourceReady_ = true;
```

When the env var is unset **and** `inputs == 0` (a real fresh-install state: no audio file configured, an output-only device, or a device whose input channels aren't enabled), neither `useFilePlayer` nor `useLiveInput` runs, yet `source_.prepare()` is still called and `sourceReady_` is unconditionally set to `true`. There is no visible "else a surfaced error" branch in this file. The promised Constitution-V no-silent-fallback behavior is entirely delegated to an unseen contract: `WorkbenchAudioSource::prepare()` (or `fillBlock()`) must throw `AudioSourceError` when no mode was selected. If that source does not throw on an unconfigured/default mode, `getNextAudioBlock` calls `fillBlock` on an unconfigured source — the exact silent-fallback-to-garbage/silence failure the comment claims to prevent, with no operator notification.

Blast radius: an operator on a fresh machine with no input device sees the window come up, hears nothing or garbage, and gets no error dialog — the failure mode the surrounding code went to lengths to avoid (the `catch (AudioSourceError&)` dialog path right below). Either add an explicit `else { throw AudioSourceError("no audio source: set ACFX_WORKBENCH_FILE or enable a live input"); }` before `prepare()`, or, if `WorkbenchAudioSource` is genuinely the enforcement point, the comment here should say so instead of implying a local error branch that doesn't exist. This is anchored in this chunk but the enforcement lives in `audio-source.h` (chunk a05731de…) — that contract must be confirmed, not assumed.

### `params_` member is written but never read — dead state on a hot-path component

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:30 and member decl ~line 146

The constructor body assigns `params_ = node_->parameters();` (line 30), and `params_` is declared as a member `span<const ParameterDescriptor> params_;`. But `paramView_` is already constructed directly from `node_->parameters()` in the initializer list, and nothing in the component ever reads `params_` afterward. It is dead state.

Blast radius is small (a stored `span` is cheap), but it is a misleading-intent hazard: a future maintainer reads `params_` as "the live parameter table this component drives" and may build on it, when in fact it is never consulted and could drift from what `paramView_` actually holds. Either delete the member and its assignment, or, if it is meant to be the single source the component reads, route `paramView_` and the MIDI handler through it instead of re-calling `node_->parameters()`. Cosmetic, no current behavioral consequence — hence low.

### Third-party dependency pins mix verified and unverified refs; the unverified ones fail only at first per-toolchain configure

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    cmake/dependencies.cmake:12-20, 49-87

The comment is commendably honest: DaisySP and doctest pins are marked "verified by an in-session fetch+build," while JUCE `8.0.14`, clap-juce-extensions `16e9d4c`, libDaisy `c02245d`, and the two Teensy pins are marked "captured from the upstream repos (real refs)" with "first-fetch verification happens the first time each target's preset is configured on a machine with the matching toolchain." This is the correct disclosure shape and resolves the prior fabricated-version concern as a matter of intent.

The residual blast radius worth flagging: the desktop/daisy/teensy pins are presented inline alongside the verified ones with identical syntax, so an adopter scanning the file cannot tell which builds have actually been exercised — and a wrong tag (e.g., if `JUCE 8.0.14` does not resolve, which pattern-matches the previously-flagged too-high-minor shape) fails loud only when someone with that toolchain first configures, potentially far downstream of this commit. This is fail-loud, not silent, so it is low rather than higher; but the verification asymmetry deserves a one-line in-file marker per pin (e.g. `# UNVERIFIED — no desktop toolchain in CI`) so the gap is visible at the point of use, not just in the prose header. No fix to behavior is required.