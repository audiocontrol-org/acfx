---
id: TASK-32
title: audioring-ar6-enforcement-by-convention
status: To Do
assignee: []
created_date: '2026-08-24 01:32'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 32000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
AudioRing AR6 fail-loud is enforced only by unenforced call-site convention on firmware. Before T032, an out-of-range startupFillFrames on an embedded toolchain was a COMPILE error (unguarded throw under -fno-exceptions, set at cmake/toolchains/nucleo-f446.cmake:63 and the daisy/teensy siblings, both line 63). T032 guarded the throw on __cpp_exceptions and made the exception-free branch __builtin_trap(), which is a HardFault during __libc_init_array() - before the fault LED and before USB, i.e. an apparently bricked board with no diagnostic. The mitigating static_asserts live in a DIFFERENT file (adapters/nucleo/usb-audio-service.h:61-65) and the class comment only says callers "are expected to" write them. Failure scenario: T035 adds an output ring in another TU and copies the AudioRing instantiation without the two static_asserts; a later edit to its capacity constant then bricks the board silently instead of failing the build. startupFill is deliberately a runtime argument (contract: host tests sweep it), so a static_assert inside the class cannot fix it - a MakeAudioRing<Capacity, Fill>() factory or equivalent would restore structural enforcement. Not a regression of shipped behaviour (nothing instantiated a ring before T032). Surfaced by independent review of T032 (commit 35e2bea). Related: the same -fno-exceptions guard will be needed for the daisy and teensy adapters if they ever instantiate a ring.
<!-- SECTION:DESCRIPTION:END -->
