---
id: TASK-37
title: tinyusb-weak-callback-must-be-strong
status: To Do
assignee: []
created_date: '2026-08-24 06:47'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 37000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
TinyUSB application callbacks (tud_audio_set_itf_cb, tud_audio_set_itf_close_ep_cb, tud_audio_*_cb, tud_descriptor_*_cb) are TU_ATTR_WEAK with permissive defaults. An 'extern C inline' definition in a header emits a weak COMDAT symbol the linker may resolve to the weak DEFAULT rather than the app definition -- links clean, host tests pass (they call the pure logic directly), and the callback is a silent no-op on silicon (objdump showed 'movs r0,#1; bx lr'). Found in T047 (US7 capture-only), fixed by moving to strong .cpp defs. Add a build/link guard: a post-link nm/objdump assertion that each overridden tud_* callback resolves into an adapter TU, or a lint flagging 'inline' on a tud_*_cb definition. usb-audio-controls.cpp already documents the trap for the clock-source callbacks; this generalizes it to a checkable gate. Affects every future hardware target using TinyUSB callbacks.
<!-- SECTION:DESCRIPTION:END -->
