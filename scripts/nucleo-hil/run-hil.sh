#!/usr/bin/env bash
#
# run-hil.sh — top-level runner for the T059 (US9) HIL transport-quality
# harness (FR-050, FR-034a, FR-034c, US9 AS2/AS3). Ties together: flash ->
# snapshot-before -> full-duplex known-signal stream -> snapshot-after ->
# evaluate, on a board-attached macOS dev host.
#
# This is the BOARD-DEPENDENT piece. It requires a physical NUCLEO-F446RE
# attached over ST-Link/USB, a working Arm toolchain, Homebrew `stlink`,
# `ffmpeg`, and `sox`/`play` — and is therefore excluded from CI (T060) and
# invoked manually. It is UNVERIFIED ON HARDWARE as authored (no board
# available in this environment) — the flash/stream/serial mechanics mirror
# the ad-hoc rig recorded in the operator's "nucleo-hil-loopback-macos" note,
# already verified live against this firmware; the counter-readback and
# evaluation steps reuse read-serial-snapshot.sh and
# evaluate-transport-quality.sh, both exercisable independently.
#
# Per project rule (no fallbacks/mock data): every required tool, the board,
# and the CDC serial device are checked up front and this script FAILS LOUD
# naming exactly what is missing — it never substitutes a mock result.
#
# Usage:
#   run-hil.sh --elf <path/to/firmware.elf> --signal <path/to/signal.wav> \
#              [--duration <seconds, default 5>] \
#              [--serial-device <path, default: auto-detect /dev/cu.usbmodem*>] \
#              [--audio-device-name <name, default "acfx Audio">] \
#              [--out-dir <dir, default: a fresh mktemp -d>]
#
# Exit status: propagates evaluate-transport-quality.sh's verdict (0 PASS,
# 1 FAIL), or a nonzero setup-failure status with a descriptive message if
# any precondition (tool, board, device) is not met.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat >&2 <<'USAGE'
usage: run-hil.sh --elf <firmware.elf> --signal <signal.wav>
                   [--duration <seconds>] [--serial-device <path>]
                   [--audio-device-name <name>] [--out-dir <dir>]
USAGE
  exit 2
}

elf_path=""
signal_wav=""
duration=5
serial_device=""
audio_device_name="acfx Audio"
out_dir=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --elf) elf_path="${2:-}"; shift 2 ;;
    --signal) signal_wav="${2:-}"; shift 2 ;;
    --duration) duration="${2:-}"; shift 2 ;;
    --serial-device) serial_device="${2:-}"; shift 2 ;;
    --audio-device-name) audio_device_name="${2:-}"; shift 2 ;;
    --out-dir) out_dir="${2:-}"; shift 2 ;;
    -h | --help) usage ;;
    *) printf 'unrecognized argument: %s\n' "$1" >&2; usage ;;
  esac
done

[ -n "$elf_path" ] || { printf 'missing required --elf\n' >&2; usage; }
[ -n "$signal_wav" ] || { printf 'missing required --signal\n' >&2; usage; }
[ -f "$elf_path" ] || die "firmware ELF not found: $elf_path"
[ -f "$signal_wav" ] || die "signal WAV not found: $signal_wav"

case "$duration" in
  '' | *[!0-9]*) die "--duration must be a positive integer, got \"$duration\"" ;;
esac

if [ -z "$out_dir" ]; then
  out_dir="$(mktemp -d "${TMPDIR:-/tmp}/nucleo-hil.XXXXXX")" \
    || die "failed to create a temp output directory"
fi
mkdir -p "$out_dir" || die "failed to create --out-dir: $out_dir"

require_tool() {
  command -v "$1" >/dev/null 2>&1 \
    || die "required tool not found on PATH: $1 (Homebrew 'stlink' provides st-flash/st-info; 'ffmpeg' and 'sox' provide ffmpeg/play; the Arm GNU Toolchain provides arm-none-eabi-objcopy — see the project's ARM-toolchain PATH note if arm-none-eabi-objcopy resolves to a C-only Homebrew shim instead)"
}

