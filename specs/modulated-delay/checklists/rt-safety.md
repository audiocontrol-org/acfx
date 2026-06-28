> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Real-Time Safety & DSP Correctness Checklist: Modulated Delay

**Purpose**: Unit-test the *requirements* (spec.md) for real-time audio safety and
DSP correctness — are they complete, clear, consistent, and measurable? This does
NOT test the implementation; it interrogates the written requirements.
**Created**: 2026-06-28
**Feature**: [spec.md](../spec.md)

## Real-Time Safety Requirements

- [ ] CHK001 Are the allocation-free and lock-free obligations for `process()` stated as testable requirements? [Completeness, Spec §FR-006, SC-007]
- [ ] CHK002 Is "preallocated in `prepare()`" specified for *every* buffer the feature introduces (main delay AND the wow & flutter line)? [Completeness, Spec §FR-006, §FR-021]
- [ ] CHK003 Is the boundary of which methods may run on the audio thread vs only while the stream is stopped (`prepare`/`reset`) clearly stated? [Clarity, Spec Edge Cases]
- [ ] CHK004 Is "bounded work per sample" expressed measurably (no unbounded loops/recursion in the audio path) rather than as an adjective? [Measurability, Spec §FR-006]
- [ ] CHK005 Are the lock-free cross-thread parameter-edit semantics (publish pending, consume at block top, never race `process()`) specified as a requirement, not left implicit? [Completeness, Spec §FR-023]

## Buffer Sizing & In-Range Reads

- [ ] CHK006 Is the maximum delay time a single, unambiguous value driving buffer size at `prepare()`? [Clarity, Spec §FR-009]
- [ ] CHK007 Do the requirements state behavior when a delay-time request exceeds the maximum (bounded, not undefined/fallback)? [Edge Case, Spec §FR-009]
- [ ] CHK008 Is the in-range-read guarantee stated for *all* modulation and parameter combinations, including modulation driving the read toward 0 and toward the maximum? [Coverage, Spec §FR-007, §FR-014]
- [ ] CHK009 Is the in-range-read guarantee also stated for the wow & flutter line specifically? [Coverage, Spec §FR-021]
- [ ] CHK010 Is the fractional/interpolated-read requirement (smooth swept delay, no zipper) stated measurably rather than as "smooth"? [Measurability, Spec §FR-008]
- [ ] CHK011 Is buffer sizing specified as sample-rate-dependent (2 s at the prepared rate) so the max delay *time* is invariant across rates? [Consistency, Spec §FR-009, §FR-015]

## Feedback Stability

- [ ] CHK012 Is the feedback stability bound specified (what "does not diverge into uncommanded runaway" means) measurably? [Measurability, Spec §FR-010, SC-002]
- [ ] CHK013 Are the requirements clear that high author-commanded feedback combined with a resonant in-loop filter stays bounded? [Clarity, Spec Edge Cases, §FR-010]
- [ ] CHK014 Is it specified that bounding feedback is a defined behavior, not a silent fallback/clamp that hides intent (Constitution V)? [Consistency, Spec §FR-010]

## Modulation Semantics

- [ ] CHK015 Is the three-independent-LFO architecture (delay, cutoff, resonance; each own rate/depth/shape) stated unambiguously? [Clarity, Spec §FR-011, §FR-012, Clarifications]
- [ ] CHK016 Is depth-zero equivalence ("indistinguishable from unmodulated") specified for every modulated destination, regardless of that source's rate/shape? [Completeness, Spec §FR-013]
- [ ] CHK017 Is the selectable waveform set {sine, triangle, saw, random} enumerated as a requirement? [Completeness, Spec §FR-012a, Clarifications]
- [ ] CHK018 Are the wow and flutter components specified with independent rate AND depth (four controls), consistent with the clarification? [Consistency, Spec §FR-018, Clarifications]
- [ ] CHK019 Is the no-audible-stepping/zipper requirement across the supported rate and depth ranges measurable? [Measurability, Spec §FR-016]

## Sample-Rate Independence

- [ ] CHK020 Is sample-rate independence specified for both delay time AND modulation rate (same musical result at 44.1/48/96 kHz)? [Coverage, Spec §FR-015, SC-009]
- [ ] CHK021 Are the verification sample rates enumerated so the requirement is objectively testable? [Measurability, Spec SC-009]

## Single Source of Parameter Truth

- [ ] CHK022 Is the single-constexpr-parameter-table requirement stated, with every author-facing control enumerated exactly once? [Completeness, Spec §FR-022]
- [ ] CHK023 Are the discrete parameters (filter mode, the three modulation shapes) identified so descriptor validity (count ≥ 2) is checkable? [Clarity, Spec §FR-022, data-model]
- [ ] CHK024 Is the requirement that the same table drives every adapter (no per-adapter control definitions) stated? [Consistency, Spec §FR-022, SC-008]

## Cross-Platform & Testability

- [ ] CHK025 Is the "no platform headers in core" requirement restated for the new primitives and effect? [Consistency, Spec §FR-024]
- [ ] CHK026 Is the "runs unchanged on every target" parity requirement stated and tied to a measurable outcome? [Measurability, Spec §FR-001, SC-008]
- [ ] CHK027 Is host-side testability required for each behavior (timing/in-range, feedback shaping, modulation movement, wow/flutter, no-alloc)? [Coverage, Spec §FR-026, SC-006/007]
- [ ] CHK028 Is the module-size / strict-typing constraint stated so the effect is decomposed into small units? [Completeness, Spec §FR-025]

## Channel Handling

- [ ] CHK029 Is behavior at 1 and 2 channels defined, including whether modulation is correlated across channels? [Coverage, Spec §FR-027, Assumptions]
- [ ] CHK030 Is the channel-count source (`prepare()` context) and its upper bound clearly specified consistent with the SVF effect? [Consistency, Spec §FR-027]

## Notes

- Check items off as the spec is confirmed to satisfy each: `[x]`.
- A failing item means the *requirement text* needs work (clarify/complete/reconcile),
  not the code.
- All items carry a traceability reference (`Spec §…`, `Clarifications`, `data-model`)
  or a quality marker, per the ≥80% traceability bar.
