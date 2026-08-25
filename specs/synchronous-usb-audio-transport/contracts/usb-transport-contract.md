# Contract: USB-observable transport behaviour

The device's externally-observable USB Audio Class 2.0 contract — what a host (and the objective
packet capture, FR-013) can verify. Descriptor field values are the contract; exact byte layouts
are the implementation's (plan §Project Structure).

## Enumeration / descriptors

| Element | Contract |
|---|---|
| IN endpoint `bmAttributes` | **Synchronous** (`0x0D` = iso `0x01` \| sync `0x0C` \| data `0x00`) |
| OUT endpoint `bmAttributes` | **Synchronous** (`0x0D`) |
| Feedback endpoint | **None** (synchronous device takes none; D20 preserved as an honest declaration) |
| Clock Source | one `INT_VAR_CLK \| CLK_SYC_SOF` entity, referenced by both streams' terminals (single clock domain, FR-016) |
| Clock freq control | **read-write** (`AUDIO20_CTRL_RW`) |
| Clock RANGE (GET) | two subranges: {44 100, 44 100, 44 100} and {48 000, 48 000, 48 000} |
| AudioStreaming alts (each direction) | alt-0 zero-bandwidth · alt-1 16-bit (`bSubslotSize=2`) · alt-2 packed-24 (`bSubslotSize=3`, 24-bit) — alt-2 subject to FR-014 |

## Control requests

| Request | Contract |
|---|---|
| Clock Sampling-Frequency **GET_CUR** | returns `g_currentSampleRateHz` (default 48 000) |
| Clock Sampling-Frequency **GET_RANGE** | returns the two subranges above |
| Clock Sampling-Frequency **SET_CUR** | value ∈ {44 100, 48 000} accepted (strong `tud_audio_set_req_entity_cb`) → triggers the rate-change reaction; other values rejected |
| **SET_INTERFACE** alt (1↔2) | recorded per direction (strong `tud_audio_set_itf_cb`) → selects converter bit depth; alt-0 idles the direction |

## Streaming cadence (the core invariant, FR-002 / SC-002)

| Rate | IN packets/USB-frame | Long-term contract |
|---|---|---|
| 48 kHz | 48 (steady) | **48 000 audio frames per 1 000 USB frames** |
| 44.1 kHz | 44 or 45 on the exact schedule (TinyUSB flow control) | **44 100 audio frames per 1 000 USB frames** (NOT a 44/45 alternation → that would be 44.5 kHz) |

- **No zero-length or systematically short IN packets in steady state.**
- The **accumulated** audio-frame count MUST track the exact SOF-derived schedule over any
  measurement interval — verified by the packet capture, not merely "packets are 44 or 45".

## Packet-capture verification contract (FR-013)

The host-side USB capture MUST report: selected sample rate · selected bit depth / subslot size ·
USB frames observed · total audio frames observed · packet-size histogram · zero-length-packet
count · non-nominal-packet count · effective frames/second. **PASS** = zero ZLP/short packets in
steady state AND accumulated frames match the exact SOF-derived schedule for the selected rate.

## Latency (FR-009, best-effort per §R8)
- If a UAC2 latency mechanism that hosts consume is confirmed, expose the device's
  processing/transport latency through it. `bLockDelayUnits`/`wLockDelay` MUST NOT be repurposed
  (clock-lock delay, not end-to-end latency).
- Host delay-compensation is **verified empirically**, not assumed. The durable contract is:
  minimise the round-trip latency and record the measured value.

## Fault behaviour (FR-017)
- **IN**: device owns cadence → always the scheduled nominal packet.
- **OUT**: host owns contents → a short/ZLP/malformed payload is a bounded, counted
  substitution/truncation; MUST NOT corrupt stereo alignment or perturb the IN cadence.

## Lifecycle (FR-012, preserved from base US10)
- Enumeration, suspend/resume, stream open/close, capture-only continue to work under the new
  synchronous, multi-format transport; counters are not reset by a rate/format change (AR9).
