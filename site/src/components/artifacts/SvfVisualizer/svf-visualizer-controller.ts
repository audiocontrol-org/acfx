// Client island for the SVF "Observe it" visualizer (T030).
//
// Three instrument wells — frequency response, z-plane pole/zero, impulse
// response — all painted from the REAL compiled analysis ABI (T025) as the
// learner sweeps cutoff / resonance / mode. There is NO filter math in this
// file: every curve comes from `SvfAnalysisWasm` (getFrequencyResponse /
// getPoleZero / renderImpulse), which the compiled `SvfEffect` computes
// (FR-003, Principle VII). Log/dB axes and marker geometry are presentation
// only.
//
// WASM sourcing (the integration crux, T031): the analysis wasm is loaded
// GLUE-FREE, straight from the manifest's CDN url (`data-analysis-wasm-url`,
// resolved by SvfVisualizer.astro from the `kind:"wasm"` asset whose
// `capabilities` include `"analysis"` — FR-014). svf.wasm imports only two
// host functions and exports its own memory (same pattern T026 proved for the
// AudioWorklet audio engine), so `SvfAnalysisWasm` (adapters/web/analysis)
// fetches + compiles + instantiates it directly, with NO dependency on the
// generated Emscripten glue (`svf.mjs`) or the local `build/web` Emscripten
// output — both are gitignored and only produced by a local Emscripten build,
// never on Netlify/CI, so depending on either would break the static-build
// contract. If the manifest has no analysis-capable wasm, or the load fails,
// the visible error + content fallback below kicks in (FR-015).

import { SvfAnalysisWasm } from '@acfx/web/analysis/svf-analysis-wasm.ts';
import { modeToNormalized, type SvfMode } from '@acfx/web/worklet/svf-engine-core.ts';

import {
  cutoffHzToNormalized,
  formatCutoff,
  formatResonance,
  modeLabel,
  modeToSvfMode,
  normalizedToCutoffHz,
  type SvfDemoMode,
} from '../SvfDemo/svf-param-map.ts';

import {
  drawEmptyPoleZero,
  drawFrequencyResponse,
  drawImpulse,
  drawPoleZero,
  type PlotSurface,
  type PlotTheme,
} from './svf-visualizer-plots.ts';

const SAMPLE_RATE = 48_000;
const RESPONSE_POINTS = 256;
const IMPULSE_SAMPLES = 512;
const CUTOFF_PARAM = 0;
const RESONANCE_PARAM = 1;
const MODE_PARAM = 2;

function isMode(value: string): value is SvfDemoMode {
  return value === 'lowpass' || value === 'highpass' || value === 'bandpass';
}

function analysisSupported(): boolean {
  return typeof WebAssembly === 'object';
}

function requireEl<T extends Element>(root: ParentNode, selector: string, ctor: new () => T): T {
  const el = root.querySelector(selector);
  if (!(el instanceof ctor)) {
    throw new Error(`svf-visualizer: missing or wrong element for "${selector}"`);
  }
  return el;
}

// --- Static content-fallback assets (real pre-generated core output) --------

interface ResponseAsset {
  readonly freqsHz: readonly number[];
  readonly magsDb: readonly number[];
}
interface ImpulseAsset {
  readonly samples: readonly number[];
}

function isNumberArray(value: unknown): value is number[] {
  return Array.isArray(value) && value.every((v) => typeof v === 'number');
}

async function fetchResponseAsset(url: string): Promise<ResponseAsset> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`response asset ${res.status}`);
  const json: unknown = await res.json();
  if (
    typeof json !== 'object' || json === null ||
    !isNumberArray((json as Record<string, unknown>)['freqsHz']) ||
    !isNumberArray((json as Record<string, unknown>)['magsDb'])
  ) {
    throw new Error('malformed response asset');
  }
  const rec = json as { freqsHz: number[]; magsDb: number[] };
  return { freqsHz: rec.freqsHz, magsDb: rec.magsDb };
}

async function fetchImpulseAsset(url: string): Promise<ImpulseAsset> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`impulse asset ${res.status}`);
  const json: unknown = await res.json();
  if (
    typeof json !== 'object' || json === null ||
    !isNumberArray((json as Record<string, unknown>)['samples'])
  ) {
    throw new Error('malformed impulse asset');
  }
  return { samples: (json as { samples: number[] }).samples };
}

// --- WASM analysis engine (real compiled SVF) -------------------------------

