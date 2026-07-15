// Pure Canvas drawing for the SVF analyzer bay. DOM-agnostic beyond the 2D
// context it is handed: every function takes a ready-scaled context (CSS-pixel
// coordinate space) plus the REAL analysis data and paints one instrument well.
//
// These functions never compute the filter — they only PRESENT data produced by
// the compiled analysis ABI (magnitude, poles/zeros, impulse). Log/dB axis maps
// and marker geometry are presentation, not DSP re-derivation (FR-003).

/** Palette handed down from the faceplate CSS custom properties. */
export interface PlotTheme {
  readonly surface: string;
  readonly grid: string;
  readonly axis: string;
  readonly trace: string;
  readonly traceHot: string;
  readonly ink: string;
  readonly dim: string;
  readonly cool: string;
}

/** A ready-to-draw well: a device-pixel-scaled context in CSS-pixel space. */
export interface PlotSurface {
  readonly ctx: CanvasRenderingContext2D;
  readonly width: number;
  readonly height: number;
}

const FREQ_MIN_HZ = 20;
const FREQ_MAX_HZ = 20_000;
const DB_TOP = 12;
const DB_BOTTOM = -60;
const MONO = '11px ui-monospace, "SF Mono", "JetBrains Mono", monospace';

function clearWell(s: PlotSurface, theme: PlotTheme): void {
  const { ctx, width, height } = s;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = theme.surface;
  ctx.fillRect(0, 0, width, height);
}

// --- Frequency response ---------------------------------------------------

function freqX(hz: number, width: number): number {
  const t = Math.log(hz / FREQ_MIN_HZ) / Math.log(FREQ_MAX_HZ / FREQ_MIN_HZ);
  return t * width;
}

function dbY(db: number, height: number): number {
  const t = (DB_TOP - db) / (DB_TOP - DB_BOTTOM);
  return t * height;
}

