---
id: TASK-27
title: allocation-sentinel-has-no-positive-control
status: To Do
assignee: []
created_date: '2026-08-23 17:53'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - tests/support/allocation-sentinel.cpp
ordinal: 27000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The host suite's no-allocation guarantees all rest on acfx::test::AllocationSentinel, which replaces global operator new/new[] and counts heap traffic in thread_local counters. Every assertion across the four no-allocation suites (core, compressor, program-dependent-saturation, tape-dynamics, and now nucleo sample-format) is of the form allocations() == 0. No test anywhere asserts allocations() > 0. That means the entire family passes vacuously if the overrides ever stop being invoked - a link-order change that drops support/allocation-sentinel.cpp from the target, a toolchain that elides the replacement, or an allocator path that bypasses global operator new. The suite would stay green while proving nothing. Verified by hand on 2026-08-23 that the sentinel currently DOES fire (a std::vector reserve took the counter 0 -> 1, pure stack work left it at 0), so the mechanism is sound today; the gap is that nothing keeps it sound. Fix: add one positive-control test asserting the counter increments on a deliberate heap allocation, so a broken sentinel fails loudly instead of silently blessing every audio-path claim. Same family as TASK-25: a check that cannot fail gets believed.
<!-- SECTION:DESCRIPTION:END -->
