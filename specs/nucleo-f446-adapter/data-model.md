# Phase 1 Data Model — NUCLEO-F446RE adapter

**Feature**: `specs/nucleo-f446-adapter` · **Date**: 2026-08-23

The entities below are all **in-SRAM, single-power-cycle** state. There is no persistence, no
serialization format other than the CDC telemetry line (R7), and no external storage. What
matters about each entity is therefore its **invariants** and its **bounds** — those are what
the host tests assert and what real-time safety depends on.

Every entity in the first table lives in `acfx_nucleo_support` and is **platform-independent**:
no TinyUSB, no CMSIS, no board headers (research R8). That constraint is what lets doctest reach
them (FR-002).

---

## `SampleFormat` (conversion, not state)

Stateless free functions rather than an entity, listed here because the format boundary they
enforce is a data contract.

| Property | Value |
|---|---|
| USB-side representation | Interleaved `int16_t`, L R L R …, native-endian |
| Effect-side representation | Non-interleaved `float`, one contiguous buffer per channel |
| Channels | 2 (**D4**) |
| Frames per call | 0 to 49 inclusive (**D21**, FR-028) |
| Scale factor | 32768 (FR-038a) |
| Rounding, float → int16 | Round to nearest |
| Clamping, float → int16 | To [-32768, 32767] — **required**, not optional (FR-038a) |

**Invariants**

- **I-SF1**: int16 → float → int16 recovers every input value exactly.
- **I-SF2**: A float outside [-1.0, 1.0) clamps at the boundary; it never wraps to the opposite
  rail. This is the invariant with teeth — wrapping produces loud broadband noise from a mild
  overshoot, and nothing counts it.
- **I-SF3**: Exactly `frames` frames are read and written; no access past the payload, including
  the `frames == 0` case.
- **I-SF3a**: A payload that is not a whole number of frames is truncated to whole frames and
  the remainder counted (FR-028a). Truncation preserves L/R alignment downstream; whole-packet
  rejection would discard good frames, and silent acceptance would misalign every subsequent
  frame.
- **I-SF4**: No heap allocation.

---

## `AudioRing`

The statically sized buffer between the USB packet cadence and the DSP's independent fixed
48-frame block cadence (FR-030, FR-030a). One instance per direction.

| Field | Meaning |
|---|---|
| storage | Fixed-capacity frame array, sized at compile time |
| writeIndex / readIndex | Single-producer / single-consumer cursors |
| capacity | **Deliberately not pinned** — a compile-time parameter (**D23**, FR-035) |
| startup fill | **Deliberately not pinned** — target occupancy before the consumer starts |
| water marks | **Deliberately not pinned** — occupancy thresholds for observation |

**State transitions**

| Situation | Behaviour | Counter |
|---|---|---|
| Read requests a block, ring holds fewer frames | Silence fills the shortfall | `inputUnderruns` / `outputUnderruns` |
| Write arrives, ring full | Oldest frames dropped | `inputOverruns` / `outputOverruns` |
| Capture-only: IN polled, **no OUT stream open at all** | Silence emitted | `inputStarved` |
| Torn payload: byte count not a whole number of frames | Truncate to whole frames | `truncatedFrames` |
| Stream start, ring below startup fill | Consumer **waits**; no block drawn | *(none — not an error)* |
| Sustained one-way excursion | **No re-centring**; drift stays visible in the counters | *(the existing under/overrun counters)* |

**Invariants**

- **I-AR1**: Occupancy is always in [0, capacity]. Bounded by construction; there is no path
  that grows it.
- **I-AR2**: Every substitution above increments its counter. No substitution is silent
  (FR-032) — this is the whole of how Principle VII is honoured in a path that cannot throw.
- **I-AR3**: No heap allocation and no locks (FR-030). Single execution context (**D26**) is
  what makes plain cursors sufficient today; FR-047 records the trigger to revisit.
- **I-AR4**: The DSP always draws a **fixed 48-frame** block. A partial ring is completed by
  I-AR2's substitution — the DSP cadence never stretches to match the ring (FR-030a).
- **I-AR5**: Packet size does not propagate into block size. A 0-frame or 49-frame packet
  changes occupancy, never the block.
- **I-AR6**: At stream start the consumer **waits** for the startup fill before drawing its
  first block (FR-030b). Starting early would manufacture a burst of underruns on every open,
  polluting the statistic the harness asserts against.
- **I-AR7**: The ring **never re-centres** (FR-030c). Both directions are paced by the same SOF
  clock, so persistent drift is a fault to surface, not to mask — and masking it would mean
  audible, uncounted frame drops, which I-AR2 forbids.
- **I-AR8**: On bus reset or re-enumeration the ring is cleared and I-AR6's wait applies again,
  so the device never drains a stale partial ring (FR-053).

