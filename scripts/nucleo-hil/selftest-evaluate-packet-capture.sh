#!/usr/bin/env bash
#
# selftest-evaluate-packet-capture.sh — proves evaluate-packet-capture.sh
# (T024, US5) and its pure core (lib/packet-capture-core.awk) correct with
# NO board and NO USB capture device: it drives the evaluator against
# committed synthetic packet-size-series fixtures under
# fixtures/packet-capture/ and checks each pair's exit code against what
# the contract (specs/synchronous-usb-audio-transport/contracts/
# usb-transport-contract.md, FR-002/FR-003/FR-013) requires.
#
# Coverage (fixtures/packet-capture/*.series, all reproducible via
# fixtures/packet-capture/generate-fixtures.sh):
#   healthy-48k16-*   — steady 48 audio-frames/USB-frame, no ZLP/short
#                        -> PASS (0)
#   healthy-44k16-*   — the 44/45 fractional SOF-derived schedule,
#                        accumulated total tracks exactly 44100/1000
#                        -> PASS (0)
#   healthy-48k24-*   — packed-24-bit at 48 kHz                -> PASS (0)
#   unhealthy-zlp-short-in.series (paired with the already-healthy
#     healthy-48k16-out.series) — IN has ZLPs and short packets in
#     otherwise-steady-state 48 kHz/16-bit           -> FAIL (1)
#   drift-out.series (paired with the already-healthy
#     healthy-44k16-in.series) — OUT uses a naive 44/45 alternation
#     (averages 44.5 kHz; every individual packet is still "nominal") in
#     place of the exact 44100/1000 schedule -- a systematic drift that
#     zero-ZLP/short checking alone would miss (FR-003)  -> FAIL (1)
#
# Usage: selftest-evaluate-packet-capture.sh
# Exit status: 0 if every fixture pair's actual exit code matched its
# expected exit code, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EVAL="$SCRIPT_DIR/evaluate-packet-capture.sh"
FIXTURES="$SCRIPT_DIR/fixtures/packet-capture"

if [ ! -f "$FIXTURES/healthy-48k16-in.series" ]; then
  printf 'fixtures not found under %s — generating them now via generate-fixtures.sh\n' "$FIXTURES"
  "$FIXTURES/generate-fixtures.sh" || { printf 'ERROR: fixture generation failed\n' >&2; exit 1; }
fi

fail=0

check() {
  # $1=case-name $2=in-fixture $3=out-fixture $4=expected-exit-code
  local name="$1" in_fx="$2" out_fx="$3" expect="$4"
  local out actual

  out="$("$EVAL" "$FIXTURES/$in_fx" "$FIXTURES/$out_fx" 2>&1)"
  actual=$?

  printf -- '---- %s ----\n' "$name"
  printf '%s\n' "$out"

  if [ "$actual" -eq "$expect" ]; then
    printf 'SELFTEST PASS: %s (exit=%s, expected=%s)\n\n' "$name" "$actual" "$expect"
  else
    printf 'SELFTEST FAIL: %s (exit=%s, expected=%s)\n\n' "$name" "$actual" "$expect"
    fail=1
  fi
}

check "healthy 48kHz/16-bit, IN+OUT" \
  healthy-48k16-in.series healthy-48k16-out.series 0

check "healthy 44.1kHz/16-bit, IN+OUT (44/45 fractional schedule)" \
  healthy-44k16-in.series healthy-44k16-out.series 0

check "healthy 48kHz/packed-24-bit, IN+OUT" \
  healthy-48k24-in.series healthy-48k24-out.series 0

check "unhealthy IN: ZLPs + short packets in steady state" \
  unhealthy-zlp-short-in.series healthy-48k16-out.series 1

check "OUT systematic drift: naive 44/45 alternation vs exact schedule" \
  healthy-44k16-in.series drift-out.series 1

if [ "$fail" -eq 0 ]; then
  printf 'All packet-capture evaluator self-tests PASSED.\n'
else
  printf 'One or more packet-capture evaluator self-tests FAILED.\n'
fi

exit "$fail"
