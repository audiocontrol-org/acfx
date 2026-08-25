# Data Model: Synchronous, multi-format USB-audio transport

Transport-layer entities and state. This feature is firmware, so "entities" are device state and
the USB contract, not persisted records.

## Entities

### ClockSource (single, shared, SOF-derived)
- **Kind**: `INT_VAR_CLK | CLK_SYC_SOF` (variable, USB-SOF-synchronous).
- **Supported frequencies**: {44 100, 48 000} Hz (advertised as two RANGE subranges, each
  `bMin==bMax==bRes`).
- **Current frequency** (`g_currentSampleRateHz`): host-settable via the read-write Sampling
  Frequency Control (`AUDIO20_CTRL_RW`); default at enumeration = 48 000 Hz.
- **Relationships**: referenced by BOTH AudioStreaming interfaces' terminals (one clock domain,
  FR-016).
- **Validation**: a SET to a value not in {44 100, 48 000} is rejected.

### AudioFormat (per AudioStreaming interface, selected by alternate setting)
- **alt-0**: zero-bandwidth (idle).
- **alt-1**: stereo PCM, 16-bit (`bSubslotSize=2`, `bBitResolution=16`).
- **alt-2**: stereo PCM, packed 24-bit (`bSubslotSize=3`, `bBitResolution=24`), signed little-endian
  — *subject to FR-014*.
- **Current format** (recorded per direction on SET_INTERFACE): selects the wire↔float converter.

### IN stream (device → host)
- **Sync type**: Synchronous (`bmAttributes = 0x0D`).
- **Cadence**: exactly the SOF-derived nominal per USB frame for the current rate — 48 at 48 kHz;
  44 or 45 at 44.1 kHz on the exact 44 100/1 000 schedule (TinyUSB flow control owns this).
  **Device owns the cadence**: always emits the scheduled nominal packet; never a short/ZLP in
  steady state.
- **Source**: the output ring (processed audio), kept fed by the DSP block engine.

### OUT stream (host → device)
- **Sync type**: Synchronous (`bmAttributes = 0x0D`).
- **Cadence**: the host's nominal per-frame delivery for the current rate.
- **Fault contract (FR-017)**: the **host owns the contents**; a short/ZLP/malformed OUT payload is
  a bounded, **counted** substitution/truncation (carries forward base FR-028a) that MUST NOT
  corrupt stereo alignment nor perturb the IN cadence.

### AudioRing (input + output)
- **Fields**: capacity, startup fill, water marks, **occupancy min/max** (new instrumentation).
- **Sizing**: measurement-derived (FR-008, §R10) — expected far smaller than the current
  1024-frame/98-fill placeholder because the SOF-exact cadence removes open-ended pull variability.
- **State**: Priming → Running (base feature's ring states, preserved).

### RateFormatChangeEvent (lifecycle)
- **Trigger**: a Clock Source SET_CUR (rate) or a SET_INTERFACE alt change (format).
- **Reaction** (deferred off EP0 to a poll-loop service step, R9): on rate change → re-`prepare()`
  the compiled-in effect at the new sample rate + reset the rings; on format change → switch the
  recorded wire format (converter bit depth). Resume streaming without re-plug (FR-006).

## Key state variables (adapter)
- `g_currentSampleRateHz` — the selected clock frequency (set by the strong set-frequency callback).
- per-direction current alt / format (set by the strong SET_INTERFACE callback).
- a "rate/format-change pending" flag consumed by the poll-loop service step (like `ServiceUsbLifecycle`).
- ring occupancy counters (min/max) for the measurement pass.

## Transitions
```
enumerate → default (48 kHz, alt-0 idle)
host SET clock freq (∈{44100,48000}) → flag pending → [service step] re-prepare(rate) + ring reset → stream at new rate
host SET_INTERFACE alt (1↔2) → record format → [service step] switch converter depth → stream at new format
host SET_INTERFACE alt-0 → stop that direction (idle), counters untouched (base US10)
suspend/resume, mount/unmount → base US10 lifecycle, preserved
```
