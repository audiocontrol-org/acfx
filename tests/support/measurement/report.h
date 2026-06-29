#pragma once

// tests/support/measurement/report.h
//
// Opt-in CSV report for the acfx measurement harness (FR-014).
// Namespace: acfx::measure.  Host-side / offline ONLY — allocation + file I/O OK.
//
// == Design notes ==
//
// Serialization choices:
//   - `pass` field:   written as the literal strings "true" or "false".
//   - double fields:  default ostream formatting for finite values. NON-FINITE
//                     values are emitted as CANONICAL, parser-stable tokens —
//                     "nan", "inf", "-inf" — instead of the platform-dependent
//                     output of `ostream << nan` ("nan" vs "-1.#IND" vs "NaN")
//                     (AUDIT-20260629-15). A NaN `value` is reachable in practice:
//                     thd() returns NaN for an unmeasurable/dead effect, and that
//                     value may be logged to a row. Downstream CSV/quality-gate
//                     parsers therefore see one stable spelling on every host.
//
// String-field assumption:
//   The fields effect, metric, stimulus, and units are expected to be simple
//   token strings that contain NO embedded commas or double-quote characters.
//   This invariant must be maintained by the caller. No RFC-4180 quoting is
//   performed (minimal-first: add a quoter only when the need arises).
//
// "OFF by default" usage property:
//   A caller that never constructs or calls write() on a CsvReport produces no
//   file. CI correctness gates on doctest assertions; the CSV is an opt-in
//   artifact intended for offline trending and plotting (the lab-reuse seam,
//   Principle IX). The class itself is a pure data + serialization concern with
//   no dependency on the rest of the measurement harness.
//
// Canonical CSV column order (FIXED, must not change):
//   effect,metric,stimulus,sample_rate,block_size,value,units,tolerance,pass

#include <cmath>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace acfx::measure {

// ---------------------------------------------------------------------------
// MeasurementRow — one row in the canonical CSV schema (FR-014).
//
// Long/normalized form: one row per measured metric so that reports are
// directly plottable and trendable without per-call-site schema divergence.
// ---------------------------------------------------------------------------
struct MeasurementRow {
    std::string effect;      // effect identifier under test (e.g. "svf-lowpass")
    std::string metric;      // metric name (e.g. "magnitude", "thd", "latency",
                             //               "relative_exec_time")
    std::string stimulus;    // stimulus used (e.g. "sine@1kHz", "impulse")
    double      sampleRate;  // Hz
    int         blockSize;   // samples (relevant to relative_exec_time; use 0 otherwise)
    double      value;       // the measured value
    std::string units;       // e.g. "ratio", "dB", "radians", "samples",
                             //       "ms", "time/block", "count"
    double      tolerance;   // the named tolerance asserted against
    bool        pass;        // pass/fail vs the analytic reference bound
};

// ---------------------------------------------------------------------------
// CsvReport — opt-in CSV emitter; OFF by default.
//
// Usage:
//   CsvReport report;
//   report.add({...});
//   report.write("/tmp/results.csv");
//
// CI correctness gates on doctest assertions regardless of whether write() is
// called. Only call write() when you want an artifact for trending/plotting.
// ---------------------------------------------------------------------------
namespace detail {
// Canonical, parser-stable serialization for a double CSV field: finite values
// use default ostream formatting; non-finite values get fixed spellings so the
// CSV does not vary by platform/STL (AUDIT-20260629-15).
inline void writeCsvDouble(std::ostream& os, double v) {
    if (std::isnan(v))      os << "nan";
    else if (std::isinf(v)) os << (v < 0.0 ? "-inf" : "inf");
    else                    os << v;
}
} // namespace detail

class CsvReport {
public:
    // Append a row. Rows are emitted in insertion order.
    inline void add(const MeasurementRow& row) {
        rows_.push_back(row);
    }

    // Write the canonical CSV to `path`, overwriting any existing file.
    //
    // Output format:
    //   Line 1: canonical header (fixed column order, snake_case names)
    //   Lines 2+: one row per added MeasurementRow, fields in header order,
    //             comma-separated, no trailing comma, newline-terminated.
    //
    // Throws std::runtime_error if the file cannot be opened.
    inline void write(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) {
            throw std::runtime_error(
                "acfx::measure::CsvReport::write — cannot open file: " + path);
        }

        // Canonical header — FIXED order and EXACT snake_case names (FR-014).
        out << "effect,metric,stimulus,sample_rate,block_size,"
               "value,units,tolerance,pass\n";

        for (const MeasurementRow& r : rows_) {
            out << r.effect    << ',';
            out << r.metric    << ',';
            out << r.stimulus  << ',';
            detail::writeCsvDouble(out, r.sampleRate); out << ',';
            out << r.blockSize << ',';
            detail::writeCsvDouble(out, r.value);      out << ',';
            out << r.units     << ',';
            detail::writeCsvDouble(out, r.tolerance);  out << ',';
            out << (r.pass ? "true" : "false") << '\n';
        }
    }

private:
    std::vector<MeasurementRow> rows_;
};

} // namespace acfx::measure
