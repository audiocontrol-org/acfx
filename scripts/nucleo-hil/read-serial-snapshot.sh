#!/usr/bin/env bash
#
# read-serial-snapshot.sh — counter-reader step of the T059 (US9) HIL
# transport-quality harness (FR-050, FR-033a, FR-033c/d).
#
# Opens the board's CDC serial device (macOS: /dev/cu.usbmodem*) and
# captures the NEWEST COMPLETE `key=value` diagnostic snapshot emitted by
# T058's main-loop diagnostic service (adapters/nucleo/support/
# diagnostic-serializer.h) into an output file, verbatim (same wire text —
# evaluate-transport-quality.sh parses this exact format).
#
# TOLERANT OF GAPS. diagnostic-service.h's CDC TX FIFO is only 64 bytes and
# it deliberately DROPS an entire snapshot (never a partial line) when the
# FIFO can't take one — so not every service-loop pass yields a line on the
# wire. This reader does not assume a fixed cadence: it reads whatever lines
# arrive, tracks progress through the nine expected keys in
# diagnostic-serializer.h's declared order (iu io ou oo is mp bp wb tl), and
# on seeing all nine in order treats that as one complete snapshot. It keeps
# listening up to the wait budget and remembers only the LAST complete
# snapshot it saw — "the newest complete snapshot at each sampling point",
# per the T059 task brief — discarding any partial/torn record left over
# from a drop mid-sequence or from starting mid-stream.
#
# Usage:
#   read-serial-snapshot.sh <serial-device-path> <output-file> [max-wait-seconds]
#
# max-wait-seconds defaults to 10. Exit status: 0 with a snapshot written to
# <output-file>, or 1 with a descriptive error on stderr and NO output file
# written (no fallback, no partial/mock snapshot ever written).

set -u

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  die "usage: read-serial-snapshot.sh <serial-device-path> <output-file> [max-wait-seconds]"
fi

device="$1"
outfile="$2"
max_wait="${3:-10}"

case "$max_wait" in
  '' | *[!0-9]*) die "max-wait-seconds must be a positive integer, got \"$max_wait\"" ;;
esac

[ -e "$device" ] || die "serial device not found: $device (is the board attached and enumerated? check /dev/cu.usbmodem*)"
[ -r "$device" ] || die "serial device not readable: $device (permissions?)"

stty -f "$device" 115200 raw -echo 2>/dev/null \
  || die "failed to configure $device via 'stty -f $device 115200 raw -echo' — is this a valid tty device?"

exec 3< "$device" || die "failed to open $device for reading"

keys="iu io ou oo is mp bp wb tl"
# shellcheck disable=SC2206 # word-splitting is intentional: fixed, space-separated key list
key_list=($keys)
num_keys=${#key_list[@]}

next_index=0
candidate=""
best_snapshot=""
have_snapshot=0

start_ts=$(date +%s)

while :; do
  now=$(date +%s)
  elapsed=$((now - start_ts))
  remaining=$((max_wait - elapsed))
  if [ "$remaining" -le 0 ]; then
    break
  fi

  if ! IFS= read -r -t "$remaining" line <&3; then
    break
  fi
  line="${line%$'\r'}"
  [ -z "$line" ] && continue

  key="${line%%=*}"

  if [ "$key" = "iu" ]; then
    # Start (or restart) collecting a new record here, regardless of where
    # we were — a fresh "iu=" line always begins a new snapshot per
    # diagnostic-serializer.h's field order.
    candidate="${line}"$'\n'
    next_index=1
    continue
  fi

  expected="${key_list[$next_index]}"
  if [ "$next_index" -eq 0 ] || [ "$key" != "$expected" ]; then
    # Out-of-sequence line (e.g. we started mid-stream, or a record was torn
    # by a drop): discard whatever we were collecting and wait for the next
    # "iu=" to resynchronize.
    candidate=""
    next_index=0
    continue
  fi

  candidate="${candidate}${line}"$'\n'
  next_index=$((next_index + 1))

  if [ "$next_index" -eq "$num_keys" ]; then
    best_snapshot="$candidate"
    have_snapshot=1
    candidate=""
    next_index=0
    # Keep listening until max_wait elapses so a later, newer snapshot (if
    # any arrives) overwrites this one — "newest complete snapshot", not
    # "first complete snapshot".
  fi
done

exec 3<&-

if [ "$have_snapshot" -eq 0 ]; then
  die "no complete diagnostic snapshot received from $device within ${max_wait}s — confirm the firmware's T058 diagnostic service is running and the CDC port is open"
fi

printf '%s' "$best_snapshot" > "$outfile" \
  || die "failed to write snapshot to $outfile"