interface AnalysisResult {
  readonly freqsHz: Float32Array;
  readonly magsDb: Float32Array;
  readonly poles: readonly { re: number; im: number }[];
  readonly zeros: readonly { re: number; im: number }[];
  readonly impulse: Float32Array;
  readonly poleRadius: number;
}

class AnalysisEngine {
  private readonly freqs: Float32Array;

  private constructor(
    private readonly mod: SvfAnalysisWasm,
    private readonly handle: number,
  ) {
    this.freqs = new Float32Array(RESPONSE_POINTS);
    for (let i = 0; i < RESPONSE_POINTS; i++) {
      const t = i / (RESPONSE_POINTS - 1);
      this.freqs[i] = 20 * Math.pow(1000, t); // 20 Hz .. 20 kHz, log-spaced
    }
  }

  static async create(analysisWasmUrl: string): Promise<AnalysisEngine> {
    const mod = await SvfAnalysisWasm.load(analysisWasmUrl);
    const handle = mod.create();
    mod.prepare(handle, SAMPLE_RATE, IMPULSE_SAMPLES, 1);
    return new AnalysisEngine(mod, handle);
  }

  analyze(cutoffNorm: number, resonance: number, mode: SvfMode): AnalysisResult {
    this.mod.setParam(this.handle, CUTOFF_PARAM, cutoffNorm);
    this.mod.setParam(this.handle, RESONANCE_PARAM, resonance);
    this.mod.setParam(this.handle, MODE_PARAM, modeToNormalized(mode));

    const magsLinear = this.mod.getFrequencyResponse(this.handle, this.freqs);
    const magsDb = new Float32Array(magsLinear.length);
    for (let i = 0; i < magsLinear.length; i++) {
      magsDb[i] = 20 * Math.log10(Math.max(magsLinear[i] ?? 0, 1e-6));
    }
    const pz = this.mod.getPoleZero(this.handle);
    const lead = pz.poles[0];
    const poleRadius = lead === undefined ? 0 : Math.hypot(lead.re, lead.im);
    return {
      freqsHz: this.freqs,
      magsDb,
      poles: pz.poles,
      zeros: pz.zeros,
      impulse: this.mod.renderImpulse(this.handle, IMPULSE_SAMPLES),
      poleRadius,
    };
  }

  dispose(): void {
    this.mod.destroy(this.handle);
  }
}

// --- Canvas well (device-pixel-ratio aware) ---------------------------------

class Well {
  private ctx: CanvasRenderingContext2D;
  width = 0;
  height = 0;

  constructor(private readonly canvas: HTMLCanvasElement) {
    const ctx = canvas.getContext('2d');
    if (ctx === null) throw new Error('svf-visualizer: 2D canvas context unavailable');
    this.ctx = ctx;
    this.resize();
  }

  resize(): void {
    const rect = this.canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    this.width = Math.max(1, Math.round(rect.width));
    this.height = Math.max(1, Math.round(rect.height));
    this.canvas.width = this.width * dpr;
    this.canvas.height = this.height * dpr;
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  surface(): PlotSurface {
    return { ctx: this.ctx, width: this.width, height: this.height };
  }
}

// --- Panel orchestration ----------------------------------------------------

interface PanelConfig {
  /** Analysis-capable wasm url from the manifest (a CDN url — FR-014), if published. */
  readonly wasmUrl: string | undefined;
  readonly responseUrl: string | undefined;
  readonly impulseUrl: string | undefined;
  readonly initialCutoffHz: number;
  readonly initialResonance: number;
  readonly initialMode: SvfDemoMode;
}

function readConfig(root: HTMLElement): PanelConfig {
  const cdnAnalysisUrl = root.dataset['analysisWasmUrl'];
  const wasmUrl = cdnAnalysisUrl !== undefined && cdnAnalysisUrl.length > 0 ? cdnAnalysisUrl : undefined;
  const cutoff = Number(root.dataset['initialCutoffHz'] ?? '1000');
  const resonance = Number(root.dataset['initialResonance'] ?? '0.1');
  const modeRaw = root.dataset['initialMode'] ?? 'lowpass';
  return {
    wasmUrl,
    responseUrl: root.dataset['responseUrl'],
    impulseUrl: root.dataset['impulseUrl'],
    initialCutoffHz: Number.isFinite(cutoff) ? cutoff : 1000,
    initialResonance: Number.isFinite(resonance) ? resonance : 0.1,
    initialMode: isMode(modeRaw) ? modeRaw : 'lowpass',
  };
}

function readTheme(root: HTMLElement): PlotTheme {
  const cs = getComputedStyle(root);
  const read = (name: string, fallback: string): string => {
    const v = cs.getPropertyValue(name).trim();
    return v.length > 0 ? v : fallback;
  };
  return {
    surface: read('--vz-well', '#0c0f16'),
    grid: read('--vz-grid', '#232b3d'),
    axis: read('--vz-axis', '#3a4560'),
    trace: read('--signal', '#ff9f45'),
    traceHot: read('--signal-hot', '#ffc978'),
    ink: read('--ink', '#eaedf5'),
    dim: read('--dim', '#7e889f'),
    cool: read('--vz-cool', '#6fb6d6'),
  };
}

class SvfVisualizerPanel {
  private readonly config: PanelConfig;
  private readonly theme: PlotTheme;

