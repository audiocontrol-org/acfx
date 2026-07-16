// Client island controller for the SVF "play with it" demo.
//
// This is the crux of the cross-package integration: it drives the REAL
// compiled SVF (svf.wasm) running on the audio thread via the `adapters/web`
// AudioWorklet engine (T026). A rich harmonic SOURCE (a sawtooth oscillator)
// is fed through the worklet node so that sweeping cutoff / resonance / mode is
// audible live. There is NO BiquadFilter and NO JS filter reimplementation —
// the only DSP is svf_process inside the worklet (FR-002, Principle VII).
//
// Worklet-URL bundling (the other half of the crux): the processor is a
// TypeScript module with its own imports, so it must be BUNDLED (not merely
// copied) to a browser-loadable URL. We import it with Vite's `?worker&url`
// suffix, which compiles the entry + its `svf-engine-core` import into a single
// self-executing script and yields its emitted URL — exactly what
// `audioWorklet.addModule` needs. The `@acfx/web` alias (astro.config.mjs +
// tsconfig paths) resolves the specifier into the sibling `adapters/web` pkg.

import workletUrl from '@acfx/web/worklet/svf-processor.ts?worker&url';
import { createSvfAudioNode, type SvfAudioNode } from '@acfx/web/worklet/svf-audio-node.ts';

import {
  cutoffHzToNormalized,
  formatCutoff,
  formatResonance,
  modeLabel,
  modeToSvfMode,
  normalizedToCutoffHz,
  type SvfDemoMode,
} from './svf-param-map.ts';

import { ARTIFACT_VISIBILITY_EVENT, readVisibilityDetail } from '@lib/lesson/artifact-visibility';

const SOURCE_FREQ_HZ = 55; // sawtooth fundamental (A1) — dense harmonics to sweep
const MASTER_GAIN = 0.22; // conservative output level; resonance can add energy
const FADE_SECONDS = 0.04; // click-free start/stop ramps

function isMode(value: string): value is SvfDemoMode {
  return value === 'lowpass' || value === 'highpass' || value === 'bandpass';
}

/** True when this browser can run the real WASM audio path (else: content fallback). */
function liveAudioSupported(): boolean {
  return (
    typeof window.AudioContext === 'function' &&
    typeof WebAssembly === 'object' &&
    'audioWorklet' in window.AudioContext.prototype
  );
}

function requireEl<T extends Element>(
  root: ParentNode,
  selector: string,
  ctor: new () => T,
): T {
  const el = root.querySelector(selector);
  if (!(el instanceof ctor)) {
    throw new Error(`svf-demo: missing or wrong element for "${selector}"`);
  }
  return el;
}

interface PanelConfig {
  readonly wasmUrl: string;
  readonly initialCutoffHz: number;
  readonly initialResonance: number;
  readonly initialMode: SvfDemoMode;
}

function readConfig(root: HTMLElement): PanelConfig {
  const wasmUrl = root.dataset['wasmUrl'];
  if (wasmUrl === undefined || wasmUrl.length === 0) {
    throw new Error('svf-demo: data-wasm-url is required');
  }
  const initialCutoffHz = Number(root.dataset['initialCutoffHz'] ?? '1000');
  const initialResonance = Number(root.dataset['initialResonance'] ?? '0.1');
  const modeRaw = root.dataset['initialMode'] ?? 'lowpass';
  return {
    wasmUrl,
    initialCutoffHz: Number.isFinite(initialCutoffHz) ? initialCutoffHz : 1000,
    initialResonance: Number.isFinite(initialResonance) ? initialResonance : 0.1,
    initialMode: isMode(modeRaw) ? modeRaw : 'lowpass',
  };
}

/** Live running audio graph: context + source oscillator + real SVF worklet node. */
interface AudioGraph {
  readonly context: AudioContext;
  readonly oscillator: OscillatorNode;
  readonly master: GainNode;
  readonly svf: SvfAudioNode;
}

class SvfDemoPanel {
  private readonly config: PanelConfig;

  private readonly liveSection: HTMLElement;
  private readonly fallbackSection: HTMLElement;
  private readonly startBtn: HTMLButtonElement;
  private readonly stopBtn: HTMLButtonElement;
  private readonly cutoffInput: HTMLInputElement;
  private readonly resonanceInput: HTMLInputElement;
  private readonly modeButtons: readonly HTMLButtonElement[];
  private readonly cutoffReadout: HTMLElement;
  private readonly resonanceReadout: HTMLElement;
  private readonly modeReadout: HTMLElement;
  private readonly statusEl: HTMLElement;
  private readonly errorEl: HTMLElement;

