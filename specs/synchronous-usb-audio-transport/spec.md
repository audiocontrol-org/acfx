> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> **4. ALL UI/UX WORK GOES THROUGH `/frontend-design`** — no exceptions, no offroading; every
> user-facing visual/interaction decision routes through the frontend-design skill.
> **5. SCOPE IS THE OPERATOR'S CALL** — never cut/defer/drop scope on "YAGNI" or "simplicity";
> when scope is open, present options and ASK. The operator decides scope, not the agent.
> (acfx Constitution, Principles I–V — `.specify/memory/constitution.md`.)

# Feature Specification: Synchronous, multi-format USB-audio transport (NUCLEO-F446RE)

**Feature Branch**: `nucleo-f446-adapter` (follow-on transport fix; descriptive spec dir)

**Created**: 2026-08-25

**Status**: Draft

**Input**: Rework the Nucleo USB-audio transport from a free-running, no-feedback asynchronous
source into a proper **synchronous** (USB-SOF-locked) USB Audio Class device, fixing a
pitch-down / digital-noise / large-latency defect observed live when the device is used inside a
CoreAudio aggregate, and adding native **44.1 + 48 kHz × 16 + 24-bit** format support.

## Context

The `nucleo-f446-adapter` feature ships a driverless USB-audio effect device. It has **no analog
converter** — audio is pure math flowing host → device → host — so to *monitor* it live a user
binds it with a real audio interface in a host **aggregate device** (e.g. Logic Pro through a
CoreAudio aggregate + an I/O plugin).

Diagnosed live 2026-08-25: the transport presents the device as a free-running **asynchronous**
source with **no feedback endpoint**, and its device→host (IN) isochronous endpoint delivers a
**variable, buffer-gated packet size decoupled from the USB frame clock** (it sends `min(FIFO
bytes, max)` at each transfer-complete, and a **zero-length packet when its buffer is empty** —
which, measured on hardware, is nearly every service interval). Inside an aggregate the host's
resampler cannot lock to the device's rate, so the returned audio is **consistently pitch-shifted
down, full of digital noise, and accrues ~0.5 s (~24 000 samples) of latency**. Switching the
host between 44.1 and 48 kHz changes nothing because the device advertises **only 48 kHz**, so the
aggregate always resamples. The defect is in the **transport clock model**, independent of the DSP
effect (the dry, unprocessed signal is affected identically to the wet signal).

Because the device's only timebase is the USB frame clock, the correct model is **synchronous**:
declare the endpoints synchronous and deliver/consume exactly the nominal number of audio frames
per 1 ms USB frame, so the host locks to the USB clock and does not resample the device for rate.
This **supersedes** two prior decisions of the base feature: **D20** (no feedback endpoint —
replaced by an explicit *synchronous* declaration, **not** by adding feedback, which a
converter-less device has no clock to justify) and **D4** (single 48 kHz / 16-bit format). It also
makes the base feature's deferred ring-capacity/startup-fill measurement (prior tasks **T062 /
T063**) **load-bearing and in scope**.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Play an instrument through the device with correct pitch and no noise (Priority: P1) 🎯 MVP

A musician routes a DAW track through the acfx device (host → device → host) inside an aggregate
and monitors the return. The audio comes back at the **same pitch** it went in and is **free of
digital noise** — the device behaves as a clean real-time insert effect, not a broken sample-rate
converter.

**Why this priority**: This is the defect that makes the device unusable today. Without it the
product does not function as a real-time effect at all.

**Independent Test**: With an objective host-side USB packet-size capture, confirm the IN endpoint
delivers a steady nominal frame count per USB frame (no zero-length / short packets in steady
state). Then, in a DAW aggregate, confirm a sustained tone returns at the same fundamental
frequency with no added broadband noise.

**Acceptance Scenarios**:

1. **Given** the device streaming full-duplex at 48 kHz inside an aggregate, **When** a sustained
   tone is played through it, **Then** the returned tone's fundamental matches the input (no
   consistent downward pitch offset) and carries no added digital noise.
2. **Given** a host-side USB packet capture during steady-state streaming, **When** the IN
   endpoint's per-frame packet sizes are inspected, **Then** they hold a steady nominal count
   (no zero-length or systematically short packets) locked to the USB frame cadence.
