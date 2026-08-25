#!/usr/bin/env bash
#
# evaluate-transport-quality.sh — pure, board-independent evaluator half of
# the T059 (US9) HIL transport-quality harness (FR-050, FR-034a, FR-034c,
# US9 AS2).
#
# Takes two CDC diagnostic snapshot files (produced by T058's
# adapters/nucleo/support/diagnostic-serializer.h `key=value` wire format,
# newest-complete-line-wins, captured by read-serial-snapshot.sh) — a
# "before" snapshot and an "after" snapshot bracketing a measurement window
# — and:
#   1. Parses both (iu/io/ou/oo/is/mp/bp/wb/tl, the exact keys
#      diagnostic-serializer.h emits, in its declared field order).
#   2. Computes each error-class counter's DELTA between the two snapshots,
#      wraparound-safe modulo 2^32 (FR-034a: counters are monotonic modulo
#      2^32, not lifetime totals — a single wrap inside the window is a
#      legal, expected condition, not a fault).
#   3. Expresses each error-class delta as a normalized HEALTH RATE against
#      delta(blocksProcessed) — delta(counter)/delta(blocksProcessed) — never
#      a proportion of failed transfers and never a lifetime total
#      (FR-034c, US9 AS2; mirrors support/transport-stats.h's errorRate()).
#      A rate above 1.0 is legal and is NOT clamped: one block can both
#      underrun and see a malformed payload.
#   4. Applies the operator-settled (T061) fail bar: FAIL if ANY of
#      inputUnderruns/inputOverruns/outputUnderruns/outputOverruns/
#      inputStarved/malformedPayloads has a nonzero delta over the window,
#      OR if blocksProcessed does not advance (delta==0 — a stalled
#      denominator is itself a failure, not an undefined/zero rate).
#      worstBlockMicros (wb) and timingSourceLive (tl) are REPORTED ONLY,
#      never gated — a dead timing source (tl=false, wb pinned at the
#      0xFFFFFFFF sentinel) must not fail a run whose transport counters are
#      otherwise clean.
#
# This script touches no hardware, no serial device, no USB — it only reads
# two text files given as arguments. That is what makes it self-testable
# with no board (see selftest-evaluate-transport-quality.sh and
# fixtures/*.snap) and reusable standalone from run-hil.sh.
#
# Usage:
#   evaluate-transport-quality.sh <before-snapshot-file> <after-snapshot-file>
#
# Exit status: 0 on PASS, 1 on FAIL, 2 on usage/input error (missing file,
# malformed/incomplete snapshot). No fallback, no mock data: a malformed or
# incomplete snapshot file is a hard error naming the missing field, never
# silently treated as zero.

set -u

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 2
}

note() { printf '%s\n' "$*"; }

if [ "$#" -ne 2 ]; then
  die "usage: evaluate-transport-quality.sh <before-snapshot-file> <after-snapshot-file>"
fi

before_file="$1"
after_file="$2"

[ -f "$before_file" ] || die "before-snapshot file not found: $before_file"
[ -f "$after_file" ] || die "after-snapshot file not found: $after_file"
[ -r "$before_file" ] || die "before-snapshot file not readable: $before_file"
[ -r "$after_file" ] || die "after-snapshot file not readable: $after_file"

is_uint() {
  case "$1" in
    '' | *[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}

# Parses a `key=value` snapshot file into the fixed globals F_IU F_IO F_OU
# F_OO F_IS F_MP F_BP F_WB F_TL, matching diagnostic-serializer.h's nine
# keys exactly. No associative arrays (POSIX-portable / bash-3.2-safe, since
# macOS ships bash 3.2 and this harness runs on a macOS dev host).
parse_snapshot() {
  F_IU=""; F_IO=""; F_OU=""; F_OO=""; F_IS=""; F_MP=""; F_BP=""; F_WB=""; F_TL=""
  local key value
  while IFS='=' read -r key value; do
    [ -z "$key" ] && continue
    value="${value%$'\r'}"
    case "$key" in
      iu) F_IU="$value" ;;
      io) F_IO="$value" ;;
      ou) F_OU="$value" ;;
      oo) F_OO="$value" ;;
      is) F_IS="$value" ;;
      mp) F_MP="$value" ;;
      bp) F_BP="$value" ;;
      wb) F_WB="$value" ;;
      tl) F_TL="$value" ;;
    esac
  done < "$1"
}

parse_snapshot "$before_file"
BEFORE_IU="$F_IU"; BEFORE_IO="$F_IO"; BEFORE_OU="$F_OU"; BEFORE_OO="$F_OO"
BEFORE_IS="$F_IS"; BEFORE_MP="$F_MP"; BEFORE_BP="$F_BP"; BEFORE_WB="$F_WB"; BEFORE_TL="$F_TL"