  private mode: SvfDemoMode;
  private graph: AudioGraph | undefined;
  private starting = false;
  // True when this panel's tab was hidden mid-playback and we suspended the
  // AudioContext ourselves — so re-showing resumes it (but a user Stop doesn't).
  private autoSuspended = false;

  constructor(private readonly root: HTMLElement) {
    this.config = readConfig(root);
    this.mode = this.config.initialMode;

    this.liveSection = requireEl(root, '[data-role="live"]', HTMLElement);
    this.fallbackSection = requireEl(root, '[data-role="fallback"]', HTMLElement);
    this.startBtn = requireEl(root, '[data-role="start"]', HTMLButtonElement);
    this.stopBtn = requireEl(root, '[data-role="stop"]', HTMLButtonElement);
    this.cutoffInput = requireEl(root, '[data-role="cutoff"]', HTMLInputElement);
    this.resonanceInput = requireEl(root, '[data-role="resonance"]', HTMLInputElement);
    this.cutoffReadout = requireEl(root, '[data-role="cutoff-readout"]', HTMLElement);
    this.resonanceReadout = requireEl(root, '[data-role="resonance-readout"]', HTMLElement);
    this.modeReadout = requireEl(root, '[data-role="mode-readout"]', HTMLElement);
    this.statusEl = requireEl(root, '[data-role="status"]', HTMLElement);
    this.errorEl = requireEl(root, '[data-role="error"]', HTMLElement);

    const modeButtons: HTMLButtonElement[] = [];
    root.querySelectorAll('[data-role="mode"]').forEach((el) => {
      if (el instanceof HTMLButtonElement) modeButtons.push(el);
    });
    this.modeButtons = modeButtons;
  }

  /** Progressive enhancement: reveal the live panel only when the real path can run. */
  init(): void {
    // Pause/resume across tab switches: the island is never unmounted, so its
    // AudioContext + worklet persist — but it must not play from an unseen tab.
    this.root.addEventListener(ARTIFACT_VISIBILITY_EVENT, (event) => {
      const detail = readVisibilityDetail(event);
      if (detail !== undefined) this.setVisible(detail.visible);
    });

    if (!liveAudioSupported()) {
      // Leave the content fallback (pre-rendered clips) visible; explain why.
      this.setStatus('Live DSP needs AudioWorklet + WebAssembly — showing pre-rendered examples.');
      return;
    }

    // Go live: show the interactive faceplate, hide the content fallback.
    this.liveSection.hidden = false;
    this.fallbackSection.hidden = true;

    // Seed control positions from the declared initial params.
    this.cutoffInput.value = String(cutoffHzToNormalized(this.config.initialCutoffHz));
    this.resonanceInput.value = String(this.config.initialResonance);
    this.reflectCutoff();
    this.reflectResonance();
    this.reflectMode();

    this.startBtn.addEventListener('click', () => void this.start());
    this.stopBtn.addEventListener('click', () => this.stop());
    this.cutoffInput.addEventListener('input', () => this.onCutoff());
    this.resonanceInput.addEventListener('input', () => this.onResonance());
    for (const btn of this.modeButtons) {
      btn.addEventListener('click', () => {
        const value = btn.dataset['mode'] ?? '';
        if (isMode(value)) this.onMode(value);
      });
    }

    this.setStatus('Ready. Press Start to hear the filter.');
  }

  private async start(): Promise<void> {
    if (this.graph !== undefined || this.starting) return;
    this.starting = true;
    this.clearError();
    this.startBtn.disabled = true;
    this.setStatus('Starting audio engine…');

    let context: AudioContext | undefined;
    try {
      context = new AudioContext();
      // Resume within the click handler's user-gesture window (browser policy).
      await context.resume();

      const svf = await createSvfAudioNode(context, {
        workletUrl,
        wasmUrl: this.config.wasmUrl,
      });

      const oscillator = new OscillatorNode(context, { type: 'sawtooth', frequency: SOURCE_FREQ_HZ });
      const master = new GainNode(context, { gain: 0 });

      // source -> REAL SVF worklet -> master gain -> speakers
      oscillator.connect(svf.node);
      svf.node.connect(master);
      master.connect(context.destination);

      // Push current control values into the running filter before sound starts.
      svf.setCutoff(cutoffHzToNormalized(this.currentCutoffHz()));
      svf.setResonance(this.currentResonance());
      svf.setMode(modeToSvfMode(this.mode));

      oscillator.start();
      master.gain.setValueAtTime(0, context.currentTime);
      master.gain.linearRampToValueAtTime(MASTER_GAIN, context.currentTime + FADE_SECONDS);

      this.graph = { context, oscillator, master, svf };
      this.root.dataset['playing'] = 'true';
      this.startBtn.hidden = true;
      this.stopBtn.hidden = false;
      this.setStatus('Playing · sawtooth source');
    } catch (error) {
      // WASM fetch/instantiate FAILURE — show a visible, descriptive error and
      // fall back to the pre-rendered content clips. Never a substitute DSP.
      if (context !== undefined) void context.close();
      this.showError(error);
      this.startBtn.disabled = false;
    } finally {
      this.starting = false;
    }
  }

