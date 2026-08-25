#!/usr/bin/env bash
#
# loopback-audio-check.sh — reliable single-clock-domain audio HIL check.
#
# THE FLAKY-RIG PROBLEM this replaces: driving a SYNCHRONOUS USB-audio device
# with two independent host streams (e.g. `ffmpeg` capturing input + `sox`
# playing output) means two unsynchronised host clocks against one SOF-timed
# device — it underruns on its own and returns intermittent digital silence,
# so it cannot reliably test the transport. This check instead uses ONE AUHAL
# unit (acfx-loopback.swift) with input+output enabled on the same device, so
# capture and playback share ONE IOProc / ONE clock domain — the reliable
# analogue of a DAW aggregate, which is exactly the condition the synchronous
# transport is designed for.
#
# It plays a sine tone through the device+effect, captures the returning audio,
# and objectively measures: dominant frequency (pitch-correctness, in cents) and
# a THD+N residual (least-squares-removing the fundamental). It ALSO reads the
# T058 CDC diagnostic counters before/after so the audio result and the
# transport counters can be read together.
#
# INTERPRETING THE COUNTERS (see support/diagnostic-serializer.h for the keys):
#   bp=blocksProcessed grows = healthy throughput.
#   ou=outputUnderruns is an EVENT counter that trips on ANY partial-block
#   zero-fill; per backlog TASK-39 it reads high under HEALTHY audio because the
#   SOF-paced IN pull (min(room,48)) does not align to the 48-frame DSP block
#   boundary. Its audible effect is small (a ~0.19% time-stretch / a few cents,
#   and a ~-25 dB THD+N residual on this device) — NOT the gross pitch-down +
#   noise of the original async-transport defect, which this feature fixed.
#
# BOARD-DEPENDENT, CI-EXCLUDED. Requires a flashed NUCLEO-F446RE, its OTG-FS
# audio cable enumerated as "acfx Audio", its CDC serial device, and a macOS
# host with swiftc. Per project rule it fails loud on any missing piece.
#
# Usage: loopback-audio-check.sh [toneHz] [seconds] [sampleRate] [serial-dev] [device-name]

set -u
HZ="${1:-440}"; DUR="${2:-6}"; RATE="${3:-48000}"
SERIAL="${4:-}"; DEVNAME="${5:-acfx Audio}"
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/acfx-loopback"

die() { printf '%s\n' "$*" >&2; exit 1; }
command -v swiftc >/dev/null 2>&1 || die "swiftc not found (Xcode command line tools required)"

# compile the tester if missing or stale
if [ ! -x "$BIN" ] || [ "$DIR/acfx-loopback.swift" -nt "$BIN" ]; then
  swiftc -O "$DIR/acfx-loopback.swift" -o "$BIN" || die "swiftc failed to build acfx-loopback"
fi

# resolve the CDC serial device if not given (exactly one acfx CDC expected)
if [ -z "$SERIAL" ]; then
  for p in /dev/cu.usbmodem*; do
    [ -e "$p" ] || continue
    line="$( (stty -f "$p" 115200 raw -echo 2>/dev/null; timeout 2 cat "$p" 2>/dev/null | grep -m1 -E '^bp=') )"
    [ -n "$line" ] && { SERIAL="$p"; break; }
  done
  [ -n "$SERIAL" ] || die "no acfx CDC diagnostic serial device found among /dev/cu.usbmodem* (is the board flashed + audio cable attached?)"
fi
[ -e "$SERIAL" ] || die "serial device not found: $SERIAL"

snap() { (stty -f "$SERIAL" 115200 raw -echo 2>/dev/null; timeout 3 cat "$SERIAL" 2>/dev/null \
          | grep -E '^(iu|io|ou|oo|is|mp|bp|wb|tl)=' | tail -9); }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "=== CDC BEFORE ==="; snap | tee "$TMP/before.snap"
echo "=== single-clock full-duplex loopback: ${HZ} Hz, ${DUR}s ==="
"$BIN" "$DEVNAME" "$HZ" "$DUR" "$RATE"; RES=$?
echo "=== CDC AFTER ==="; snap | tee "$TMP/after.snap"

echo "=== counter deltas ==="
python3 - "$TMP/before.snap" "$TMP/after.snap" <<'PY'
import sys
def load(p):
    d={}
    for ln in open(p):
        ln=ln.strip()
        if "=" in ln: k,v=ln.split("=",1); d[k]=v
    return d
b=load(sys.argv[1]); a=load(sys.argv[2])
lab={"iu":"inputUnderruns","io":"inputOverruns","ou":"outputUnderruns","oo":"outputOverruns",
     "is":"inputStarved","mp":"malformedPayloads","bp":"blocksProcessed","wb":"worstBlockMicros","tl":"timingSourceLive"}
for k in ["iu","io","ou","oo","is","mp","bp","wb","tl"]:
    bv,av=b.get(k,"?"),a.get(k,"?"); d=""
    try: d=f"  (+{int(av)-int(bv)})" if int(av)!=int(bv) else "  (0)"
    except: pass
    print(f"  {k:3} {lab[k]:18} {bv:>7} -> {av:>7}{d}")
PY
echo "loopback RESULT exit=$RES  (0=PASS: pitch-correct + tone-dominant + signal-present)"
exit "$RES"