  private readonly liveSection: HTMLElement;
  private readonly fallbackSection: HTMLElement;
  private readonly errorEl: HTMLElement;
  private readonly statusEl: HTMLElement;
  private readonly cutoffInput: HTMLInputElement;
  private readonly resonanceInput: HTMLInputElement;
  private readonly modeButtons: readonly HTMLButtonElement[];
  private readonly cutoffReadout: HTMLElement;
  private readonly resonanceReadout: HTMLElement;
  private readonly modeReadout: HTMLElement;
  private readonly radiusReadout: HTMLElement;

  private readonly responseWell: Well;
  private readonly poleZeroWell: Well;
  private readonly impulseWell: Well;

  private mode: SvfDemoMode;
  private engine: AnalysisEngine | undefined;
  private frame = 0;

  constructor(private readonly root: HTMLElement) {
    this.config = readConfig(root);
    this.theme = readTheme(root);
    this.mode = this.config.initialMode;

    this.liveSection = requireEl(root, '[data-role="live"]', HTMLElement);
    this.fallbackSection = requireEl(root, '[data-role="fallback"]', HTMLElement);
    this.errorEl = requireEl(root, '[data-role="error"]', HTMLElement);
    this.statusEl = requireEl(root, '[data-role="status"]', HTMLElement);
    this.cutoffInput = requireEl(root, '[data-role="cutoff"]', HTMLInputElement);
    this.resonanceInput = requireEl(root, '[data-role="resonance"]', HTMLInputElement);
    this.cutoffReadout = requireEl(root, '[data-role="cutoff-readout"]', HTMLElement);
    this.resonanceReadout = requireEl(root, '[data-role="resonance-readout"]', HTMLElement);
    this.modeReadout = requireEl(root, '[data-role="mode-readout"]', HTMLElement);
    this.radiusReadout = requireEl(root, '[data-role="radius-readout"]', HTMLElement);

    this.responseWell = new Well(requireEl(root, '[data-role="plot-response"]', HTMLCanvasElement));
    this.poleZeroWell = new Well(requireEl(root, '[data-role="plot-polezero"]', HTMLCanvasElement));
    this.impulseWell = new Well(requireEl(root, '[data-role="plot-impulse"]', HTMLCanvasElement));

    const buttons: HTMLButtonElement[] = [];
    root.querySelectorAll('[data-role="mode"]').forEach((el) => {
      if (el instanceof HTMLButtonElement) buttons.push(el);
    });
    this.modeButtons = buttons;
  }

  async init(): Promise<void> {
    this.cutoffInput.value = String(cutoffHzToNormalized(this.config.initialCutoffHz));
    this.resonanceInput.value = String(this.config.initialResonance);
    this.reflectControls();

    if (!analysisSupported()) {
      await this.showFallback('WebAssembly is unavailable — showing the pre-generated response and impulse.');
      return;
    }
    if (this.config.wasmUrl === undefined) {
      await this.showFallback(
        'No analysis-capable WASM was published in the manifest — showing the pre-generated response and impulse.',
      );
      return;
    }

    try {
      this.engine = await AnalysisEngine.create(this.config.wasmUrl);
    } catch (error) {
      await this.showFallback(
        `Could not load the analysis engine: ${error instanceof Error ? error.message : String(error)}`,
      );
      return;
    }

    this.liveSection.hidden = false;
    this.fallbackSection.hidden = true;

    this.cutoffInput.addEventListener('input', () => this.onControl());
    this.resonanceInput.addEventListener('input', () => this.onControl());
    for (const btn of this.modeButtons) {
      btn.addEventListener('click', () => {
        const value = btn.dataset['mode'] ?? '';
        if (isMode(value)) {
          this.mode = value;
          this.onControl();
        }
      });
    }
    const onResize = (): void => {
      this.responseWell.resize();
      this.poleZeroWell.resize();
      this.impulseWell.resize();
      this.render();
    };
    if (typeof ResizeObserver === 'function') {
      new ResizeObserver(onResize).observe(this.root);
    } else {
      window.addEventListener('resize', onResize);
    }

    this.setStatus('Live · real compiled analysis');
    this.render();
  }

