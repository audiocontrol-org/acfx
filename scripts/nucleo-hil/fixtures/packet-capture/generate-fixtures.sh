#!/usr/bin/env bash
#
# generate-fixtures.sh — (re)generates the deterministic synthetic
# packet-size-series fixtures in this directory, consumed by
# selftest-evaluate-packet-capture.sh to prove the T024 (US5)
# packet-capture evaluator correct with NO board. Fixture tooling only —
# not part of the evaluator. Idempotent and side-effect-free beyond
# rewriting the .series files below: no randomness, so re-running produces
# byte-identical output.
#
# Usage: generate-fixtures.sh

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GEN="$SCRIPT_DIR/../../lib/generate-series.awk"
OUT_DIR="$SCRIPT_DIR"

[ -f "$GEN" ] || { printf 'ERROR: generator module not found: %s\n' "$GEN" >&2; exit 1; }

gen() {
  # $1=MODE $2=RATE $3=SUBSLOT $4=N $5=ZLP_AT $6=SHORT_AT $7=out-file
  awk -v MODE="$1" -v RATE="$2" -v SUBSLOT="$3" -v CHANNELS=2 -v N="$4" \
      -v ZLP_AT="$5" -v SHORT_AT="$6" \
      -f "$GEN" > "$OUT_DIR/$7"
  printf 'wrote %s (%d lines)\n' "$7" "$(wc -l < "$OUT_DIR/$7" | tr -d ' ')"
}

# (a) healthy 48 kHz / 16-bit, IN + OUT — steady 48 audio-frames/USB-frame.
gen healthy 48000 2 1000 "" "" healthy-48k16-in.series
gen healthy 48000 2 1000 "" "" healthy-48k16-out.series

# (b) healthy 44.1 kHz / 16-bit, IN + OUT — exercises the 44/45 fractional
# rational-accumulator schedule (spec FR-002).
gen healthy 44100 2 1000 "" "" healthy-44k16-in.series
gen healthy 44100 2 1000 "" "" healthy-44k16-out.series

# (c) healthy 48 kHz / packed-24-bit, IN + OUT (FR-005/FR-010).
gen healthy 48000 3 1000 "" "" healthy-48k24-in.series
gen healthy 48000 3 1000 "" "" healthy-48k24-out.series

# (d) unhealthy IN: ZLPs and short packets in otherwise-steady-state 48 kHz
# / 16-bit — MUST FAIL. Paired in the self-test with the already-healthy
# healthy-48k16-out.series above (no separate OUT fixture needed).
gen zlp_short 48000 2 200 "20,75,150" "40,110" unhealthy-zlp-short-in.series

# (e) OUT systematic drift: a naive 44/45 alternation (averages 44.5 kHz)
# instead of the exact 44100/1000 SOF-derived schedule — every individual
# packet size is still "nominal", isolating the schedule/drift failure
# mode (FR-003) from the ZLP/short failure mode. Paired in the self-test
# with the already-healthy healthy-44k16-in.series above.
gen naive_alt 44100 2 1000 "" "" drift-out.series

printf 'done.\n'
