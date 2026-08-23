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

# Real-Time Safety & Transport Correctness Checklist: NUCLEO-F446RE adapter

**Purpose**: Validate that the *requirements* governing real-time safety and USB-audio transport
correctness are complete, unambiguous, consistent, and measurable — before implementation makes
their gaps expensive. These are unit tests for the spec's English, not for the firmware.

**Created**: 2026-08-23
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [data-model.md](../data-model.md)

**Depth**: Standard · **Audience**: Reviewer (PR) · **Focus**: the properties that are cheap to
get wrong and expensive to detect.

**Scope note**: The spec's **11 open questions are deliberately open** and are NOT treated as
defects by this checklist. Items below distinguish "unspecified because the operator decided it
is not yet knowable" from "unspecified because nobody noticed" — only the latter is a finding.

---

## Real-Time Safety Boundary

- [ ] CHK001 Is "the audio path" — the region in which no allocation and no locks are permitted — explicitly delimited, rather than left to the reader's inference? [Ambiguity, Spec §FR-030]
- [ ] CHK002 Are the no-allocation and no-lock requirements stated for the parameter flush and the telemetry write, or only for conversion and buffering? [Coverage, Spec §FR-030, §FR-033a]
- [ ] CHK003 Is the requirement that `acfx_nucleo_support` MUST NOT include platform headers (TinyUSB, CMSIS, board) stated as a *requirement*, or does it exist only in the plan and contract? [Gap, Traceability, Spec §FR-002/§FR-003]
- [ ] CHK004 Is the single-execution-context assumption stated with its falsification trigger, so a future change cannot silently invalidate the lock-free design? [Completeness, Spec §FR-046/§FR-047]
- [ ] CHK005 Are the real-time constraints expressed so that the existing allocation-sentinel discipline can mechanically enforce them? [Measurability, Spec §SC-010]

## Substitution Semantics — "Defined and Counted, Never Silent"

- [ ] CHK006 Is every substitution path paired with exactly one named counter, with no path left uncounted? [Completeness, Spec §FR-031/§FR-032]
- [ ] CHK007 **Is the capture-only case unambiguous about *which* counter increments?** FR-029 assigns capture-only silence to `inputStarved`, while FR-031 assigns "USB polled IN, output ring empty" to `outputUnderruns` — the same physical event appears to satisfy both descriptions. [Conflict, Spec §FR-029 vs §FR-031]
- [ ] CHK008 Is it specified whether a single event may increment more than one counter, or whether the counters are required to be mutually exclusive? [Ambiguity, Spec §FR-033]
- [ ] CHK009 Are counter **reset** semantics specified — may a host reset them between HIL runs, or are they strictly power-cycle-scoped? [Gap]
- [ ] CHK010 Is behaviour at **counter overflow** defined? These are 32-bit counters; `blocksProcessed` wraps after roughly 49 days of continuous streaming, which silently corrupts every derived rate. [Edge Case, Gap, Spec §FR-034]
- [ ] CHK011 Is the justified departure from the no-fallbacks principle recorded as a requirement with its reasoning, rather than only as commentary? [Traceability, Spec §FR-032]
- [ ] CHK012 Is the telemetry-drop behaviour (nothing reading the CDC port) held to the same "defined" standard, and is its exemption from counting stated rather than assumed? [Consistency, Spec §Edge Cases]

## Packet Size & Block Decoupling

- [ ] CHK013 Is the accepted payload range stated as an inclusive bound with both endpoints named, leaving no room for an off-by-one reading? [Clarity, Spec §FR-028]
- [ ] CHK014 Is the prohibition on assuming 48 frames stated as a requirement, not merely as rationale? [Completeness, Spec §FR-028]
- [ ] CHK015 Is the relationship between the 49-frame prepare and the 48-frame working block stated explicitly enough that the two cannot be read as contradictory? [Consistency, Spec §FR-036 vs §FR-036a]
- [ ] CHK016 Are requirements defined for a **malformed payload** — a byte count that is not a whole number of stereo frames, or a torn transfer? [Gap, Edge Case]
- [ ] CHK017 Is the **startup policy** specified — whether the DSP begins drawing blocks immediately and underflows, or waits until the ring reaches its fill target? This is a *semantic* separable from the unpinned fill *value*. [Gap, Spec §FR-030a/§FR-035]
- [ ] CHK018 Are requirements defined for **recovery** after a sustained excursion — does the ring re-centre toward its target occupancy, or simply persist at the new offset? [Gap, Recovery Flow]
- [ ] CHK019 Is it specified that packet size must not propagate into block size, as a checkable property rather than an implementation preference? [Measurability, Spec §FR-030a]

## Sample Format Conversion

