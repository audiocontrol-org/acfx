// Build-along checkpoint ladder data ("Build it" section).
//
// The four layers the SVF is built from — `lab -> primitive -> effect ->
// adapter` — each with the repo file it lives in, a short description of what
// that layer does, and a way to run or read it. Every `repoPath` exists in the
// tree and every `verify` names a command or file a reader can run/open.
//
// Strictly typed (Principle IX): a bad field is a compile error, and the ladder
// component renders exactly these — no free-text drift between prose and data.

/**
 * A curated source excerpt for a rung — the pedagogically relevant lines of a
 * REAL repo file, read and highlighted at build time. Line numbers are REAL
 * (1-based, matching the file and GitHub); the viewer deep-links this exact
 * range. Ranges must be correct against the CURRENT file — a stale range fails
 * `astro build` (load-source is fail-loud), so this data cannot drift silently.
 */
export interface CheckpointCode {
  /** Repo-relative path to the file the excerpt is taken from. */
  readonly repoPath: string;
  /** Real 1-based first line of the excerpt (inclusive). */
  readonly startLine: number;
  /** Real 1-based last line of the excerpt (inclusive). */
  readonly endLine: number;
  /** One line naming what this excerpt teaches about the rung. */
  readonly caption: string;
}

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
  /** The curated source excerpt shown for this rung (real file + real lines). */
  readonly code: CheckpointCode;
}

/** The four layers the SVF is built from, from the math down to the browser. */
export const svfBuildCheckpoints: readonly BuildCheckpoint[] = [
  {
    stage: 'lab',
    title: 'Derive it in the lab',
    repoPath: 'core/labs/state-variable-filter/',
    milestone:
      'The lab works out the Chamberlin SVF recurrence (HP/BP/LP/notch computed together) and settles on wrapping DaisySP’s Svf rather than re-implementing the math.',
    verify:
      'Read core/labs/state-variable-filter/README.md (Theory + Walkthrough); build & run the lab harness at harness/svf-harness.cpp.',
    code: {
      repoPath: 'core/labs/state-variable-filter/README.md',
      startLine: 11,
      endLine: 39,
      caption:
        'The lab’s derivation itself: the Chamberlin recurrence (LP/BP/HP/notch in lockstep), the f/q coefficients and stability bounds — then the design decision to wrap DaisySP’s proven Svf rather than re-implement the math, so core/ stays free of platform headers.',
    },
  },
  {
    stage: 'primitive',
    title: 'Wrap it as a primitive',
    repoPath: 'core/primitives/filters/svf-primitive.h',
    milestone:
      'acfx::SvfPrimitive is a thin, allocation-free wrapper over daisysp::Svf that owns mode selection (LP/HP/BP) and reset; the per-sample math stays DaisySP’s.',
    verify:
      'Run the core test suite — tests/core/svf-test.cpp asserts lowpass passes lows, highpass passes highs, bandpass emphasises the centre, and high resonance stays NaN/denormal-free and bounded.',
    code: {
      repoPath: 'core/primitives/filters/svf-primitive.h',
      startLine: 15,
      endLine: 57,
      caption:
        'The whole primitive: the SvfMode enum plus the thin wrapper that owns mode/reset and forwards setFreq/setRes to daisysp::Svf — process() runs the DaisySP math once and dispatches to the selected tap. No allocation, no locks, no platform headers.',
    },
  },
  {
    stage: 'effect',
    title: 'Lift it to an effect',
    repoPath: 'core/effects/svf/svf-effect.h',
    milestone:
      'acfx::SvfEffect satisfies the Effect contract with no base class and no vtable in the hot path: one constexpr parameter table (cutoff/resonance/mode) drives every adapter, and a lock-free atomic hand-off lets setParameter() be called from any thread without ever racing process(). process() is allocation-free.',
    verify:
      'Open core/effects/svf/svf-effect.h — the kParams table and the atomic pending-value hand-off at the top of process() are the contract; the host tests exercise them.',
    code: {
      repoPath: 'core/effects/svf/svf-effect.h',
      startLine: 44,
      endLine: 113,
      caption:
        'The Effect contract, no base class in sight: one constexpr kParams table (the single source of parameter truth every adapter reads), a compile-time static_assert guarding it, an allocation-free process() that consumes cross-thread edits at the top, and a setParameter() that only publishes a lock-free atomic — so UI edits never race the audio thread.',
    },
  },
  {
    stage: 'adapter',
    title: 'Compile it for the web',
    repoPath: 'adapters/web',
    milestone:
      'adapters/web compiles the SVF to WebAssembly, depending only inward on acfx_core. The audio ABI runs in an AudioWorklet, and an analysis ABI provides the frequency response, poles and zeros, and impulse response. A parity test checks the WASM output against the native reference.',
    verify:
      'Run the web-adapter vitest — adapters/web/test/svf-parity.test.ts compares the native reference and the WASM module against shared, versioned vectors (test/vectors/lowpass-sweep.json), with no numeric constants copied into the test.',
    code: {
      repoPath: 'adapters/web/svf-web-abi.h',
      startLine: 6,
      endLine: 12,
      caption:
        'The flat extern-"C" boundary the WebAssembly build exposes: an opaque SvfHandle plus create/destroy/prepare/set_param/process. No C++ types cross the ABI, so the AudioWorklet glue calls the real core effect through a handful of C symbols.',
    },
  },
];
