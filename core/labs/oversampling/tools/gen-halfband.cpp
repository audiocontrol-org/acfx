// ============================================================================
// gen-halfband.cpp — OFFLINE halfband-FIR coefficient generator (HOST-ONLY).
//
// THIS FILE IS NOT ON ANY BUILD OR AUDIO PATH. It is a reproducibility aid
// (research.md Decision 5): it computes the linear-phase halfband taps and
// emits (a) a ready-to-paste `static constexpr float` C++ initializer + tap
// count N, and (b) a verification summary (measured passband ripple dB and
// stopband attenuation dB). The committed header authored from its output
// (core/primitives/oversampling/halfband-coefficients.h, task T004) is the
// source of truth the core compiles; this generator is never compiled by the
// core build and pulls in nothing but the C++17 standard library.
//
// Build & run manually (host compiler):
//   c++ -std=c++17 -O2 core/labs/oversampling/tools/gen-halfband.cpp \
//       -o /tmp/gen-halfband && /tmp/gen-halfband
//
// ----------------------------------------------------------------------------
// DESIGN PARAMETERS (research.md Decisions 5 & 6; FR-004, FR-009, FR-024)
//   Method            : Kaiser-windowed ideal-sinc half-band FIR low-pass.
//   Filter type       : Type-I linear phase (symmetric, odd length N).
//   Cutoff            : quarter-sample-rate, normalized f_c = 0.25 (= pi/2 rad).
//   Half-band property: N odd with (N-1)/2 odd  =>  N = 4*k + 3. Every tap at
//                       an EVEN offset from the center is exactly zero except
//                       the center tap = 0.5. (Exploited by the polyphase
//                       decomposition in T005.) The ideal half-band sinc has
//                       these values analytically; the Kaiser window preserves
//                       the zeros (it multiplies them) and the center. We set
//                       the even-offset taps to exactly 0 and the center to
//                       exactly 0.5 so the emitted table carries the half-band
//                       structure bit-exactly (no 1e-17 float dust).
//   Windowing         : Kaiser, beta below.
//   Targets           : stopband attenuation >= 80 dB, passband ripple <= 0.1 dB.
//   Transition band   : full width TRANSITION_WIDTH, centered on 0.25, i.e.
//                       passband edge f_p = 0.25 - TRANSITION_WIDTH/2,
//                       stopband edge f_s = 0.25 + TRANSITION_WIDTH/2.
//
//   The three tunables below (STOPBAND_TARGET_DB used to derive beta,
//   TRANSITION_WIDTH used to derive N, and the resulting N/beta) were chosen so
//   the MEASURED figures clear the targets with margin; the program verifies
//   this in-process and refuses to declare success otherwise.
// ============================================================================

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

// ---- Design tunables --------------------------------------------------------
// beta is derived from this attenuation target via Kaiser's empirical formula.
// We aim above 80 dB so the *measured* stopband clears 80 dB comfortably.
constexpr double kStopbandTargetDb = 85.0;
// Full transition-band width (normalized to sample rate), centered on 0.25.
constexpr double kTransitionWidth = 0.06;
// Cutoff (normalized to sample rate): quarter-sample-rate half-band cutoff.
constexpr double kCutoff = 0.25;

// Passband / stopband edges (symmetric about the cutoff).
constexpr double kPassbandEdge = kCutoff - kTransitionWidth * 0.5;
constexpr double kStopbandEdge = kCutoff + kTransitionWidth * 0.5;

// Acceptance thresholds (research.md Decision 6).
constexpr double kRequiredStopbandDb = 80.0;
constexpr double kRequiredPassbandRippleDb = 0.1;

// Zeroth-order modified Bessel function of the first kind, I0(x), by series.
double besselI0(double x) {
  double sum = 1.0;
  double term = 1.0;
  const double halfX = x * 0.5;
  for (int k = 1; k < 64; ++k) {
    // term_k = (x/2)^(2k) / (k!)^2 = term_{k-1} * (x/2)^2 / k^2
    term *= (halfX * halfX) / (static_cast<double>(k) * static_cast<double>(k));
    sum += term;
    if (term < 1e-18 * sum) break;
  }
  return sum;
}

// Normalized sinc: sin(pi*x)/(pi*x), with the removable singularity at x==0.
double sinc(double x) {
  if (x == 0.0) return 1.0;
  const double px = M_PI * x;
  return std::sin(px) / px;
}

// Kaiser beta from a stopband attenuation target A (dB), Kaiser's formula.
double kaiserBeta(double aDb) {
  if (aDb > 50.0) return 0.1102 * (aDb - 8.7);
  if (aDb >= 21.0) return 0.5842 * std::pow(aDb - 21.0, 0.4) + 0.07886 * (aDb - 21.0);
  return 0.0;
}