**Deliberately unspecified**: capacity, water marks, and startup fill are derived from HIL
measurement per research R5 and pinned in Phase H. Values invented before measurement would be
false precision (**D23**).

---

## `AudioTransportStats`

The observability record (FR-033). Seven `uint32_t` counters — four error counts, one
capture-only starvation count, one denominator, one worst-case timing.

| Field | Meaning |
|---|---|
| `inputUnderruns` | DSP wanted a block, input ring was short |
| `inputOverruns` | USB filled faster than DSP drained |
| `outputUnderruns` | USB polled IN, output ring was empty |
| `outputOverruns` | DSP produced faster than USB drained |
| `inputStarved` | Capture-only: silence emitted (**D22**) |
| `truncatedFrames` | Remainder discarded from a torn payload (FR-028a) |
| `blocksProcessed` | Denominator — lets the counters become a rate |
| `worstBlockMicros` | Longest observed block; makes the CPU budget directly observable |

**Invariants**

- **I-TS1**: Every counter is monotonically non-decreasing **modulo 2^32** within a power cycle
  (FR-034a). Counters wrap rather than saturating, so consumers take **deltas between
  snapshots** — which is what a rate over an interval needs anyway. They are not resettable at
  runtime, and survive suspend and bus reset unchanged (FR-054).
- **I-TS1a**: The counters are **mutually exclusive** — one event increments exactly one
  counter. `inputStarved` covers IN-silence when *no playback stream is open*;
  `outputUnderruns` covers IN-silence when the playback stream *is* open but the ring was
  momentarily empty (FR-029a). Different causes, different counters.
- **I-TS2**: `blocksProcessed` increments exactly once per DSP block. Without a correct
  denominator the other six are uninterpretable — a raw count of 400 underruns means nothing
  until you know whether it was over 400 blocks or 4 million.
- **I-TS3**: `worstBlockMicros` is a maximum, never a running average, and never resets
  implicitly.
- **I-TS4**: A `worstBlockMicros` of 0 after any block has been processed indicates the timing
  source failed to initialize and MUST surface loudly (research R6). Zero-meaning-unmeasured is
  indistinguishable from zero-meaning-instantaneous, which is precisely the observability
  failure FR-034 exists to prevent.
- **I-TS5**: Reading the record does not allocate, block, or perturb the audio path (FR-033a).

**Wire form** (R7): line-oriented `key=value`, newline-terminated, one snapshot per line, over
the CDC serial function. Chosen so the Python HIL harness parses it with `split`, and a
developer who just opens the serial port can read it.

---

## `ParameterShadow`

The per-`ParamId` shadow block with dirty flags (**D25**, FR-041). This is a **state** store,
not an event queue — that distinction is the entire design decision.

| Field | Meaning |
|---|---|
| `values[N]` | Normalized 0..1 value per parameter |
| `dirty[N]` | Per-parameter change flag |
| `N` | `AppEffect::parameters().size()` — the bound, fixed at compile time |

**Lifecycle**: a source writes a slot and sets its flag at any time → once per audio block the
adapter walks the flags, calls `setParameter` for each dirty parameter, and clears the flags
(FR-042).

**Invariants**

- **I-PS1**: Bounded by construction at `N`. There is structurally no overflow to handle —
  contrast a FIFO, which must have a drop policy and therefore must lose something.
- **I-PS2**: Last-write-wins per parameter. A burst of writes within one block period leaves the
  **final** value, never an intermediate one. This is not a lossy compromise; it is the
  semantically correct operation for a state-valued parameter.
- **I-PS3**: No parameter's pending update can be evicted by another parameter's activity. Per-`ParamId`
  slots make cross-parameter starvation structurally impossible — the failure mode
  drop-oldest on a shared FIFO permits (**D25**, FR-043).
- **I-PS4**: `setParameter` is called exactly once per dirty parameter per block, and not at all
  for clean ones.
- **I-PS5**: `N == 0` is valid; the walk is a no-op.

**Known limitation, recorded rather than hidden** (FR-044): this shape is **wrong for
event-valued controls** — a momentary trigger or tap tempo, where each occurrence matters and
collapsing to "last value" destroys the signal. acfx's parameter model currently has only
normalized continuous values, so nothing is broken today, and such a control would need its own
mechanism regardless.

---

## `ParameterSource`

The seam every parameter input plugs into (FR-039, FR-040, **D2**, **D3**). A duck-typed
contract — `void poll(ParameterShadow<N>&) noexcept` — not a base class, matching how
`acfx::Effect` and MNA's `CompanionSupply` are already expressed in this repo.

