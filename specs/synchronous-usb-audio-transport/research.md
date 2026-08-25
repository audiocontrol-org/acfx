# Research: Synchronous, multi-format USB-audio transport

Phase-0 research for `specs/synchronous-usb-audio-transport/spec.md`. Builds on the base feature's
`specs/nucleo-f446-adapter/research.md` (§R13 UAC2 API, §R14 FIFO-RAM budget, §R5 ring procedure,
§R15 hardware results), which this feature supersedes for D4/D20 (FR-015). All findings are
grounded in the pinned TinyUSB source and the current adapter code; file:line citations below.

## R1 — The four features are ONE coupled change through TinyUSB's rate/format mechanism

**Decision**: Treat sync-declaration + multi-rate + multi-format + nominal-per-SOF as a single
coupled change sequenced through one mechanism, not four independent descriptor edits.

**Rationale**: TinyUSB's UAC2 IN flow-control path derives its per-SOF packet size from
`sample_rate_tx`, `n_bytes_per_sample_tx`, `n_channels_tx`, `interval_tx`. UAC2 carries no sample
rate in descriptors, so TinyUSB learns it by **snooping the Clock Source `SAM_FREQ` control on EP0**
— both the device's GET_CUR response (`audio_device.c:1712-1724`) and a host SET_CUR
(`:1306-1314`) — and takes bytes-per-sample from the selected alt's Type-I `bSubslotSize`
(`:1820`), re-parsed on every SET_INTERFACE (`:1197-1200`). So rate selection, format selection,
and IN pacing all flow through the same driver state.

**Alternatives**: separate/independent edits — rejected: they would desynchronise the driver's
packet math from the advertised rate/format.

## R2 — Declare both isochronous endpoints Synchronous

**Decision**: Set both iso-endpoint `bmAttributes` to **Synchronous** — byte `0x0D` (iso `0x01` |
sync `0x0C` | data `0x00`).

**Rationale**: IN (0x81) is currently **Asynchronous** `0x05` (`usb-descriptors.cpp:287-292`); OUT
(0x01) is **Adaptive** `0x09` (`:245-250`). `SYNCHRONOUS=0x0C` (`common/tusb_types.h:126-130`).
The device needs **nothing beyond the bit + steady per-SOF delivery** — TinyUSB classifies a UAC2
data EP on `bmAttributes.usage` (bits 5:4 = DATA), never on the sync nibble (`audio_device.c:
1177-1179`). The sync bit is a declaration *to the host*; a synchronous IN endpoint takes no
feedback EP (D20 stays — the fix is the honest declaration, not adding feedback).

**Alternatives**: async-with-feedback — rejected (operator decision; a converter-less device has no
independent clock to report).

## R3 — Multi-rate clock source {44 100, 48 000 Hz}, writable

**Decision**: Change the Clock Source to `INT_VAR_CLK` (0x02) keeping `CLK_SYC_SOF`, and make the
Sampling-Frequency Control **read-write** `AUDIO20_CTRL_RW` (0x03). Answer RANGE with two subranges
(`audio20_control_range_4_n_t(2)`, each `bMin==bMax`: {44100},{48000}); CUR returns a new
`g_currentSampleRateHz`.

**Rationale**: current clock is `INT_FIX_CLK | CLK_SYC_SOF`, freq control read-only `AUDIO20_CTRL_R`
(`usb-descriptors.cpp:165-171`), answering a single 48 000 (`usb-audio-controls.cpp:64-89`). A
read-only clock gives the host nothing to select; the driver's SET path that recomputes packet
sizing (`audio_device.c:1306-1317`) is gated on the app's `tud_audio_set_req_entity_cb` returning
true — the weak default stalls it (`:357-363`). So a **writable** control + a strong SET callback
are mandatory for rate selection.

## R4 — Multi-format alt settings: 16-bit + packed-24-bit

**Decision**: Add alt-2 to each AudioStreaming interface — Type-I format `bSubslotSize=3`,
`bBitResolution=24` (packed 24-bit, signed LE) — keeping alt-0 (zero-bandwidth) and alt-1 (16-bit).
Both non-zero alts reference the **same** Clock Source.

