# Quickstart: verifying the synchronous, multi-format transport

The runnable validation scenarios that prove the feature. Two gates: (A) an **objective host-side
USB packet capture** (independent of any resampler), and (B) **operator acceptance in Logic** via a
CoreAudio aggregate. See [contracts/usb-transport-contract.md](./contracts/usb-transport-contract.md)
for the pass conditions and [research.md](./research.md) for the mechanism.

## Prerequisites
- NUCLEO-F446RE attached via its onboard ST-Link (flash + the acfx composite device on the OTG-FS
  breakout — PA11/PA12; two cables, per `adapters/nucleo/README.md`).
- Arm GNU Toolchain on PATH (`/Applications/ArmGNUToolchain/<ver>/arm-none-eabi/bin`).
- Homebrew `stlink`; macOS host (for the aggregate acceptance).
- A host-side USB packet-capture facility for the objective gate (the tool built under
  `scripts/nucleo-hil/` for FR-013).

## Build & flash
```
cmake --build --preset test -j && ctest --preset test          # host suite (conversion, cadence logic) green
PATH=".../arm-none-eabi/bin:$PATH" cmake --build --preset nucleo   # cross-build; FIFO static_asserts hold
./scripts/check-portability.sh                                 # core acquired no adapter dep; ≤500 lines
arm-none-eabi-objcopy -O binary build/nucleo/adapters/nucleo/acfx_nucleo_lofi_delay.elf x.bin
st-flash --serial <F446-serial> --reset write x.bin 0x08000000
```

## Gate A — objective USB packet capture (FR-013 / SC-002), per rate × depth
For each of {44.1, 48 kHz} × {16, 24-bit}:
1. Host-select the rate (Clock Source frequency) and format (AudioStreaming alt), stream full-duplex.
2. Run the packet capture; it reports rate, subslot size, USB/audio frame totals, size histogram,
   ZLP + non-nominal counts, effective frames/second.
3. **PASS** when: zero zero-length/short IN packets in steady state, AND the accumulated audio-frame
   count tracks the exact SOF-derived schedule (48 000/1 000 at 48 kHz; **44 100/1 000** at
   44.1 kHz — not merely "packets are 44 or 45").

## Gate B — operator acceptance in Logic (SC-001/003/005/006)
1. In a CoreAudio aggregate (main interface + acfx), route a Logic track out to acfx and back via
   the I/O plugin.
2. Play a sustained tone / software instrument.
3. **PASS** when the return is: **same pitch** as the input (no downward shift), **no digital
   noise**, and **low, usable real-time latency** — at both rates and both bit depths, including a
   **live rate/format change** without a re-plug.

## Gate C — ring & latency measurement (FR-008 / SC-004)
1. With occupancy instrumentation enabled, run the T059 HIL harness across all four combos + a live
   rate/format change.
2. Record and **pin**: ring capacity, startup fill, steady-state occupancy min/max, round-trip
   latency in **frames and ms**, and the device-reported latency figure. Confirm the round-trip is a
   small bounded value (not the prior ~0.5 s / ~24 000 samples).

## Gate D — 24-bit feasibility confirmation (FR-014, before committing 24-bit)
1. After resizing the EP/SW-buf constants for packed-24, re-verify the OTG-FS FIFO-RAM budget
   against the **actual** constants (research §R7: ~20/320 words free at 48 kHz — thin).
2. If it overruns, surface the fallback table to the operator (24-bit-at-44.1-only, or 16-bit-only)
   — do not silently cut.

## Regression guards (host `test` preset, no board)
- packed-24 (3-byte signed LE) wire↔float conversion round-trips within 24-bit resolution.
- format/rate-selection state machine (which converter/rate the recorded selection yields).
- the packet-cadence/accumulator reasoning where host-modellable (the 44 100/1 000 schedule).
- no-allocation guards over the audio path (base T066 sentinel) extended to the 24-bit path.