// Estimate the FIR order needed for attenuation A over transition width df
// (normalized to the sample rate) via Kaiser's formula, then round the length
// up to the nearest N = 4*k + 3 so it is a valid half-band length.
int halfbandLength(double aDb, double df) {
  const double dw = 2.0 * M_PI * df;          // transition width in rad/sample
  const double order = (aDb - 8.0) / (2.285 * dw);
  int n = static_cast<int>(std::ceil(order)) + 1;   // taps = order + 1
  // Round up to N = 4k + 3  =>  (N-1)/2 is odd.
  while (n % 4 != 3) ++n;
  return n;
}

// Build the Kaiser-windowed half-band low-pass taps (length n, n = 4k+3).
std::vector<double> designHalfband(int n, double beta) {
  const int m = (n - 1) / 2;                  // center index (must be odd)
  std::vector<double> h(static_cast<size_t>(n), 0.0);
  const double i0Beta = besselI0(beta);
  for (int i = 0; i <= m; ++i) {
    const int offset = i - m;                 // <= 0
    const double idealVal = 2.0 * kCutoff * sinc(2.0 * kCutoff * offset);
    // Kaiser window sample.
    const double r = static_cast<double>(offset) / static_cast<double>(m);
    const double win = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
    double tap = idealVal * win;
    // Enforce the exact half-band structure: center = 0.5, even offsets = 0.
    if (offset == 0) {
      tap = 0.5;
    } else if (offset % 2 == 0) {
      tap = 0.0;
    }
    h[static_cast<size_t>(i)] = tap;
    h[static_cast<size_t>(n - 1 - i)] = tap;  // symmetric mirror (linear phase)
  }
  return h;
}

// Magnitude response |H(f)| for normalized frequency f in [0, 0.5].
double magnitudeAt(const std::vector<double>& h, double f) {
  const double w = 2.0 * M_PI * f;
  std::complex<double> acc{0.0, 0.0};
  for (size_t n = 0; n < h.size(); ++n) {
    acc += h[n] * std::exp(std::complex<double>(0.0, -w * static_cast<double>(n)));
  }
  return std::abs(acc);
}

struct Measurement {
  double stopbandDb;         // min attenuation across the stopband
  double passbandRippleDb;   // peak-to-peak ripple across the passband
  double dcGain;             // |H(0)|
};

Measurement measure(const std::vector<double>& h) {
  constexpr int kGrid = 20001;                // dense sweep over [0, 0.5]
  double worstStopMag = 0.0;                  // max |H| in stopband
  double passMax = -1.0;
  double passMin = 1e300;
  for (int i = 0; i < kGrid; ++i) {
    const double f = 0.5 * static_cast<double>(i) / static_cast<double>(kGrid - 1);
    const double mag = magnitudeAt(h, f);
    if (f <= kPassbandEdge) {
      passMax = std::max(passMax, mag);
      passMin = std::min(passMin, mag);
    }
    if (f >= kStopbandEdge) {
      worstStopMag = std::max(worstStopMag, mag);
    }
  }
  Measurement out{};
  out.stopbandDb = -20.0 * std::log10(worstStopMag);
  out.passbandRippleDb = 20.0 * std::log10(passMax / passMin);
  out.dcGain = magnitudeAt(h, 0.0);
  return out;
}

// Verify the exact half-band zero pattern / center / symmetry.
bool checkHalfbandStructure(const std::vector<double>& h, std::string& report) {
  const int n = static_cast<int>(h.size());
  const int m = (n - 1) / 2;
  bool ok = true;
  char buf[256];
  if (m % 2 != 1) { ok = false; report += "  FAIL: (N-1)/2 is not odd\n"; }
  if (h[static_cast<size_t>(m)] != 0.5) {
    ok = false;
    std::snprintf(buf, sizeof(buf), "  FAIL: center tap = %.17g (expected 0.5)\n",
                  h[static_cast<size_t>(m)]);
    report += buf;
  }
  int evenZeros = 0, oddNonzero = 0;
  for (int off = 1; off <= m; ++off) {
    const double v = h[static_cast<size_t>(m + off)];
    if (off % 2 == 0) {
      if (v != 0.0) { ok = false; report += "  FAIL: even-offset tap nonzero\n"; }
      else ++evenZeros;
    } else if (v != 0.0) {
      ++oddNonzero;
    }
    // Symmetry check.
    if (h[static_cast<size_t>(m + off)] != h[static_cast<size_t>(m - off)]) {
      ok = false;
      report += "  FAIL: filter not symmetric about center\n";
    }
  }
  std::snprintf(buf, sizeof(buf),
                "  center tap = 0.5 (exact), symmetric, even-offset zeros = %d, "
                "nonzero odd-offset taps (per side) = %d\n",
                evenZeros, oddNonzero);
  report += buf;
  return ok;
}