  private onControl(): void {
    this.reflectControls();
    if (this.frame !== 0) return;
    this.frame = window.requestAnimationFrame(() => {
      this.frame = 0;
      this.render();
    });
  }

  private render(): void {
    const engine = this.engine;
    if (engine === undefined) return;
    const cutoffNorm = Number(this.cutoffInput.value);
    const resonance = Number(this.resonanceInput.value);
    const result = engine.analyze(cutoffNorm, resonance, modeToSvfMode(this.mode));

    this.radiusReadout.textContent = result.poleRadius.toFixed(4);
    this.root.style.setProperty('--vz-glow', resonance.toFixed(3));

    drawFrequencyResponse(
      this.responseWell.surface(),
      result.freqsHz,
      result.magsDb,
      normalizedToCutoffHz(cutoffNorm),
      resonance,
      this.theme,
    );
    drawPoleZero(this.poleZeroWell.surface(), result.poles, result.zeros, resonance, this.theme);
    drawImpulse(this.impulseWell.surface(), result.impulse, resonance, this.theme);
  }

  private async showFallback(message: string): Promise<void> {
    this.liveSection.hidden = true;
    this.fallbackSection.hidden = false;
    this.errorEl.hidden = false;
    this.errorEl.textContent = message;
    this.setStatus('Static snapshot · pre-generated core output');

    const fbResponse = requireEl(this.root, '[data-role="fallback-response"]', HTMLCanvasElement);
    const fbImpulse = requireEl(this.root, '[data-role="fallback-impulse"]', HTMLCanvasElement);
    const fbPoleZero = requireEl(this.root, '[data-role="fallback-polezero"]', HTMLCanvasElement);
    const responseWell = new Well(fbResponse);
    const impulseWell = new Well(fbImpulse);
    const poleZeroWell = new Well(fbPoleZero);
    drawEmptyPoleZero(poleZeroWell.surface(), this.theme);

    try {
      if (this.config.responseUrl !== undefined) {
        const asset = await fetchResponseAsset(this.config.responseUrl);
        drawFrequencyResponse(
          responseWell.surface(),
          Float32Array.from(asset.freqsHz),
          Float32Array.from(asset.magsDb),
          this.config.initialCutoffHz,
          this.config.initialResonance,
          this.theme,
        );
      }
      if (this.config.impulseUrl !== undefined) {
        const asset = await fetchImpulseAsset(this.config.impulseUrl);
        drawImpulse(impulseWell.surface(), Float32Array.from(asset.samples), this.config.initialResonance, this.theme);
      }
    } catch (error) {
      this.errorEl.textContent =
        `${message} (pre-generated assets also failed: ${error instanceof Error ? error.message : String(error)})`;
    }
  }

  private reflectControls(): void {
    const cutoffHz = normalizedToCutoffHz(Number(this.cutoffInput.value));
    const resonance = Number(this.resonanceInput.value);
    this.cutoffReadout.textContent = formatCutoff(cutoffHz);
    this.resonanceReadout.textContent = formatResonance(resonance);
    this.modeReadout.textContent = modeLabel(this.mode);
    this.root.style.setProperty('--vz-glow', resonance.toFixed(3));
    for (const btn of this.modeButtons) {
      btn.setAttribute('aria-pressed', btn.dataset['mode'] === this.mode ? 'true' : 'false');
    }
  }

  private setStatus(text: string): void {
    this.statusEl.textContent = text;
  }
}

/** Boot every visualizer island on the page (idempotent per element). */
export function initSvfVisualizers(): void {
  document.querySelectorAll('[data-svf-visualizer]').forEach((root) => {
    if (!(root instanceof HTMLElement)) return;
    if (root.dataset['svfVisualizerReady'] === 'true') return;
    root.dataset['svfVisualizerReady'] = 'true';
    void new SvfVisualizerPanel(root).init();
  });
}