3. **Given** the device declared synchronous, **When** the host queries the endpoint
   synchronization type, **Then** both isochronous endpoints report *Synchronous*.

---

### User Story 2 — Run natively at the project's sample rate, 44.1 or 48 kHz (Priority: P1)

A user working at 44.1 kHz (or 48 kHz) selects that rate for the device; the device runs
**natively** at it, so the host does not have to hard-convert the project rate to a fixed device
rate.

**Why this priority**: Multi-rate is a stated requirement and is coupled to the fix — a
synchronous device must deliver the correct nominal-per-frame cadence for the *selected* rate
(including the fractional 44/45 cadence at 44.1 kHz), and native rate support minimises the
aggregate's resampling burden.

**Independent Test**: Select each rate from the host and confirm (via USB capture) the device
clock source reports and honours the selected rate, delivering the correct average frames/second
(48 000 at 48 kHz; 44 100 via an alternating 44/45 per-frame cadence at 44.1 kHz).

**Acceptance Scenarios**:

1. **Given** the device enumerated, **When** the host queries the clock source's supported
   frequencies, **Then** both 44 100 and 48 000 Hz are offered.
2. **Given** the host selects 44.1 kHz, **When** the device streams, **Then** the IN endpoint
   delivers an alternating 44/45 frames-per-USB-frame cadence averaging 44 100 Hz, and the return
   is pitch-correct and noise-free.
3. **Given** a live rate change from the host, **When** it occurs, **Then** the device re-prepares
   its audio processing at the new rate and resumes streaming at that rate without requiring a
   re-plug or power cycle.

---

### User Story 3 — Select 16-bit or 24-bit format (Priority: P2)

A user selects 16-bit or 24-bit for the device; the device streams that bit depth in both
directions.

**Why this priority**: 24-bit is an operator-requested capability that adds headroom, but it is
secondary to a correct, usable transport and is subject to a hardware feasibility gate (see
Assumptions) that could constrain it.

**Independent Test**: Select each bit depth from the host and confirm the device advertises and
streams it, with the returned audio bit-accurate for a known signal within the format's
resolution.

**Acceptance Scenarios**:

1. **Given** the device enumerated, **When** the host queries supported formats, **Then** both
   16-bit and 24-bit stereo are offered at each supported rate (subject to the feasibility gate).
2. **Given** the host selects 24-bit, **When** the device streams, **Then** samples round-trip at
   24-bit resolution and the transport remains pitch-correct and noise-free.
3. **Given** a live format change from the host, **When** it occurs, **Then** the device resumes
   streaming in the new format without a re-plug or power cycle.

---

### User Story 4 — Low, host-compensated latency (Priority: P2)

A user playing in real time experiences low round-trip latency, and the DAW can compensate for the
device's reported latency so recorded material lines up.

**Why this priority**: A real-time effect must be low-latency to be playable; the current ~0.5 s is
unusable. Reporting device latency lets the host align tracks.

**Independent Test**: Measure the round-trip latency and confirm it is a small, bounded value
(dominated by USB framing + a minimal ring cushion, not hundreds of milliseconds); confirm the
device reports a latency value the host reads.

**Acceptance Scenarios**:

1. **Given** a clean synchronous stream, **When** round-trip latency is measured, **Then** it is a
   small bounded value driven by USB framing plus a minimal buffer cushion, not the prior ~0.5 s.
2. **Given** the device enumerated, **When** the host reads the device's reported audio latency,
   **Then** a non-zero latency figure is present for the DAW's delay compensation.

---

### User Story 5 — Objective, board-independent verification of transport health (Priority: P3)

A developer verifies the transport is delivering a clean nominal-per-frame stream **at the USB
level**, independent of any host resampler or DAW, so a regression is caught without relying on a
subjective listen.

**Why this priority**: The defect was invisible to the existing signal-based HIL harness (single
device, no aggregate, noise-only signals). An objective USB-level check is the durable guard.

**Independent Test**: Run a host-side USB packet-size capture against the streaming device and
report the distribution of IN-endpoint packet sizes per USB frame; a healthy transport shows a
tight steady nominal distribution with no zero-length/short packets in steady state.