**Rationale**: current layout is exactly alt-0/alt-1 with one 16-bit format (`usb-descriptors.h:
106-107`, `usb-descriptors.cpp:237-238,281-282`). Clock association lives on the **terminals**
(`_clkid=kEntityClock`, `usb-descriptors.cpp:178,192,199,209`), not the alt, so both alts share one
clock domain automatically (satisfies FR-016). The driver reads the selected alt's `bSubslotSize`
into `n_bytes_per_sample_tx` (`audio_device.c:1820`), so packet math follows the alt. Needs a
packed-24 converter (§R6) and format-aware OUT/IN paths.

## R5 — Nominal-per-SOF IN delivery via TinyUSB flow control (no SOF-pipeline rewrite)

**Decision**: Set `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1` (`tusb_config.h:254`, currently 0). Keep the
existing free-poll service loop; the app's only job stays "keep the IN FIFO fed"
(`ServiceUsbAudioIn`, `usb-audio-service.h:398-401`) — no feed-logic change.

**Rationale**: with flow control on, the ISR switches from `tu_min16(fifo_count, ep_in_sz)` (the
bursty/ZLP behaviour, `audio_device.c:541-543`) to `audiod_tx_packet_size(...)` (`:538-540`).
**TinyUSB — not the app — does the fractional 44.1k cadence**: `audiod_calc_tx_packet_sz`
(`:1826-1861`) builds size triplets (48k→[47,48,49], 44.1k→[44,44,45]) and `audiod_tx_packet_size`
(`:1863-1896`) is a FIFO-level control loop interleaving 44/45 to hold the exact average. IN pacing
comes from **iso transfer completion** re-arming the endpoint each frame (`:529-556`, from `:1517`);
the SOF ISR is gated on the (absent) feedback EP and is not needed (`:1645-1670,1266-1279`). So the
design's "explicit SOF-paced pipeline" escalation lever is **not required** — a significant de-risk.

**Alternatives**: hand-rolled SOF-driven pipeline — rejected as unnecessary given the above; kept
only as a fallback if hardware capture shows residual jitter under flow control (unlikely).

## R6 — Packed-24 conversion + format-aware paths

**Decision**: Add a packed-24 (3-byte, signed LE) wire↔float path in `support/sample-format.h`
(today 16-bit-only, `:35-80`) and make `usb-out-path.h`/`usb-in-path.h` select the converter by the
recorded format; update the size `static_assert`s.

**Rationale**: base converter is int16-only; §R4 requires both depths. FR-011 (dry-signal
correctness) is unaffected — conversion is transport-layer, not effect-layer.

## R7 — 24-bit feasibility (FR-014 gate): FITS at both rates, TIGHTLY

**Decision**: **Packed-24 at 44.1 AND 48 kHz fits** the STM32F446 OTG-FS 320-word device FIFO RAM
— proceed with full 24-bit scope, with an explicit tight-margin risk recorded.

