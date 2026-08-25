# `scripts/nucleo-hil/` — hardware-in-the-loop harness (board-dependent, CI-excluded)

This directory holds the manual, **board-attached** verification harness for the
NUCLEO-F446RE USB-audio firmware. It is **excluded from CI** (it needs a physical
board + a macOS host) and is invoked by the operator, never by the automated
suite. There is **no** `.github/`, `ctest`, or `check-portability.sh` reference to
anything here — mirroring the base US9 HIL harness (T059/T060). The host-only
self-tests below run *without* a board.

## Two evaluators (each has a board-independent pure core + a host self-test)

- **Transport-quality** (base US9): `run-hil.sh` (flash → snapshot → duplex
  known-signal stream → snapshot → evaluate) with `evaluate-transport-quality.sh`
  + `read-serial-snapshot.sh`. Reads the CDC diagnostic counters off the board.
  Self-test (no board): `./selftest-evaluate-transport-quality.sh`.
- **Packet-capture** (this feature, US5 / FR-013): `evaluate-packet-capture.sh`
  over `lib/packet-capture-core.awk` — a *pure* dual-direction evaluator that
  consumes a per-USB-frame packet-size series for the IN and OUT endpoints,
  infers the rate/subslot from the histogram, replays the exact SOF rational
  schedule frame-by-frame, and PASS/FAILs on zero-ZLP/short + accumulated-rate
  tracking + no OUT-side drift. Self-test (no board):
  `./selftest-evaluate-packet-capture.sh` (runs the fixtures in
  `fixtures/packet-capture/`).

- **Single-clock audio loopback** (reliable, this session): `loopback-audio-check.sh`
  over `acfx-loopback.swift` — opens ONE AUHAL unit on the `acfx Audio` device
  with input+output enabled, so capture and playback share ONE IOProc / ONE
  clock domain (the reliable analogue of a DAW aggregate). It plays a sine tone
  through the device+effect, captures the return, and objectively measures
  **pitch (cents)** and a **THD+N residual** (least-squares-removing the
  fundamental), and reads the CDC counters before/after. This exists because the
  two-independent-streams approach (`ffmpeg` capture + `sox` play) is flaky
  against a synchronous device — two unsynchronised host clocks underrun on their
  own. On a healthy board a tone returns pitch-correct (a few cents) and
  tone-dominant (~-25 dB THD+N — the known TASK-39 boundary-misalignment floor,
  audio intact); the original async-transport defect instead produced a gross
  multi-semitone pitch-down + audible noise. Run: `./loopback-audio-check.sh
  [toneHz] [seconds]` (auto-compiles the Swift tester, auto-detects the CDC
  serial device). Needs `swiftc`.

## Capture backend (T002) — operator/host-owned, not in this repo

`evaluate-packet-capture.sh` consumes a per-frame size series; **producing** that
series from live USB traffic is the **T002 capture backend**, which is
operator/macOS-host-dependent (e.g. Wireshark/`tshark` over the `XHC20`
interface via libpcap, possibly requiring a SIP change) and is chosen/confirmed
on the operator's machine. `usbmon` is Linux-only — do not assume it on macOS.
The evaluator is deliberately decoupled from the capture mechanism so its logic
is testable with fixtures and no hardware.

## No fallbacks

Per project rule, the board-dependent scripts check every required tool, the
board, and the CDC serial device up front and **fail loud** naming exactly what
is missing — they never substitute a mock result.