  private stop(): void {
    const graph = this.graph;
    if (graph === undefined) return;
    const { context, oscillator, master } = graph;
    const now = context.currentTime;
    master.gain.cancelScheduledValues(now);
    master.gain.setValueAtTime(master.gain.value, now);
    master.gain.linearRampToValueAtTime(0, now + FADE_SECONDS);
    oscillator.stop(now + FADE_SECONDS + 0.01);
    window.setTimeout(() => void context.close(), (FADE_SECONDS + 0.05) * 1000);

    this.graph = undefined;
    this.autoSuspended = false;
    delete this.root.dataset['playing'];
    this.stopBtn.hidden = true;
    this.startBtn.hidden = false;
    this.startBtn.disabled = false;
    this.setStatus('Stopped. Press Start to hear the filter again.');
  }

  /** Tab hidden -> suspend live audio; tab shown again -> resume if we suspended. */
  private setVisible(visible: boolean): void {
    const graph = this.graph;
    if (graph === undefined) return;
    if (!visible) {
      if (graph.context.state === 'running') {
        this.autoSuspended = true;
        void graph.context.suspend();
      }
    } else if (this.autoSuspended) {
      this.autoSuspended = false;
      void graph.context.resume();
    }
  }

  private onCutoff(): void {
    this.reflectCutoff();
    this.graph?.svf.setCutoff(this.normalizedCutoff());
  }

  private onResonance(): void {
    this.reflectResonance();
    this.graph?.svf.setResonance(this.currentResonance());
  }

  private onMode(mode: SvfDemoMode): void {
    this.mode = mode;
    this.reflectMode();
    this.graph?.svf.setMode(modeToSvfMode(mode));
  }

  private normalizedCutoff(): number {
    return Number(this.cutoffInput.value);
  }

  private currentCutoffHz(): number {
    return normalizedToCutoffHz(this.normalizedCutoff());
  }

  private currentResonance(): number {
    return Number(this.resonanceInput.value);
  }

  private reflectCutoff(): void {
    this.cutoffReadout.textContent = formatCutoff(this.currentCutoffHz());
  }

  private reflectResonance(): void {
    const resonance = this.currentResonance();
    this.resonanceReadout.textContent = formatResonance(resonance);
    // Signature: the faceplate "heats up" as resonance nears self-oscillation.
    this.root.style.setProperty('--svf-glow', resonance.toFixed(3));
  }

  private reflectMode(): void {
    this.modeReadout.textContent = modeLabel(this.mode);
    for (const btn of this.modeButtons) {
      const active = btn.dataset['mode'] === this.mode;
      btn.setAttribute('aria-pressed', active ? 'true' : 'false');
    }
  }

  private setStatus(text: string): void {
    this.statusEl.textContent = text;
  }

  private clearError(): void {
    this.errorEl.hidden = true;
    this.errorEl.textContent = '';
  }

  private showError(error: unknown): void {
    const message = error instanceof Error ? error.message : String(error);
    this.errorEl.hidden = false;
    this.errorEl.textContent = `Could not start live DSP: ${message}`;
    this.fallbackSection.hidden = false;
    this.setStatus('Live DSP failed to start — showing pre-rendered examples below.');
  }
}

/** Boot every SVF demo island present on the page (idempotent per element). */
export function initSvfDemos(): void {
  const roots = document.querySelectorAll('[data-svf-demo]');
  roots.forEach((root) => {
    if (!(root instanceof HTMLElement)) return;
    if (root.dataset['svfDemoReady'] === 'true') return;
    root.dataset['svfDemoReady'] = 'true';
    new SvfDemoPanel(root).init();
  });
}
