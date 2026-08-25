#!/usr/bin/env bash
#
# selftest-evaluate-transport-quality.sh — proves evaluate-transport-quality.sh
# (T059, US9) correct with NO board and NO serial device: it drives the pure
# evaluator against committed fixture snapshot pairs under fixtures/ and
# checks each pair's exit code against what the settled fail bar (T061)
# requires. Runs anywhere; this is how the harness's logic goes GREEN
# without hardware.
#
# Coverage (fixtures/*.snap):
#   clean-*              — all counters flat, bp advances        -> PASS (0)
#   malformed-payload-*  — nonzero malformedPayloads (mp) delta  -> FAIL (1)
#   output-underrun-*    — nonzero outputUnderruns (ou) delta    -> FAIL (1)
#   stalled-blocks-*     — blocksProcessed (bp) does not advance -> FAIL (1)
#   timer-dead-*         — tl=false, wb pinned at the dead-timer
#                           sentinel, but transport counters clean
#                           and bp advances (wb/tl are report-only,
#                           never gated)                          -> PASS (0)
#   wraparound-*         — bp wraps modulo 2^32 across the window,
#                           proving delta arithmetic handles the
#                           wrap rather than producing a bogus huge
#                           negative delta                        -> PASS (0)
#
# Usage: selftest-evaluate-transport-quality.sh
# Exit status: 0 if every fixture pair's actual exit code matched its
# expected exit code, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EVAL="$SCRIPT_DIR/evaluate-transport-quality.sh"
FIXTURES="$SCRIPT_DIR/fixtures"

fail=0

check() {
  # $1=case-name $2=before-fixture $3=after-fixture $4=expected-exit-code
  local name="$1" before="$2" after="$3" expect="$4"
  local out actual

  out="$("$EVAL" "$FIXTURES/$before" "$FIXTURES/$after" 2>&1)"
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

check "clean snapshot pair (steady state, no errors)" \
  clean-before.snap clean-after.snap 0

check "nonzero malformedPayloads delta" \
  malformed-payload-before.snap malformed-payload-after.snap 1

check "nonzero outputUnderruns delta" \
  output-underrun-before.snap output-underrun-after.snap 1

check "blocksProcessed does not advance (stalled denominator)" \
  stalled-blocks-before.snap stalled-blocks-after.snap 1

check "dead timing source (wb sentinel / tl=false, report-only)" \
  timer-dead-before.snap timer-dead-after.snap 0

check "blocksProcessed wraps modulo 2^32 across the window" \
  wraparound-before.snap wraparound-after.snap 0

if [ "$fail" -eq 0 ]; then
  printf 'All evaluator self-tests PASSED.\n'
else
  printf 'One or more evaluator self-tests FAILED.\n'
fi

exit "$fail"