parse_snapshot "$after_file"
AFTER_IU="$F_IU"; AFTER_IO="$F_IO"; AFTER_OU="$F_OU"; AFTER_OO="$F_OO"
AFTER_IS="$F_IS"; AFTER_MP="$F_MP"; AFTER_BP="$F_BP"; AFTER_WB="$F_WB"; AFTER_TL="$F_TL"

require_uint_field() {
  # $1=label $2=file $3=value
  is_uint "$3" || die "$2: field '$1' missing or not a uint32 (\"$3\") — incomplete or malformed snapshot"
}

require_bool_field() {
  # $1=label $2=file $3=value
  case "$3" in
    true | false) : ;;
    *) die "$2: field '$1' missing or not true/false (\"$3\") — incomplete or malformed snapshot" ;;
  esac
}

require_uint_field iu "$before_file" "$BEFORE_IU"
require_uint_field io "$before_file" "$BEFORE_IO"
require_uint_field ou "$before_file" "$BEFORE_OU"
require_uint_field oo "$before_file" "$BEFORE_OO"
require_uint_field is "$before_file" "$BEFORE_IS"
require_uint_field mp "$before_file" "$BEFORE_MP"
require_uint_field bp "$before_file" "$BEFORE_BP"
require_uint_field wb "$before_file" "$BEFORE_WB"
require_bool_field tl "$before_file" "$BEFORE_TL"

require_uint_field iu "$after_file" "$AFTER_IU"
require_uint_field io "$after_file" "$AFTER_IO"
require_uint_field ou "$after_file" "$AFTER_OU"
require_uint_field oo "$after_file" "$AFTER_OO"
require_uint_field is "$after_file" "$AFTER_IS"
require_uint_field mp "$after_file" "$AFTER_MP"
require_uint_field bp "$after_file" "$AFTER_BP"
require_uint_field wb "$after_file" "$AFTER_WB"
require_bool_field tl "$after_file" "$AFTER_TL"

# Wraparound-safe delta: counters are monotonic modulo 2^32 (FR-034a). A
# single wrap inside the measurement window is legal; this computes the
# correct forward delta rather than a huge negative number.
wrap_delta() {
  # $1=before $2=after
  local before="$1" after="$2" raw
  raw=$((after - before))
  if [ "$raw" -lt 0 ]; then
    raw=$((raw + 4294967296))
  fi
  printf '%s' "$raw"
}

rate_of() {
  # $1=delta $2=deltaBp -> formatted rate, or "N/A" if deltaBp is 0
  local delta="$1" deltaBp="$2"
  if [ "$deltaBp" -eq 0 ]; then
    printf 'N/A'
    return
  fi
  awk -v d="$delta" -v b="$deltaBp" 'BEGIN { printf "%.6f", d / b }'
}

fail=0

delta_bp=$(wrap_delta "$BEFORE_BP" "$AFTER_BP")

note "== HIL transport-quality evaluation =="
note "before: $before_file"
note "after:  $after_file"
note ""
note "blocksProcessed (bp): before=$BEFORE_BP after=$AFTER_BP delta=$delta_bp"
if [ "$delta_bp" -eq 0 ]; then
  note "  FAIL: blocksProcessed did not advance between snapshots (stalled denominator)"
  fail=1
fi
note ""

report_counter() {
  # $1=key $2=label $3=before $4=after
  local key="$1" label="$2" before="$3" after="$4" delta rate
  delta=$(wrap_delta "$before" "$after")
  rate=$(rate_of "$delta" "$delta_bp")
  note "$label ($key): before=$before after=$after delta=$delta rate(delta/deltaBp)=$rate"
  if [ "$delta" -ne 0 ]; then
    note "  FAIL: nonzero $label delta over the measurement window"
    fail=1
  fi
}

report_counter iu inputUnderruns "$BEFORE_IU" "$AFTER_IU"
report_counter io inputOverruns "$BEFORE_IO" "$AFTER_IO"
report_counter ou outputUnderruns "$BEFORE_OU" "$AFTER_OU"
report_counter oo outputOverruns "$BEFORE_OO" "$AFTER_OO"
report_counter is inputStarved "$BEFORE_IS" "$AFTER_IS"
report_counter mp malformedPayloads "$BEFORE_MP" "$AFTER_MP"

note ""
note "worstBlockMicros (wb): before=$BEFORE_WB (tl=$BEFORE_TL) after=$AFTER_WB (tl=$AFTER_TL) — report-only, NOT gated"
if [ "$AFTER_TL" = "false" ] || [ "$BEFORE_TL" = "false" ]; then
  note "  note: timing source was not live for at least one snapshot; wb carries the dead-timer sentinel there, not a real measurement"
fi

note ""
if [ "$fail" -eq 0 ]; then
  note "VERDICT: PASS"
else
  note "VERDICT: FAIL"
fi

exit "$fail"