/** Magnitude response |H(f)| in dB vs log frequency, with a cutoff marker. */
export function drawFrequencyResponse(
  s: PlotSurface,
  freqsHz: Float32Array,
  magsDb: Float32Array,
  cutoffHz: number,
  glow: number,
  theme: PlotTheme,
): void {
  const { ctx, width, height } = s;
  clearWell(s, theme);

  // Decade minor + major graticule.
  const majors = [100, 1000, 10_000];
  const minors = [30, 40, 50, 60, 70, 80, 90, 200, 300, 400, 500, 600, 700, 800, 900,
    2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000];
  ctx.lineWidth = 1;
  ctx.strokeStyle = theme.grid;
  for (const f of minors) {
    const x = freqX(f, width);
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
  // Horizontal dB lines.
  for (let db = 0; db >= DB_BOTTOM; db -= 12) {
    const y = dbY(db, height);
    ctx.strokeStyle = db === 0 ? theme.axis : theme.grid;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
    ctx.fillStyle = theme.dim;
    ctx.font = MONO;
    ctx.textAlign = 'left';
    ctx.textBaseline = 'bottom';
    ctx.fillText(`${db > 0 ? '+' : ''}${db} dB`, 4, y - 2);
  }
  // Major frequency labels.
  ctx.fillStyle = theme.dim;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'bottom';
  for (const f of majors) {
    const x = freqX(f, width);
    ctx.strokeStyle = theme.axis;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
    ctx.fillText(f >= 1000 ? `${f / 1000}k` : `${f}`, x, height - 3);
  }

  // Cutoff marker.
  const cx = freqX(Math.min(Math.max(cutoffHz, FREQ_MIN_HZ), FREQ_MAX_HZ), width);
  ctx.save();
  ctx.setLineDash([3, 3]);
  ctx.strokeStyle = theme.traceHot;
  ctx.globalAlpha = 0.55;
  ctx.beginPath();
  ctx.moveTo(cx, 0);
  ctx.lineTo(cx, height);
  ctx.stroke();
  ctx.restore();

  // The trace.
  const n = Math.min(freqsHz.length, magsDb.length);
  ctx.beginPath();
  let started = false;
  for (let i = 0; i < n; i++) {
    const f = freqsHz[i];
    const db = magsDb[i];
    if (f === undefined || db === undefined) continue;
    const x = freqX(f, width);
    const y = dbY(Math.max(db, DB_BOTTOM), height);
    if (!started) {
      ctx.moveTo(x, y);
      started = true;
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.lineWidth = 2;
  ctx.strokeStyle = theme.trace;
  ctx.shadowColor = theme.traceHot;
  ctx.shadowBlur = 4 + glow * 16;
  ctx.stroke();
  ctx.shadowBlur = 0;
}

// --- Pole / zero (z-plane) ------------------------------------------------

const ZPLANE_RANGE = 1.35;

/** Poles (×) and zeros (○) on the unit-circle z-plane. */
export function drawPoleZero(
  s: PlotSurface,
  poles: readonly { re: number; im: number }[],
  zeros: readonly { re: number; im: number }[],
  glow: number,
  theme: PlotTheme,
): void {
  const { ctx, width, height } = s;
  clearWell(s, theme);

  const scale = Math.min(width, height) / (2 * ZPLANE_RANGE);
  const ox = width / 2;
  const oy = height / 2;
  const toX = (re: number): number => ox + re * scale;
  const toY = (im: number): number => oy - im * scale;

  // Real / imaginary axes.
  ctx.lineWidth = 1;
  ctx.strokeStyle = theme.grid;
  ctx.beginPath();
  ctx.moveTo(0, oy);
  ctx.lineTo(width, oy);
  ctx.moveTo(ox, 0);
  ctx.lineTo(ox, height);
  ctx.stroke();

  // Unit circle — the stability boundary.
  ctx.strokeStyle = theme.axis;
  ctx.beginPath();
  ctx.arc(ox, oy, scale, 0, Math.PI * 2);
  ctx.stroke();
  ctx.fillStyle = theme.dim;
  ctx.font = MONO;
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  ctx.fillText('|z| = 1', toX(0) + 4, toY(1) + 2);

  // Zeros: hollow rings, cool ink.
  ctx.lineWidth = 2;
  ctx.strokeStyle = theme.cool;
  for (const z of zeros) {
    ctx.beginPath();
    ctx.arc(toX(z.re), toY(z.im), 6, 0, Math.PI * 2);
    ctx.stroke();
  }

  // Poles: amber crosses that heat up as they near the unit circle (resonance).
  ctx.strokeStyle = theme.traceHot;
  ctx.shadowColor = theme.traceHot;
  ctx.shadowBlur = 6 + glow * 22;
  for (const p of poles) {
    const x = toX(p.re);
    const y = toY(p.im);
    const r = 6;
    ctx.beginPath();
    ctx.moveTo(x - r, y - r);
    ctx.lineTo(x + r, y + r);
    ctx.moveTo(x - r, y + r);
    ctx.lineTo(x + r, y - r);
    ctx.stroke();
  }
  ctx.shadowBlur = 0;

  // Pole radius annotation (the physical meaning of resonance).
  const lead = poles[0];
  if (lead !== undefined) {
    const radius = Math.hypot(lead.re, lead.im);
    ctx.fillStyle = theme.ink;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'bottom';
    ctx.fillText(`|p| = ${radius.toFixed(4)}`, width - 5, height - 5);
  }
}

// --- Impulse response -----------------------------------------------------

/** The real filter's unit-impulse response, samples left to right. */
export function drawImpulse(
  s: PlotSurface,
  samples: Float32Array,
  glow: number,
  theme: PlotTheme,
): void {
  const { ctx, width, height } = s;
  clearWell(s, theme);

  const n = samples.length;
  let peak = 1e-6;
  for (let i = 0; i < n; i++) {
    const v = samples[i];
    if (v !== undefined) peak = Math.max(peak, Math.abs(v));
  }
  const mid = height / 2;
  const amp = mid * 0.9;

  // Zero baseline.
  ctx.lineWidth = 1;
  ctx.strokeStyle = theme.axis;
  ctx.beginPath();
  ctx.moveTo(0, mid);
  ctx.lineTo(width, mid);
  ctx.stroke();

  const sampleX = (i: number): number => (n <= 1 ? 0 : (i / (n - 1)) * width);
  const sampleY = (v: number): number => mid - (v / peak) * amp;

  // Faint stems for texture.
  ctx.strokeStyle = theme.grid;
  for (let i = 0; i < n; i += Math.max(1, Math.floor(n / 96))) {
    const v = samples[i];
    if (v === undefined) continue;
    const x = sampleX(i);
    ctx.beginPath();
    ctx.moveTo(x, mid);
    ctx.lineTo(x, sampleY(v));
    ctx.stroke();
  }

  // The envelope trace.
  ctx.beginPath();
  let started = false;
  for (let i = 0; i < n; i++) {
    const v = samples[i];
    if (v === undefined) continue;
    const x = sampleX(i);
    const y = sampleY(v);
    if (!started) {
      ctx.moveTo(x, y);
      started = true;
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.lineWidth = 2;
  ctx.strokeStyle = theme.trace;
  ctx.shadowColor = theme.traceHot;
  ctx.shadowBlur = 4 + glow * 14;
  ctx.stroke();
  ctx.shadowBlur = 0;

  ctx.fillStyle = theme.dim;
  ctx.font = MONO;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'top';
  ctx.fillText(`${n} samples`, width - 5, 4);
}

/** Draw an empty z-plane (unit circle only) for the content-fallback state. */
export function drawEmptyPoleZero(s: PlotSurface, theme: PlotTheme): void {
  drawPoleZero(s, [], [], 0, theme);
}