**Rationale / arithmetic**: the FIFO formula from `dcd_dwc2.c:187-262` reduces to
`Total(words) = 78 + 3·ceil(packet_bytes/4)` (reproduces R14's 225-word 16-bit result as a sanity
check). 48 kHz is the binding rate (44.1k's 45-frame packet is smaller). Packed-24 stereo IN + OUT
+ MIDI + CDC:
- exact nominal (48 frames): **294 / 320 words, 26 free (8.1 %)**;
- with the base feature's +1-frame jitter margin (49 frames): **300 / 320 words, 20 free
  (6.25 %)** — 6 words shy of the hard ceiling.
Full-speed bandwidth (~588 B/ms periodic load vs the 1023 B/packet iso ceiling) is not a
constraint. **BUT** flow control also needs the IN **software** FIFO ≥ 4·Navg (`audio_device.c:
1864-1865`); at 48k/24-bit Navg=288 B → 4× = 1152 B > the current 784 B (`tusb_config.h:219-220`),
and `EP_IN_SZ_MAX`/`kAudioEpSize`/SW-buf sizes grow 196→294 B — all must be resized and the R14
budget re-verified with the resized values.

**Fallback table (operator-owned, FR-014)** — surfaced, not chosen:
| Scope | 48 kHz FIFO headroom |
|---|---|
| 24-bit, both rates (chosen) | ~6.25 % (20/320 words free) |
| 24-bit at 44.1 kHz only | ~12 % |
| 16-bit only | ~29 % |

**Risk**: the 6.25 % headroom is thin; if the resized SW-buf math or a descriptor addition eats the
remaining words, fall back per the table. The plan MUST verify the budget against the *actual*
resized constants, not the estimate, before committing.

## R8 — UAC2 latency reporting (FR-009): mechanism exists in the class, unconsumed in practice

**Decision**: **Minimise and document** the actual round-trip latency; verify host delay
compensation **empirically**; treat the class latency control as optional/cheap-if-added, **not**
load-bearing.

**Rationale**: UAC2 defines an AC-interface-header "Latency Control" `bmControls` bit
(`audio.h:929-932`, routed via `tud_audio_get_req_itf_cb`, `audio_device.c:1400-1420`), but
TinyUSB implements no logic for it and the shipped examples appear to pass the raw bit-position `0`
(disabling it — likely an authoring bug). There is **no evidence** macOS CoreAudio reads a
device-declared processing latency for a USB-audio-class device for PDC (flagged as domain
knowledge, not a cited fact — no spec/web access in research). `bLockDelayUnits`/`wLockDelay`
(`audio.h:1185-1186`) is confirmed clock-recovery lock delay, NOT end-to-end latency. So FR-009's
"expose latency" is best-effort/optional; the durable requirement is minimise + measure + document.

## R9 — Rate/format-change callbacks (STRONG `.cpp` — TASK-37 weak-callback trap)

**Decision**: Add a new strong `tud_audio_set_req_entity_cb` (clock-frequency SET) and extend the
existing strong `tud_audio_set_itf_cb`/`_close_ep_cb` (alt-setting → recorded format). Defer the
app-side reaction (effect `prepare()` at the new rate + ring reset) to a poll-loop service step,
not EP0 context.

**Rationale**: the file today implements clock GET only, no SET (`usb-audio-controls.cpp:42-48`);
the SET callback is absent, so the driver's rate-change path stalls on the weak default. All these
callbacks are the TinyUSB weak-linkage trap ([[tinyusb-weak-callback-linkage-trap]], TASK-37): they
MUST be strong `extern "C"` `.cpp` definitions or they silently no-op on silicon
(`usb-audio-controls.cpp:33-38`). `PrepareEffect()` is today one-shot at 48 kHz
(`nucleo-main.cpp:440`) — it must become re-invokable at the selected rate. The rate-change service
step mirrors `ServiceUsbLifecycle` (`usb-audio-service.h:158-194`).

## R10 — Ring measurement (FR-008, the deferred T062/T063): expect a much smaller ring

**Decision**: Reuse the base feature's R5 measure-don't-pick procedure, but expect a **far smaller**
ring than the 1024-frame/98-frame placeholder (`usb-audio-service.h:60-61,99-100`). Add the
occupancy instrumentation that was missing (the R15 measurement gap), then run the T059 HIL harness
across all four rate×depth combinations plus a live rate/format change and derive
capacity/startup-fill/occupancy-min-max/round-trip-latency (frames + ms). Invent no numbers.

**Rationale**: the old async IN path pulled `min(sink.writeAvailable(), 49)` frames/pass (the source
of R15's benign `outputUnderruns`); the new SOF-exact cadence (FR-002: always the scheduled 48 or
44/45 frames/ms, never room-gated) shrinks the ring's job from absorbing open-ended pull variability
to just the bounded phase misalignment between the fixed 48-frame DSP block and the SOF draw — so a
much smaller cushion suffices, lowering latency (FR-009/SC-004).

## Cross-cutting sequence for implementation

1. Descriptors: sync bits (both EPs `0x0D`), `INT_VAR_CLK` + RW freq control, add alt-2 packed-24.
2. `tusb_config.h`: `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1`; resize EP/SW-buf for 24-bit; then
   **re-verify the R14 FIFO-RAM budget against the resized constants** (the tight 6.25 % margin).
3. Controls: two-subrange RANGE, RW CUR + strong SET callback; strong alt-setting → format record.
4. Conversion: packed-24 wire↔float; format-aware OUT/IN paths + `static_assert`s.
5. Rate/format-change service step: re-`prepare()` the effect + reset rings off the poll loop.
6. Ring occupancy instrumentation + measurement across the 4 combos; pin the values.
7. Host-side USB packet-capture tool (metrics + accumulated-rate tracking) as the objective guard.
