---
doc-grammar: design-inbox
---

# Design Inbox

A governed, low-friction parking lot for out-of-sequence design ideas. Capture
and triage with `stackctl inbox` (`capture` / `promote` / `drop` / `list`) —
do not hand-edit.

### cv-output
- **Surfaced:** Operator request while waiting on a CD40106 for the analog LFO alternative; design questions settled in-session (control-only, single channel, built-in DAC).
- **Context:** PA5/DAC2 is unavailable: it is the NUCLEO-F446RE's LD2, which the existing acfx firmware uses for fault indication, so PA4/DAC1 is the only free channel. Hardware constraint recorded on the pedals side: the DAC drives a series resistor into the PT2399 VCO pin, whose operating voltage is NOT manufacturer-specified and is currently unmeasured, with contradictory evidence between roughly 2.5 V and roughly 4 V. With a 3.3 V DAC the injected current is strongly asymmetric at best and entirely one-directional at worst, shortening delay only. Firmware must not assume a symmetric swing and the usable output range must be a declared parameter rather than hardcoded. Likely roadmap parent: multi:feature/hardware-targets. Hardware interface recorded at pedals repo docs/superpowers/specs/2026-09-09-delay-module-pcb-design.md.
- **Idea:** Control-voltage output so acfx can modulate an external analog effect. First and only target use: driving a PT2399 analog delay's VCO pin from a NUCLEO-F446RE. Control-only, so audio stays entirely analog outside the MCU and never enters the audio path; a firmware fault cannot interrupt sound. Scope as agreed with the operator and NOT to be narrowed on YAGNI grounds per Constitution Principle V: a platform-independent modulation source in core/ that is host-testable per Principle X; a thin CV output service in adapters/nucleo/ driving DAC1 on PA4; timer-driven DAC updates fast enough for audio-rate FM, not only sub-audio LFO rates; rate, depth, waveshape and offset declared through the existing parameter model and reachable over the existing MIDI CC map; waveshapes to include sample-and-hold and stepped random, not only periodic shapes.
- **Provisional home:** specs/cv-output (to be authored via the speckit chain)
- **Status:** **captured**