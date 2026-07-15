import { describe, expect, it } from "vitest";
import { SvfModule, type Complex, type PoleZero } from "../loader/svf-module.ts";

// Cross-validation: prove the analytic poles/zeros (svf_get_pole_zero, derived
// from DaisySP's real Svf difference equations) are AUTHORITATIVE by
// reconstructing |H(e^jw)| from them and asserting it matches the MEASURED
// magnitude response (svf_get_frequency_response, which DFTs the real
// SvfEffect's impulse response). If the derivation were a "lookalike" the two
// would diverge (Principle VII).

const wasmModuleUrl = new URL("../../../build/web/svf.mjs", import.meta.url).href;
const SAMPLE_RATE = 48000;

// |e^{jw} - c| for a complex c.
function distToUnitCircle(w: number, c: Complex): number {
  const re = Math.cos(w) - c.re;
  const im = Math.sin(w) - c.im;
  return Math.hypot(re, im);
}

// Analytic |H(e^jw)| = |gain| * prod|e^jw - zero| / prod|e^jw - pole|.
function analyticMagnitude(pz: PoleZero, freqHz: number): number {
  const w = (2 * Math.PI * freqHz) / SAMPLE_RATE;
  let num = Math.abs(pz.gain);
  for (const z of pz.zeros) num *= distToUnitCircle(w, z);
  let den = 1;
  for (const p of pz.poles) den *= distToUnitCircle(w, p);
  return num / den;
}

// Log-spaced probe frequencies within the SVF's valid band (< sr*0.32).
function probeFrequencies(): Float32Array {
  const n = 96;
  const fMin = 40;
  const fMax = 14000;
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / (n - 1);
    out[i] = fMin * Math.pow(fMax / fMin, t);
  }
  return out;
}

interface Setting {
  cutoffNorm: number;
  resNorm: number;
  modeNorm: number;
  label: string;
}

// mode norm buckets (linear 0..2 over 3 buckets): <1/3 -> lp, <2/3 -> hp, else bp.
const SETTINGS: Setting[] = [
  { cutoffNorm: 0.5, resNorm: 0.0, modeNorm: 0.0, label: "lowpass, mid cutoff, no res" },
  { cutoffNorm: 0.3, resNorm: 0.3, modeNorm: 0.0, label: "lowpass, low cutoff, some res" },
  { cutoffNorm: 0.7, resNorm: 0.6, modeNorm: 0.0, label: "lowpass, high cutoff, more res" },
  { cutoffNorm: 0.5, resNorm: 0.0, modeNorm: 0.5, label: "highpass, mid cutoff, no res" },
  { cutoffNorm: 0.6, resNorm: 0.5, modeNorm: 0.5, label: "highpass, high cutoff, res" },
  { cutoffNorm: 0.5, resNorm: 0.3, modeNorm: 0.9, label: "bandpass, mid cutoff, res" },
  { cutoffNorm: 0.35, resNorm: 0.6, modeNorm: 0.9, label: "bandpass, low cutoff, more res" },
];

describe("SVF analytic pole/zero vs measured magnitude response", () => {
  it("analytic |H| reconstructed from poles/zeros matches the measured response", async () => {
    const mod = await SvfModule.load(wasmModuleUrl);
    const freqs = probeFrequencies();
    // dB floor: below this the measured response is deep stopband where absolute
    // dB error is not meaningful (both curves are near-zero); compared above it.
    const FLOOR_DB = -60;
    let globalMaxDb = 0;

    for (const s of SETTINGS) {
      const h = mod.create();
      mod.prepare(h, SAMPLE_RATE, 2048, 1);
      mod.setParam(h, 0, s.cutoffNorm);
      mod.setParam(h, 1, s.resNorm);
      mod.setParam(h, 2, s.modeNorm);

      const pz = mod.getPoleZero(h);
      const measured = mod.getFrequencyResponse(h, freqs);
      mod.destroy(h);

      expect(pz.poles.length).toBe(2);
      expect(pz.zeros.length).toBe(2);
      // Stability: poles strictly inside the unit circle.
      for (const p of pz.poles) {
        expect(Math.hypot(p.re, p.im)).toBeLessThan(1);
      }

      let settingMaxDb = 0;
      for (let i = 0; i < freqs.length; i++) {
        const analytic = analyticMagnitude(pz, freqs[i]!);
        const meas = measured[i]!;
        const measDb = 20 * Math.log10(Math.max(meas, 1e-12));
        const anaDb = 20 * Math.log10(Math.max(analytic, 1e-12));
        if (measDb < FLOOR_DB && anaDb < FLOOR_DB) continue; // deep stopband: skip
        const errDb = Math.abs(measDb - anaDb);
        settingMaxDb = Math.max(settingMaxDb, errDb);
      }
      globalMaxDb = Math.max(globalMaxDb, settingMaxDb);
      // Per-setting sanity: the analytic model tracks the measured DSP tightly.
      expect(settingMaxDb, `setting "${s.label}" max |ΔdB|`).toBeLessThan(0.05);
    }

    // eslint-disable-next-line no-console
    console.log(`analytic-vs-measured max error over all settings: ${globalMaxDb.toFixed(4)} dB`);
    expect(globalMaxDb).toBeLessThan(0.05);
  });
});