| Implementation | Kind | How `poll()` behaves |
|---|---|---|
| `MidiParameterSource` (**D2**, first) | Event-driven | Drains pending CCs, resolves each via `MidiCcMap`, writes the mapped slot |
| ADC / encoder source (**D3**, later) | Sampled state | Reads the current value, writes only past a dead-band |

**Invariants**

- **I-PSRC1**: Both source kinds converge on the **shadow block** — on data, not on a shared
  execution model. This is precisely what makes physical peripherals a later *addition* rather
  than a later *redesign*, which is what **D3** asked for.
- **I-PSRC2**: A source never dirties a slot for an unchanged value (dead-band). Without this a
  sampled source dirties everything every block, and `flush()` degenerates into applying every
  parameter at audio rate — the exact waste the dirty flags exist to avoid.
- **I-PSRC3**: A source never calls `setParameter`; only `flush()` does (I-PS4).
- **I-PSRC4**: Sources compose — several may be polled per block; same-slot writes resolve by
  last-write-wins (I-PS2).

**Why this is an entity and not just a shape**: without it, D3's "shaped now for physical
peripherals" has nothing to point at, and the MIDI path would wire straight into the shadow
block — making the second source a refactor of the first.

---

## `MidiCcMap`

Table-driven mapping from MIDI CC number to parameter index (FR-045, research R9).

| Field | Meaning |
|---|---|
| table | CC number → parameter index |
| bound count | `min(table size, AppEffect::parameters().size())` |

**Invariants**

- **I-MC1**: A CC with no mapping is ignored, disturbing no slot (spec Edge Cases).
- **I-MC2**: An out-of-range parameter index never reaches `setParameter`. The Daisy adapter's
  `boundKnobs()` is the existing shape for this.
- **I-MC3**: The mapping is pure — CC in, optional parameter index out. No state, so it is
  trivially host-testable.

**Convention is OPEN** (open question 7). The mechanism is planned so that settling it later is
a table edit, not a rewrite. R9 records a recommendation — bind CC numbers to parameter indices
generically and omni-channel, mirroring `daisy-main.cpp`'s knob binding — offered but **not
applied**, since OQ7 also asks whether the convention should match the workbench's existing MIDI
CC consumption.

---

## Shim-side entities (NOT in the support library)

These touch silicon and therefore live in `nucleo-main.cpp` and its siblings, outside host test
reach (FR-003).

| Entity | Role | Key constraint |
|---|---|---|
| Clock configuration | HSE bypass on the ST-Link 8 MHz MCO; PLL M=4 N=168 P=2 Q=7 (**D6**) | 168 MHz SYSCLK, **exactly** 48 MHz PLLQ. Lock failure is fatal (**D7**) |
| `SystemCoreClock` | Owned by the adapter (**D14**) | TinyUSB derives PHY turnaround from it; a wrong value degrades timing *silently* |
| Fault indicator | LD2 (PA5): **three short pulses, long gap, repeating**, then halt (FR-015a/b) | Initialized **before** clock validation (FR-015c) — it runs on reset-default HSI, since without a PLL there is no USB to report over. Cadence is approximate; the pattern's shape carries the signal |
| USB lifecycle handling | Suspend, resume, bus reset (FR-051–FR-055) | Rings cleared on reset with the startup-fill wait reapplied; counters survive unchanged so the event's cost stays measurable |
| Vector table | Generated from CMSIS `IRQn_Type` (**D13**) | Must span `OTG_FS_IRQn` = 67; a core-exceptions-only table faults on the first USB interrupt |
| USB descriptor set | UAC2 + MIDI + CDC, IAD-grouped (**D5**, FR-018a) | One advertised format per direction: 48 kHz / 16-bit / stereo (**D4**) |
| Block timer | DWT `CYCCNT` at 168 MHz (research R6) | Must fail loud if it reads stuck-at-zero (I-TS4) |

## Entity relationships

```text
USB OUT packet (0-49 frames, int16 interleaved)
        │  SampleFormat: de-interleave + convert          [I-SF1..4]
        ▼
   input AudioRing  ──────────────────────────────────►  counters
        │  fixed 48-frame draw                            [I-AR4, I-AR5]
        ▼
   acfx::AudioBlock (non-interleaved float*, in place)
        │  AppEffect::process()          ◄── ParameterShadow walk, once per block  [I-PS4]
        ▼                                          ▲
   output AudioRing ──────────────────────────────►│──►  counters
        │  SampleFormat: convert + interleave  [I-SF2 clamp]
        ▼                                          │
USB IN packet  (host-paced, one per SOF)           │
                                                   │
MIDI CC ──► MidiCcMap ─────────────────────────────┘   [I-MC1..3]
(physical peripherals plug in at the same seam — D3)

AudioTransportStats ──► CDC serial, key=value lines ──► HIL harness   [I-TS1..5]
```
