// Pure UI<->DSP parameter mapping for the SVF demo. DOM-free and side-effect
// free so it is trivially typecheckable and reusable (main thread only).
//
// The C++ ABI's cutoff descriptor is a LOG-skewed param over [20 Hz, 20 kHz]
// (core/effects/svf/svf-effect.h, kParams[kCutoff]); resonance is LINEAR over
// [0, 1]; mode is a discrete 0/1/2 bucket. These helpers convert between the
// human-facing Hz / 0..1 values the UI shows and the normalized [0, 1] the
// engine's `setParam` expects — matching the DSP's own `denormalize`.

export const CUTOFF_MIN_HZ = 20;
export const CUTOFF_MAX_HZ = 20_000;

const CUTOFF_LOG_RANGE = Math.log(CUTOFF_MAX_HZ / CUTOFF_MIN_HZ);

/** UI filter-mode identifiers (mirror `SvfFilterMode` in the artifact registry). */
export type SvfDemoMode = 'lowpass' | 'highpass' | 'bandpass';

/** The three modes, in the ABI's discrete-bucket order (LP=0, HP=1, BP=2). */
export const SVF_DEMO_MODES: readonly SvfDemoMode[] = ['lowpass', 'highpass', 'bandpass'];

/** The engine's `SvfMode` (0|1|2) for a UI mode id. */
export function modeToSvfMode(mode: SvfDemoMode): 0 | 1 | 2 {
  switch (mode) {
    case 'lowpass':
      return 0;
    case 'highpass':
      return 1;
    case 'bandpass':
      return 2;
  }
}

/** Short faceplate label for a mode. */
export function modeLabel(mode: SvfDemoMode): string {
  switch (mode) {
    case 'lowpass':
      return 'LOW PASS';
    case 'highpass':
      return 'HIGH PASS';
    case 'bandpass':
      return 'BAND PASS';
  }
}

function clamp01(value: number): number {
  if (Number.isNaN(value)) return 0;
  if (value < 0) return 0;
  if (value > 1) return 1;
  return value;
}

/** Log-map a cutoff in Hz to the normalized [0, 1] the ABI expects. */
export function cutoffHzToNormalized(hz: number): number {
  if (!Number.isFinite(hz) || hz <= CUTOFF_MIN_HZ) return 0;
  if (hz >= CUTOFF_MAX_HZ) return 1;
  return Math.log(hz / CUTOFF_MIN_HZ) / CUTOFF_LOG_RANGE;
}

/** Inverse of {@link cutoffHzToNormalized}: normalized [0, 1] -> Hz. */
export function normalizedToCutoffHz(normalized: number): number {
  return CUTOFF_MIN_HZ * Math.exp(CUTOFF_LOG_RANGE * clamp01(normalized));
}

/** Human-facing cutoff readout, e.g. `220 Hz`, `1.00 kHz`, `12.4 kHz`. */
export function formatCutoff(hz: number): string {
  if (hz >= 1000) {
    const khz = hz / 1000;
    return `${khz.toFixed(khz >= 10 ? 1 : 2)} kHz`;
  }
  return `${Math.round(hz)} Hz`;
}

/** Human-facing resonance readout (0..1), e.g. `Q 0.10`. */
export function formatResonance(resonance: number): string {
  return `Q ${clamp01(resonance).toFixed(2)}`;
}