require_tool arm-none-eabi-objcopy
require_tool st-flash
require_tool st-info
require_tool ffmpeg
require_tool play

# --- Confirm the board is present and is an F446 (chipid 0x421) ---
probe_out="$(st-info --probe 2>&1)"
printf '%s\n' "$probe_out"
if ! printf '%s\n' "$probe_out" | grep -qi '0421'; then
  die "st-info --probe did not report an F446 (chipid 0x0421) — is the NUCLEO-F446RE attached via its onboard ST-Link? full probe output above"
fi

# --- Flash ---
bin_path="$out_dir/firmware.bin"
arm-none-eabi-objcopy -O binary "$elf_path" "$bin_path" \
  || die "arm-none-eabi-objcopy failed converting $elf_path"
st-flash --reset write "$bin_path" 0x08000000 \
  || die "st-flash write failed — is the board attached and not held by another debugger session?"

# --- Locate the CDC serial device (allow retries: enumeration after a
# reset is not instantaneous) ---
if [ -z "$serial_device" ]; then
  attempt=0
  while [ "$attempt" -lt 10 ]; do
    # shellcheck disable=SC2206 # deliberate glob expansion into an array
    candidates=(/dev/cu.usbmodem*)
    if [ -e "${candidates[0]}" ]; then
      break
    fi
    attempt=$((attempt + 1))
    sleep 1
  done
  # shellcheck disable=SC2206
  candidates=(/dev/cu.usbmodem*)
  if [ ! -e "${candidates[0]}" ]; then
    die "no /dev/cu.usbmodem* serial device found after flashing — pass --serial-device explicitly if the device node uses a different name"
  fi
  if [ "${#candidates[@]}" -gt 1 ]; then
    die "multiple /dev/cu.usbmodem* devices found (${candidates[*]}) — pass --serial-device to disambiguate"
  fi
  serial_device="${candidates[0]}"
fi
[ -e "$serial_device" ] || die "serial device not found: $serial_device"

# --- Locate the avfoundation audio device index for the given name ---
device_list="$(ffmpeg -f avfoundation -list_devices true -i "" 2>&1)"
audio_index="$(printf '%s\n' "$device_list" \
  | grep -i "$audio_device_name" \
  | grep -Eo '\[[0-9]+\]' \
  | head -1 \
  | tr -d '[]')"
if [ -z "$audio_index" ]; then
  die "ffmpeg avfoundation did not list an audio device matching \"$audio_device_name\" — full device list:
$device_list"
fi

# --- Snapshot before ---
before_snap="$out_dir/before.snap"
"$SCRIPT_DIR/read-serial-snapshot.sh" "$serial_device" "$before_snap" \
  || die "failed to capture the BEFORE snapshot from $serial_device"

# --- Full-duplex known-signal stream: record device input while playing
# the known signal to device output, simultaneously (a USB-audio EFFECT
# yields silence on its input unless something is being played to its
# output at the same time) ---
recorded_wav="$out_dir/recorded.wav"
ffmpeg -y -f avfoundation -i ":$audio_index" -t "$duration" "$recorded_wav" \
  > "$out_dir/ffmpeg-record.log" 2>&1 &
rec_pid=$!

AUDIODEV="$audio_device_name" play "$signal_wav" \
  > "$out_dir/play.log" 2>&1
play_status=$?

wait "$rec_pid"
rec_status=$?

if [ "$play_status" -ne 0 ]; then
  die "play (AUDIODEV=\"$audio_device_name\") failed with status $play_status — see $out_dir/play.log"
fi
if [ "$rec_status" -ne 0 ]; then
  die "ffmpeg avfoundation recording failed with status $rec_status — see $out_dir/ffmpeg-record.log"
fi

# --- Snapshot after ---
after_snap="$out_dir/after.snap"
"$SCRIPT_DIR/read-serial-snapshot.sh" "$serial_device" "$after_snap" \
  || die "failed to capture the AFTER snapshot from $serial_device"

# --- Evaluate ---
printf 'run artifacts in: %s\n' "$out_dir"
"$SCRIPT_DIR/evaluate-transport-quality.sh" "$before_snap" "$after_snap"
exit $?