**Acceptance Scenarios**:

1. **Given** the device streaming at a selected rate, **When** the USB packet capture runs, **Then**
   it reports the per-frame IN packet-size distribution and flags any zero-length/short packets.
2. **Given** a healthy synchronous transport, **When** the capture is evaluated, **Then** steady
   state shows the expected nominal cadence for the selected rate and zero starvation packets.

### Edge Cases

- **Host rate change mid-stream** while audio is flowing: the device must re-prepare and resume at
  the new rate without corrupting the stream, dropping the device, or requiring a re-plug.
- **Host format (bit-depth) change mid-stream**: same requirement as a rate change.
- **Startup / cold buffers**: the transport must not empty its output buffer at startup and seed a
  permanent underrun (the diagnosed cold-FIFO greedy-drain); it must reach a cushioned steady
  state before, or gracefully during, the first host reads.
- **Momentary buffer shortfall** in steady state: a brief shortfall must degrade gracefully
  (bounded, counted) without a rate/pitch shift — i.e. the device holds the nominal per-frame
  cadence rather than sending a short packet.
- **Suspend/resume and stream open/close** (the base feature's US10 lifecycle) must continue to
  work with the new synchronous, multi-format transport.
- **24-bit at 48 kHz** exceeding the device's USB FIFO/bandwidth budget (see Assumptions).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The device MUST present both isochronous audio endpoints as **Synchronous** (locked
  to the USB frame clock), so the host does not resample the device for rate.
- **FR-002**: The device MUST deliver, on the device→host (IN) endpoint, a **steady nominal number
  of audio frames per USB frame** for the selected rate — 48 per frame at 48 kHz, and an
  alternating cadence averaging 44.1 per frame at 44.1 kHz — locked to the USB frame cadence, with
  **no zero-length or systematically short packets in steady state**.
- **FR-003**: The device MUST consume, on the host→device (OUT) endpoint, the host's nominal
  per-frame delivery for the selected rate without accumulating or dropping frames in steady state.
- **FR-004**: The device MUST advertise a clock source supporting **both 44 100 Hz and 48 000 Hz**
  and MUST honour the host's rate selection (report supported frequencies; accept the host setting
  the current frequency).
- **FR-005**: The device MUST advertise **both 16-bit and 24-bit** stereo formats at each supported
  rate and MUST honour the host's format selection — **subject to the FR-014 feasibility gate**.
