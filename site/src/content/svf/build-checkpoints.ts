// Build-along checkpoint ladder data (FR-001 "Build it with acfx"; addresses
// analyze finding U1 — the checkpoints must be ENUMERATED, not hand-waved).
//
// Each checkpoint is a REAL repository milestone the SVF actually travelled on
// its `lab -> primitive -> effect -> adapter` ladder, with the real repo path
// it lives at and a concrete, independently-runnable way to verify the state is
// green. These are history, not invented tutorial steps: every `repoPath` here
// exists in the tree, and every `verify` names a command or file a reader can
// run/open today.
//
// Strictly typed (Principle IX): a bad field is a compile error, and the ladder
// component renders exactly these — no free-text drift between prose and data.

/** One rung of the build-along ladder. */
export interface BuildCheckpoint {
  /** Ladder stage — the rung's short name in the acfx effect ladder. */
  readonly stage: 'lab' | 'primitive' | 'effect' | 'adapter';
  /** Human title of the milestone. */
  readonly title: string;
  /** Real repo-relative path this rung lives at (exists in the tree). */
  readonly repoPath: string;
  /** What became true at this milestone — the state the reader is looking at. */
  readonly milestone: string;
  /** A concrete, independently-runnable way to confirm the rung is green. */
  readonly verify: string;
}

/**
 * The four real rungs the SVF travelled, in development order. This IS the
 * enumerated checkpoint list the "Build it with acfx" section replays.
 */
export const svfBuildCheckpoints: readonly BuildCheckpoint[] = [
  {
    stage: 'lab',
    title: 'Derive it in the lab',
    repoPath: 'core/labs/state-variable-filter/',
    milestone:
      'The lab derives the Chamberlin SVF recurrence (HP/BP/LP/notch in lockstep) and records the design decision to wrap DaisySP’s proven Svf rather than re-implement the math — keeping core/ free of any platform headers.',
    verify:
      'Read core/labs/state-variable-filter/README.md (Theory + Walkthrough); build & run the lab harness at harness/svf-harness.cpp.',
  },
  {
    stage: 'primitive',
    title: 'Wrap it as a primitive',
    repoPath: 'core/primitives/filters/svf-primitive.h',
    milestone:
      'acfx::SvfPrimitive is a thin, allocation-free wrapper over daisysp::Svf that owns mode selection (LP/HP/BP) and reset; the per-sample math stays DaisySP’s. The host test is green.',
    verify:
      'Run the core test suite — tests/core/svf-test.cpp asserts lowpass passes lows, highpass passes highs, bandpass emphasises the centre, and high resonance stays NaN/denormal-free and bounded.',
  },
  {
    stage: 'effect',
    title: 'Lift it to an effect',
    repoPath: 'core/effects/svf/svf-effect.h',
    milestone:
      'acfx::SvfEffect satisfies the Effect contract with no base class and no vtable in the hot path: one constexpr parameter table (cutoff/resonance/mode) drives every adapter, and a lock-free atomic hand-off lets setParameter() be called from any thread without ever racing process(). process() is allocation-free.',
    verify:
      'Open core/effects/svf/svf-effect.h — the kParams table and the atomic pending-value hand-off at the top of process() are the contract; the host tests exercise them.',
  },
  {
    stage: 'adapter',
    title: 'Compile it for the web',
    repoPath: 'adapters/web',
    milestone:
      'adapters/web compiles the real SVF target to WebAssembly, depending only inward on acfx_core (core/ is untouched and gains no web knowledge). The audio ABI runs in an AudioWorklet and an analysis ABI exposes frequency-response / pole-zero / impulse. The WASM audio path is proven equal to the native reference by a parity test.',
    verify:
      'Run the web-adapter vitest — adapters/web/test/svf-parity.test.ts compares the native reference and the WASM module against shared, versioned vectors (test/vectors/lowpass-sweep.json), with no numeric constants copied into the test.',
  },
];
