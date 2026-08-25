---
id: TASK-27
title: allocation-sentinel-has-no-positive-control
status: Done
assignee: []
created_date: '2026-08-23 17:53'
updated_date: '2026-08-25 02:07'
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

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Closed: Closed by T066: a file-scoped positive control (tests/core/nucleo-fr046a-region-no-alloc-test.cpp, deliberate new/delete) plus the pre-existing one in nucleo-audio-ring-no-allocation-test.cpp both independently prove AllocationSentinel trips; RED-confirmed by temporarily neutering the operator-new override (all allocations()==0 assertions stayed falsely green, the positive control alone caught it), then reverted and re-verified GREEN at 962/962.
<!-- SECTION:NOTES:END -->