std::string formatCoeffInitializer(const std::vector<double>& h) {
  std::string out;
  char buf[64];
  out += "static constexpr int kHalfbandTaps = " + std::to_string(h.size()) + ";\n";
  out += "static constexpr float kHalfbandCoeffs[kHalfbandTaps] = {\n";
  const int perLine = 4;
  for (size_t i = 0; i < h.size(); ++i) {
    if (i % perLine == 0) out += "    ";
    // Emit the float literal with an 'f' suffix directly attached; enough
    // significant digits to round-trip a 32-bit float. Then pad to a column so
    // the table stays aligned. Space-sign keeps positive/negative aligned.
    std::snprintf(buf, sizeof(buf), "% .9ef", static_cast<float>(h[i]));
    std::string tok = buf;           // e.g. " 1.128547137e-05f"
    if (i + 1 < h.size()) tok += ",";
    while (tok.size() < 20) tok += ' ';
    out += tok;
    if ((i + 1) % perLine == 0 || i + 1 == h.size()) out += "\n";
  }
  out += "};\n";
  return out;
}

}  // namespace

int main() {
  const double beta = kaiserBeta(kStopbandTargetDb);
  const int n = halfbandLength(kStopbandTargetDb, kTransitionWidth);
  const std::vector<double> h = designHalfband(n, beta);

  const Measurement meas = measure(h);
  std::string structureReport;
  const bool structureOk = checkHalfbandStructure(h, structureReport);

  const bool stopOk = meas.stopbandDb >= kRequiredStopbandDb;
  const bool rippleOk = meas.passbandRippleDb <= kRequiredPassbandRippleDb;
  const bool pass = structureOk && stopOk && rippleOk;

  // Human-readable summary block (also written to the .generated.txt artifact).
  std::string summary;
  char line[256];
  summary += "Half-band FIR low-pass — generated by gen-halfband.cpp\n";
  summary += "======================================================\n\n";
  summary += "Design parameters\n";
  summary += "-----------------\n";
  std::snprintf(line, sizeof(line), "  method            : Kaiser-windowed ideal-sinc half-band FIR\n");           summary += line;
  std::snprintf(line, sizeof(line), "  filter type       : Type-I linear phase (symmetric, odd length)\n");        summary += line;
  std::snprintf(line, sizeof(line), "  cutoff (norm.)    : %.4f (quarter-sample-rate)\n", kCutoff);                  summary += line;
  std::snprintf(line, sizeof(line), "  tap count N       : %d  ((N-1)/2 = %d, odd => valid half-band)\n", n, (n-1)/2); summary += line;
  std::snprintf(line, sizeof(line), "  Kaiser beta       : %.6f (from %.1f dB stopband target)\n", beta, kStopbandTargetDb); summary += line;
  std::snprintf(line, sizeof(line), "  transition width  : %.4f (normalized, centered on 0.25)\n", kTransitionWidth); summary += line;
  std::snprintf(line, sizeof(line), "  passband edge f_p : %.5f\n", kPassbandEdge);                                  summary += line;
  std::snprintf(line, sizeof(line), "  stopband edge f_s : %.5f\n", kStopbandEdge);                                  summary += line;
  std::snprintf(line, sizeof(line), "  targets           : stopband >= %.1f dB, passband ripple <= %.2f dB\n",
                kRequiredStopbandDb, kRequiredPassbandRippleDb);                                                     summary += line;
  summary += "\nMeasured response (dense sweep, 20001 points over [0, 0.5])\n";
  summary += "----------------------------------------------------------\n";
  std::snprintf(line, sizeof(line), "  stopband attenuation : %.3f dB  (target >= %.1f)  [%s]\n",
                meas.stopbandDb, kRequiredStopbandDb, stopOk ? "PASS" : "FAIL");                                     summary += line;
  std::snprintf(line, sizeof(line), "  passband ripple      : %.5f dB  (target <= %.2f)  [%s]\n",
                meas.passbandRippleDb, kRequiredPassbandRippleDb, rippleOk ? "PASS" : "FAIL");                       summary += line;
  std::snprintf(line, sizeof(line), "  DC gain |H(0)|       : %.9f\n", meas.dcGain);                                 summary += line;
  summary += "\nHalf-band structure check\n";
  summary += "-------------------------\n";
  summary += structureReport;
  std::snprintf(line, sizeof(line), "  structure            : [%s]\n", structureOk ? "PASS" : "FAIL");               summary += line;
  summary += "\nOverall: ";
  summary += pass ? "PASS — all targets met.\n" : "FAIL — targets NOT met.\n";

  const std::string coeffBlock = formatCoeffInitializer(h);

  // Print to stdout.
  std::fputs(summary.c_str(), stdout);
  std::fputs("\nReady-to-paste coefficient table\n", stdout);
  std::fputs("--------------------------------\n", stdout);
  std::fputs(coeffBlock.c_str(), stdout);

  // Write the committed artifact (provenance for T004).
  const char* artifactPath = "core/labs/oversampling/tools/halfband-coeffs.generated.txt";
  std::ofstream art(artifactPath);
  if (art) {
    art << summary << "\n";
    art << "namespace acfx {\n";
    art << coeffBlock;
    art << "}  // namespace acfx\n";
    art.close();
    std::printf("\nWrote artifact: %s\n", artifactPath);
  } else {
    std::printf("\nWARNING: could not open artifact for writing: %s\n"
                "  (run from the repository root so the relative path resolves)\n",
                artifactPath);
  }

  return pass ? 0 : 1;
}
