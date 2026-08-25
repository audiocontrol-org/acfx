#!/usr/bin/env bash
#
# evaluate-packet-capture.sh — pure, board-independent evaluator half of
# the T024 (US5) dual-direction packet-capture harness (FR-002, FR-003,
# FR-013).
#
# Consumes TWO per-USB-frame packet-size series (one for the IN endpoint,
# one for the OUT endpoint), produced by the T002 capture backend, and
# evaluates transport health per DIRECTION:
#   - the selected sample rate and subslot size (bytes/sample), INFERRED
#     from the packet-size histogram (never declared on the command line —
#     the capture is the objective source of truth, not an assumption);
#   - USB frame total and audio frame total;
#   - a packet-size histogram;
#   - ZLP (zero-length packet) count and non-nominal (short/oversize)
#     packet count;
#   - effective frames/second;
# and ASSERTS, per direction:
#   - zero ZLP/short packets in steady state (FR-002/FR-013);
#   - the accumulated audio-frame count tracks the EXACT SOF-derived
#     rational schedule (e.g. 48000/1000 at 48 kHz; the 44/45 fractional
#     schedule averaging exactly 44100/1000 at 44.1 kHz — not a naive
#     44/45 alternation, which averages 44.5 kHz) — this same check also
#     covers OUT health (FR-003): a systematic input-ring drift or
#     accumulated over/underrun shows up as a schedule mismatch even when
#     every individual packet size is individually "nominal".
#
# PASS = zero ZLP/short in steady state AND accumulated frames tracking
# the exact SOF-derived schedule, for BOTH directions.
#
# All actual computation lives in the PURE awk module lib/
# packet-capture-core.awk (input: a packet-size series; output: the
# metrics, or a descriptive "error=" line). This script is a thin CLI
# wrapper: it runs that module once per direction, parses its key=value
# report into shell variables (bash-3.2-safe — printf -v, no associative
# arrays, matching evaluate-transport-quality.sh's convention), renders a
# human-readable report, and applies the PASS/FAIL bar.
#
# This script and its core module touch no hardware, no serial device, no
# USB — they only read two text files given as arguments. That is what
# makes this self-testable with no board (see
# selftest-evaluate-packet-capture.sh and fixtures/packet-capture/*.series)
# and reusable standalone from a future capture-driving runner.
#
# Usage:
#   evaluate-packet-capture.sh <in-series-file> <out-series-file>
#
# Exit status: 0 on PASS, 1 on FAIL, 2 on usage/input error (missing file,
# malformed/unusable series). No fallback, no mock data: a malformed or
# unusable series file is a hard error naming the problem, never silently
# treated as zero/healthy.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CORE_AWK="$SCRIPT_DIR/lib/packet-capture-core.awk"

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 2
}

note() { printf '%s\n' "$*"; }

[ -f "$CORE_AWK" ] || die "core evaluator module not found: $CORE_AWK"

if [ "$#" -ne 2 ]; then
  die "usage: evaluate-packet-capture.sh <in-series-file> <out-series-file>"
fi

in_file="$1"
out_file="$2"

[ -f "$in_file" ] || die "IN packet-size series file not found: $in_file"
[ -r "$in_file" ] || die "IN packet-size series file not readable: $in_file"
[ -f "$out_file" ] || die "OUT packet-size series file not found: $out_file"
[ -r "$out_file" ] || die "OUT packet-size series file not readable: $out_file"

# Runs the pure awk core over one direction's series file, parses its
# key=value report, prints a human-readable per-direction section, and
# sets ${prefix}FAIL to "0" or "1". Calls die() (exit 2) on a malformed or
# unusable series — this is an input-validity failure, distinct from a
# transport-health FAIL.
run_direction() {
  # $1=label (IN|OUT) $2=file $3=varname-prefix (e.g. IN_ or OUT_)
  local label="$1" file="$2" prefix="$3"
  local report key value
  local rate="" subslot="" bitdepth="" usbf="" audiof="" fps=""
  local zlp="" nonnom="" schedexp="" mismatch="" firstmis=""
  local hist_lines=""

  report="$(awk -v CHANNELS=2 -f "$CORE_AWK" "$file" 2>&1)"

  while IFS='=' read -r key value; do
    [ -z "$key" ] && continue
    case "$key" in
      error) die "$label ($file): $value" ;;
      rate_hz) rate="$value" ;;
      subslot_bytes) subslot="$value" ;;
      bit_depth) bitdepth="$value" ;;
      usb_frames) usbf="$value" ;;
      audio_frames) audiof="$value" ;;
      effective_fps) fps="$value" ;;
      zlp_count) zlp="$value" ;;
      non_nominal_count) nonnom="$value" ;;
      schedule_expected_total) schedexp="$value" ;;
      schedule_mismatch_count) mismatch="$value" ;;
      schedule_first_mismatch_frame) firstmis="$value" ;;
      hist_*)
        hist_lines="${hist_lines}    ${key#hist_} bytes: ${value} packets
"
        ;;
      *) : ;;
    esac
  done <<CORE_REPORT
$report
CORE_REPORT

  [ -n "$rate" ] || die "$label ($file): core evaluator module produced no usable report"

  note "== $label direction: $file =="
  note "  selected sample rate: ${rate} Hz"
  note "  selected subslot size: ${subslot} bytes/sample (${bitdepth}-bit)"
  note "  USB frames observed: ${usbf}"
  note "  audio frames observed: ${audiof} (exact SOF-derived schedule expects: ${schedexp})"
  note "  effective frames/second: ${fps}"
  note "  ZLP count: ${zlp}"
  note "  non-nominal (short/oversize) packet count: ${nonnom}"
  if [ "${mismatch:-0}" -ne 0 ] 2>/dev/null && [ -n "$firstmis" ] && [ "$firstmis" -ne 0 ]; then
    note "  schedule mismatches: ${mismatch} (first at USB frame ${firstmis})"
  else
    note "  schedule mismatches: ${mismatch}"
  fi
  note "  packet-size histogram:"
  if [ -n "$hist_lines" ]; then
    printf '%s' "$hist_lines"
  else
    note "    (no packets observed)"
  fi

  if [ "$zlp" != "0" ] || [ "$nonnom" != "0" ] || [ "$mismatch" != "0" ]; then
    printf -v "${prefix}FAIL" '%s' "1"
    note "  VERDICT ($label): FAIL"
    [ "$zlp" != "0" ] && note "    - nonzero ZLP count in steady state (FR-002/FR-013)"
    [ "$nonnom" != "0" ] && note "    - nonzero non-nominal (short/oversize) packet count in steady state (FR-002/FR-013)"
    [ "$mismatch" != "0" ] && note "    - accumulated audio frames diverge from the exact SOF-derived schedule (FR-002/FR-003/FR-013)"
  else
    printf -v "${prefix}FAIL" '%s' "0"
    note "  VERDICT ($label): PASS"
  fi
  note ""
}

note "== USB packet-capture transport evaluation =="
note ""

run_direction "IN" "$in_file" "IN_"
run_direction "OUT" "$out_file" "OUT_"

fail=0
[ "$IN_FAIL" = "1" ] && fail=1
[ "$OUT_FAIL" = "1" ] && fail=1

note "== overall verdict =="
if [ "$fail" -eq 0 ]; then
  note "PASS: zero ZLP/short packets in steady state AND accumulated frames track the exact SOF-derived schedule, for both IN and OUT (FR-002/FR-003/FR-013)."
else
  note "FAIL: see per-direction verdicts above."
fi

exit "$fail"
