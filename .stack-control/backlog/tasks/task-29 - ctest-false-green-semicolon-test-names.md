---
id: TASK-29
title: ctest-false-green-semicolon-test-names
status: To Do
assignee: []
created_date: '2026-08-23 20:27'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - 'tests/CMakeLists.txt:287'
ordinal: 29000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
ctest --preset test reports 828/828 PASSED while the acfx_core_tests binary run directly reports 3 failed cases / 4 failed assertions. The green signal the project trusts is not measuring what it appears to measure.

Mechanism, confirmed end to end. tests/CMakeLists.txt line 287 uses doctest_discover_tests(acfx_core_tests), which registers one ctest entry per doctest TEST_CASE and invokes the binary with --test-case=<name>. CMake treats the semicolon as a LIST SEPARATOR, so any test name containing one is truncated at the semicolon when the entry is registered. The truncated filter then matches nothing in doctest (which needs an exact match absent wildcards), so the binary runs ZERO cases, exits 0, and ctest records Passed.

Worked example, verified with ctest -V:
  Test command: acfx_core_tests "--test-case=CompressorCore - sidechain HPF at 120 Hz yields substantially less gain reduction for a below-cutoff tone than an above-cutoff tone at the same level"
  [doctest] test cases: 0 | 0 passed | 0 failed | 799 skipped
  1/1 Test #106: ... Passed
The real name in tests/core/compressor-sidechain-test.cpp:233 ends "...at the same level; 0 Hz restores full-band detection (SC-006)" -- everything from the semicolon on is lost.

Blast radius: 28 test cases in the current suite have a semicolon in their name, so 28 ctest entries execute nothing while reporting Passed. Three of them are ACTUALLY FAILING right now and have been invisible: compressor-sidechain-test.cpp:259, :277, :306 and program-dependent-saturation-presets-test.cpp:229. These are pre-existing and unrelated to the nucleo work (verified: a control build at 5165b4e, before any ring source existed, reproduces the identical 3 failures / 4 assertions; audio-ring.h is included by nothing outside the three ring test files; both failing files were last touched by the program-dependent-saturation feature).

Note the two registration branches are mutually exclusive: the else branch at line 289 registers the whole binary as a single ctest entry, which WOULD have caught these, but it only fires when doctest's CMake helper is absent. The better discovery mechanism is what created the blind spot.

This is worse than a flake: a flake is visible. Here the suite affirmatively reports success for tests it never ran, which is the failure mode the repo's no-silent-fallbacks stance exists to prevent. It also means any past "suite green" claim resting on ctest alone was weaker than it read.

Candidate fixes: escape or bracket the semicolon when doctest_discover_tests builds the filter (CMake list-escaping of the discovered name); or forbid semicolons in TEST_CASE names via a lint; or additionally register the whole-binary entry alongside per-case discovery so a truncation can never yield a false green. Prefer a fix that FAILS LOUD on a filter matching zero cases -- doctest exiting 0 after running nothing should never read as success. Until fixed, treat the direct binary run, not ctest, as the suite's verdict.

Surfaced during T022 of specs/nucleo-f446-adapter (the ring implementation), whose own 38 cases / 252 assertions do run and pass under both ctest and the direct binary.
<!-- SECTION:DESCRIPTION:END -->