- [ ] CHK020 Is the scale factor stated as one value used in both directions, precluding an asymmetric 32767/32768 pairing? [Clarity, Spec §FR-038a]
- [ ] CHK021 Is the clamp specified as mandatory, with its consequence (wrap → loud broadband noise) recorded so it cannot be optimized away as defensive? [Completeness, Spec §FR-038a]
- [ ] CHK022 Is the rounding mode specified precisely enough to settle **ties** (exact .5 cases)? [Clarity, Spec §FR-038a]
- [ ] CHK023 Is the round-trip exactness property stated as a verifiable requirement over the full representable range? [Measurability, Spec §US2 AS2]
- [ ] CHK024 Is the spike's 0.999916 host-side gain explicitly excluded as a firmware target, so it is not mistaken for a defect to chase? [Assumption, Spec §Assumptions]

## Fail-Loud Initialization

- [ ] CHK025 Is the LED fault pattern specified concretely enough to be verified — or is "a distinct fault pattern" an unquantified adjective? [Ambiguity, Measurability, Spec §FR-015a]
- [ ] CHK026 Is the ordering requirement — LED initialized *before* clock validation — stated as a requirement, given it is the only thing that makes the fault reportable? [Gap, Traceability, Spec §FR-015a]
- [ ] CHK027 Is the reason no USB channel can carry a clock fault recorded, so a later reviewer does not "improve" it into a CDC message? [Completeness, Spec §FR-015a]
- [ ] CHK028 **Is fail-loud behaviour for a dead timing source stated as a requirement?** The obligation that `worstBlockMicros == 0` after processing must surface loudly currently exists only in `data-model.md` (I-TS4) and `research.md` (R6) — not in any FR. [Gap, Traceability, Spec §FR-034]
- [ ] CHK029 Are the two fail-loud paths (clock, timing source) consistent about what "loud" means, given one has no USB and the other does? [Consistency]
- [ ] CHK030 Is the prohibition on falling back to the internal oscillator stated as an absolute, with no permitted degraded-enumeration mode? [Clarity, Spec §FR-015/§SC-007]

## USB Lifecycle Coverage

- [ ] CHK031 **Are requirements defined for USB suspend and resume?** No requirement currently addresses a host suspending the device, or what the audio path and counters do across it. [Gap, Coverage, Exception Flow]
- [ ] CHK032 Are requirements defined for a **bus reset or re-enumeration mid-stream** — including whether ring contents and counters survive it? [Gap, Coverage, Recovery Flow]
- [ ] CHK033 Are alt-setting transitions (stream opened, closed, reopened) specified for every combination, or only for the capture-only case singled out by D22? [Coverage, Spec §FR-029]
- [ ] CHK034 Is it specified whether **every** effect firmware carries the CDC telemetry function, or only instrumented builds — given the flash-budget consequence? [Gap, Spec §FR-018a]
- [ ] CHK035 Is the endpoint budget across three functions recorded as an assumption to be validated rather than an established fact? [Assumption, Spec §Assumptions]

## Measurability & Traceability

- [ ] CHK036 Is every requirement that defers to measurement (FR-035) paired with a *procedure*, so "measurement-derived" is actionable rather than a placeholder? [Measurability, Spec §FR-035]
- [ ] CHK037 **Is FR-035 self-consistent with the plan?** FR-035 says the values are "pinned in the implementation plan"; the plan deliberately does not pin them, deferring to a post-hardware measurement phase. [Conflict, Spec §FR-035 vs §plan Phase H]
- [ ] CHK038 Is each of the 11 open questions traceable to the requirement(s) it leaves incomplete, so none is lost between artifacts? [Traceability, Spec §Open Questions]
- [ ] CHK039 Is it stated *where* `worstBlockMicros` is recorded for each shipped firmware, so SC-011 is objectively checkable? [Clarity, Measurability, Spec §SC-011]
- [ ] CHK040 Are the requirements distinguishable between "deliberately open" and "not yet written", so a reader can tell an operator decision from an oversight? [Clarity, Spec §Open Questions]

---

## Notes

- Check items off as reviewed: `[x]`. An item passing means **the requirement is well-written**,
  not that the firmware works.
- Items **CHK007**, **CHK010**, **CHK016**, **CHK017**, **CHK018**, **CHK028**, **CHK031**,
  **CHK032**, and **CHK037** are the ones that surfaced *substantive* gaps or conflicts during
  generation rather than confirming existing quality. They are the highest-value items here and
  are the natural input to a spec revision pass.
- The most consequential of those: **CHK007** (a genuine counter-assignment conflict between
  FR-029 and FR-031), **CHK028** (a binding fail-loud behaviour that lives outside the
  requirements entirely), and **CHK031/CHK032** (USB suspend/resume and bus reset are absent from
  the spec's coverage — an entire lifecycle class, not an edge case).
- Nothing here proposes cutting scope. Where an item finds a gap, the resolution is to *write the
  missing requirement*, and where the gap is a decision, to put it to the operator.