- **FR-006**: On a host **rate or format change**, the device MUST re-prepare its compiled-in audio
  processing at the new sample rate and reset its audio buffers, then resume streaming in the new
  rate/format **without requiring a re-plug or power cycle** (a lifecycle event, consistent with the
  base feature's suspend/resume handling).
- **FR-007**: The device MUST eliminate the startup path that empties its output buffer before the
  first host reads (the diagnosed cold-buffer greedy-drain), reaching a cushioned steady state so
  paced reads are served real audio rather than substituted silence.
- **FR-008**: The device MUST keep its output buffer cushioned in steady state such that the
  nominal-per-frame IN delivery is served from real audio and does not starve; the buffer
  capacity, startup fill, and water marks MUST be **derived from measurement** (the base feature's
  deferred T062/T063 procedure), not left at the generous placeholder values.
- **FR-009**: The device MUST **report its audio latency** to the host (the USB audio class latency
  descriptor field) so the host can perform delay compensation, and MUST **minimise** the
  round-trip latency (dominated by USB framing plus a minimal buffer cushion).
- **FR-010**: The device MUST convert between the USB wire format and the internal float audio for
  **both 16-bit and 24-bit** sample formats (a 24-bit path alongside the existing 16-bit one),
  preserving sample accuracy within each format's resolution.
- **FR-011**: The transport MUST remain correct for the **dry (unprocessed) signal** as well as the
  processed signal — the fix is in the transport, independent of the compiled-in effect.
- **FR-012**: The device MUST continue to satisfy the base feature's lifecycle behaviours
  (enumeration, suspend/resume, stream open/close, capture-only) under the new synchronous,
  multi-format transport.
- **FR-013**: A **host-side USB packet-size capture** MUST be provided that objectively records the
  IN-endpoint per-USB-frame packet sizes and flags zero-length/short packets, usable as the
  regression guard for FR-002 independent of any host resampler.
- **FR-014** *(feasibility gate — operator-owned scope)*: 24-bit at 48 kHz stereo in + out MUST be
  verified to fit the device's USB endpoint FIFO-RAM budget and full-speed USB bandwidth. If it
  does not fit, the 24-bit scope (FR-005/FR-010) is renegotiated with the operator — the fallback is
  16-bit-only, or 24-bit restricted to 44.1 kHz — a decision surfaced to the operator, never cut
  silently.
- **FR-015**: This feature **supersedes** base-feature decisions **D20** (no feedback endpoint) and
  **D4** (single 48 kHz / 16-bit format); the superseding rationale MUST be recorded so the base
  spec's decisions are not read as still-authoritative.

### Key Entities

- **Clock source**: the device's advertised sample-clock, now multi-rate (44.1 / 48 kHz), whose
  current frequency the host selects and the device honours.
- **Audio format**: the advertised wire format (bit depth × channels) the host selects (16/24-bit
  stereo).
- **IN (device→host) stream**: the processed-audio return, which must carry a steady nominal
  frame count per USB frame for the selected rate.
- **OUT (host→device) stream**: the input audio, consumed at the host's nominal per-frame rate.
- **Audio rings**: the input/output buffers decoupling USB from the DSP, whose capacity, startup
  fill, and water marks are now measurement-derived and load-bearing for starvation-free delivery.
- **Rate/format-change event**: the lifecycle transition that re-prepares the DSP and resets the
  buffers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A sustained tone played through the device returns with its fundamental frequency
  **matching the input** (no consistent downward pitch offset) at both 44.1 and 48 kHz.
- **SC-002**: In steady-state streaming, the device→host stream shows **zero zero-length/short
  packets** and holds the expected nominal per-USB-frame cadence for the selected rate, as measured
  by the host-side USB packet capture.
- **SC-003**: The returned signal carries **no added broadband digital noise** beyond the selected
  format's quantisation floor (i.e. no starvation/silence-substitution artefacts).
- **SC-004**: Round-trip latency is a **small bounded value** dominated by USB framing plus a
  minimal buffer cushion (not the prior ~0.5 s / ~24 000 samples), and the device reports a latency
  figure the host reads for compensation.
- **SC-005**: The device streams correctly at **all in-scope rate × bit-depth combinations**
  (44.1/48 kHz × 16/24-bit, subject to FR-014), including a **live rate/format change without a
  re-plug**.
- **SC-006**: **Operator acceptance** — a software instrument played through the device in a DAW
  (CoreAudio aggregate + I/O plugin) is usable as a real-time effect: same pitch, no noise, low
  latency, at both rates and both bit depths.

## Assumptions

- The device has **no analog converter**; its only timebase is the USB frame clock, which is why a
  synchronous model is correct and a feedback endpoint is unnecessary (D20 is superseded by an
  explicit synchronous declaration, not by adding feedback). A future hardware revision with a real
  codec and its own crystal would be the trigger to revisit the async-with-feedback model.
- The host does the analog conversion (the device is monitored through another interface, typically
  in an aggregate); CoreAudio's aggregate will still perform gentle drift-correction between the
  device (USB-clocked) and the master interface — that is normal and stable, not the pathological
  stretch this feature removes.
- **88.2 / 96 kHz are OUT of scope** (they likely exceed full-speed USB bandwidth / the device's
  FIFO budget for stereo in + out).
- The 24-bit feasibility (FR-014) rests on the device's USB endpoint FIFO-RAM budget: 24-bit at
  48 kHz stereo roughly 1.5×'s the 16-bit per-frame byte count. The planning research MUST confirm
  the budget before committing 24-bit; if it does not fit, the operator decides the fallback.
- Verification runs through (a) a host-side USB packet capture as the objective gate and (b) the
  operator's DAW aggregate as acceptance; the existing single-device, noise-signal HIL harness does
  **not** reproduce this class of defect and is insufficient on its own.
- The compiled-in DSP effect and the block engine are rate-parameterised via the existing
  `prepare(sampleRate)` seam; the transport change re-invokes it on rate change and is otherwise
  independent of which effect is compiled in.
