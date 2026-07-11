# Host-side doctest suite for the platform-independent core (Constitution VIII,
# FR-013). Builds one binary from all tests/core/* sources + the allocation
# sentinel, links the core spine + the host boundary, and registers with ctest so
# `ctest --preset test` runs everything.

add_executable(acfx_core_tests
  core/test-main.cpp
  core/parameter-test.cpp
  core/svf-test.cpp
  core/measurement-stimulus-test.cpp
  core/measurement-response-test.cpp
  core/measurement-distortion-test.cpp
  core/measurement-stability-test.cpp
  core/measurement-report-test.cpp
  core/delay-line-test.cpp
  core/lfo-test.cpp
  core/modulated-delay-test.cpp
  core/modulated-delay-us2-test.cpp
  core/modulated-delay-wowflutter-test.cpp
  core/no-allocation-test.cpp
  core/workbench-settings-test.cpp
  # The SourceConfig serde seam is JUCE-free (std::string only), so its TU links
  # into this JUCE-free test target directly (contracts/source-config.md).
  "${CMAKE_SOURCE_DIR}/adapters/workbench/workbench-settings.cpp"
  support/allocation-sentinel.cpp
  # ---------------------------------------------------------------------------
  # Waveshaper test suites (waveshapers feature): the six suites below are the
  # actual authored set (T007/T008 wrapper+harmonics, T012 shapes, T019
  # antiderivatives, T015 LUT, T018 ADAA).
  # ---------------------------------------------------------------------------
  core/waveshaper-test.cpp
  core/waveshaper-harmonics-test.cpp
  core/waveshaper-shapes-test.cpp
  core/waveshaper-antiderivatives-test.cpp
  core/waveshaper-lut-test.cpp
  core/waveshaper-adaa-test.cpp
  # ---------------------------------------------------------------------------
  # Saturation test suites (saturation feature): appended by their authoring
  # tasks (T007 core, T011 voicings, T014 effect, T008 harmonics, T017
  # aliasing). Each task adds its own core/saturation-*-test.cpp line below.
  # ---------------------------------------------------------------------------
  core/saturation-core-test.cpp
  core/saturation-harmonics-test.cpp
  core/saturation-voicings-test.cpp
  core/saturation-effect-test.cpp
  core/saturation-effect-rt-test.cpp
  core/saturation-aliasing-test.cpp
  # ---------------------------------------------------------------------------
  # Oversampling primitive test suites (oversampling feature): appended by their
  # authoring tasks. T007 transparency, T008 aliasing (+ T012 response, T015
  # latency added by their tasks).
  # ---------------------------------------------------------------------------
  core/oversampler-transparency-test.cpp
  core/oversampler-aliasing-test.cpp
  core/oversampler-response-test.cpp
  core/oversampler-latency-test.cpp
  # ---------------------------------------------------------------------------
  # harmonic-analysis feature: host-only analysis engine suites (host/analysis/).
  # T003 FFT (windowed radix-2 tone reconstruction; non-pow2 rejection; FR-009/026).
  # T005 window (default Blackman-Harris; Hann/flat-top selectable; FR-025).
  # T008 spectrum (full harmonic magnitude+phase; out-of-band/sub-floor NaN; FR-001/008).
  # T010 thdn (THD+N residual method + noise-floor/SNR; no-fundamental NaN; FR-002/008).
  # T012 goertzel parity (retained exact known-bin Goertzel path; FR-007/010).
  # T014 imd (twin-tone SMPTE+CCIF difference/sum products; FR-003, US2).
  # T016 alias-sweep (inharmonic-energy-vs-frequency sweep; FR-004, US2).
  # T018 drive-series (drive->THD + drive->per-harmonic curves; FR-005, US3).
  # ---------------------------------------------------------------------------
  core/analysis-fft-test.cpp
  core/analysis-window-test.cpp
  core/analysis-spectrum-test.cpp
  core/analysis-thdn-test.cpp
  core/analysis-goertzel-parity-test.cpp
  core/analysis-imd-test.cpp
  core/analysis-alias-sweep-test.cpp
  core/analysis-drive-series-test.cpp
  # T025 capture-probe (portable-core lock-free SPSC RT capture ring;
  # overrun/underrun observable; FR-011/012/013, US5). The ONLY audio-path unit
  # in this feature -- lives in core/primitives/analysis/capture-probe.h.
  core/capture-probe-test.cpp
  # T028 live-vs-offline parity (the ONE-ENGINE guarantee; FR-014/015, SC-005,
  # US5): asserts a metric measured live (through the RT capture probe,
  # drained + analyzed by the shared host/analysis/live-readout.h) equals the
  # same metric measured offline (the engine called directly) within a named
  # tolerance. T029 lands the shared LiveReadout that makes this GREEN.
  core/analysis-live-offline-parity-test.cpp
  # ---------------------------------------------------------------------------
  # Envelope-follower primitive test suites (envelope-followers feature):
  # appended by their authoring tasks. T009 interface/peak, T010 ballistics
  # timing, T015 rms, T022 peak-hold, T025 db-domain, plus the low-sample-rate
  # characterization split out of the ballistics suite for the file-size budget
  # (FR-022) — six files total.
  # ---------------------------------------------------------------------------
  core/envelope-follower-test.cpp
  core/envelope-follower-ballistics-test.cpp
  core/envelope-follower-lowfs-test.cpp
  core/envelope-follower-rms-test.cpp
  core/envelope-follower-hold-test.cpp
  core/envelope-follower-db-test.cpp
  # ---------------------------------------------------------------------------
  # Compressor test suites (compressors feature): T006–T013 authoring tasks.
  # T006 setup: stubs to keep build green during development.
  # ---------------------------------------------------------------------------
  core/gain-computer-test.cpp
  core/compressor-test.cpp
  core/compressor-topology-test.cpp
  core/compressor-sidechain-test.cpp
  core/compressor-lookahead-test.cpp
  core/compressor-makeup-link-test.cpp
  core/compressor-effect-test.cpp
  core/compressor-no-allocation-test.cpp
  # ---------------------------------------------------------------------------
  # Program-dependent-saturation test suites (program-dependent-saturation
  # feature): T006 registration; story tasks fill assertions.
  # ---------------------------------------------------------------------------
  core/dynamics-modulator-test.cpp
  core/program-dependent-saturation-orthogonality-test.cpp
  core/program-dependent-saturation-test.cpp
  core/program-dependent-saturation-matrix-test.cpp
  core/program-dependent-saturation-topology-test.cpp
  core/program-dependent-saturation-presets-test.cpp
  core/program-dependent-saturation-sidechain-test.cpp
  core/program-dependent-saturation-effect-test.cpp
  # T037 -- the ProgramDependentSaturationEffect::process() no-allocation
  # invariant (SC-013), split out of no-allocation-test.cpp for the module
  # budget (mirrors compressor-no-allocation-test.cpp).
  core/program-dependent-saturation-no-allocation-test.cpp
  # T038 -- SC-014 numerical-safety battery (no NaN/Inf + bounded output for
  # pathological stimuli x configs; modulated params stay in SaturationCore's
  # valid range via the FR-010 clamps).
  core/program-dependent-saturation-safety-test.cpp
  # Review round 2 -- compile-time guard that the compressor and PDS effect
  # headers coexist in one TU (no acfx-scope enum redefinition, e.g. Detection).
  core/pds-compressor-coexist-test.cpp
  # ---------------------------------------------------------------------------
  # Tape-dynamics test suites (tape-dynamics feature): T003 registration;
  # story tasks fill assertions.
  # ---------------------------------------------------------------------------
  core/hysteresis-test.cpp
  # T006-T009 solver/guard suites, split out of hysteresis-test.cpp to stay
  # under the Constitution VII per-file line budget (shares
  # hysteresis-test-support.h's friend accessor + helpers).
  core/hysteresis-solver-test.cpp
  core/tape-dynamics-effect-test.cpp
  core/tape-dynamics-alias-test.cpp
  # T015 focused suite: TapeDynamicsCore<Factor> composed OS+JA signal path
  # (finite output, drive nonlinearity, mix/bypass, JA loop memory, reset
  # determinism). Kept separate from tape-dynamics-effect-test.cpp's T017
  # placeholder.
  core/tape-dynamics-core-test.cpp
  # T019 -- solver-selection agreement + convergence-under-oversampling
  # (US3, SC-002, contract C4), exercised through TapeDynamicsCore<Factor> so
  # the OVERSAMPLED integrator dt is genuinely on the test's axis.
  core/tape-dynamics-solver-test.cpp
  # T021 -- US4/SC-003/FR-012 emergent-compression acceptance: monotonic
  # compressive level curve + DRR-rises-with-drive at trim OFF, plus a
  # compile-time guard that no compression/ratio/threshold parameter exists.
  # Split out of tape-dynamics-effect-test.cpp to stay under the per-file
  # line budget.
  core/tape-dynamics-compression-test.cpp
  # T023 -- the TapeDynamicsEffect::process() no-allocation / RT-safety
  # invariant (US5, SC-007), split out per Constitution VII (mirrors
  # compressor-no-allocation-test.cpp / program-dependent-saturation-
  # no-allocation-test.cpp).
  core/tape-dynamics-no-allocation-test.cpp
  # T027 -- US6/contract E7 trim acceptance suite: trim-disabled bit-exact
  # (core AND effect black-box), trim-enabled gain reduction scaling with
  # trim.amount, and attack/release ballistics timing. New file (T026's
  # tape-dynamics-core-test.cpp addition is one focused check, not the full
  # suite) to stay under the per-file line budget.
  core/tape-dynamics-trim-test.cpp
  # ---------------------------------------------------------------------------
  # Component-abstractions test suites (component-abstractions feature): T002
  # registration; story tasks fill assertions.
  # ---------------------------------------------------------------------------
  core/circuit-components-test.cpp
  core/circuit-netlist-test.cpp
  core/circuit-solver-test.cpp
  # ---------------------------------------------------------------------------
  # Passive-tone-stacks test suites (passive-tone-stacks feature): registered
  # per phase as each file lands. Tier-1 (no solver): taper + builder topology.
  # Tier-2 (lab AC solver): FMV/Baxandall analytic match.
  # ---------------------------------------------------------------------------
  core/tone-stack-taper-test.cpp
  core/tone-stack-builder-test.cpp
  core/tone-stack-ac-test.cpp
  # ---------------------------------------------------------------------------
  # Diode-clippers test suites (diode-clippers feature): T002 registration;
  # story tasks fill assertions. Tier-1 (no solver): builder topology / counts /
  # frozen vocabulary. Tier-2 (lab transient solver): RC recurrence + DC-limit
  # oracle + assembled invariants + non-convergence.
  # ---------------------------------------------------------------------------
  core/diode-clipper-builder-test.cpp
  core/diode-clipper-transient-test.cpp
  # US3 assembled-clipper invariants, split from diode-clipper-transient-test.cpp
  # to stay under the Constitution VII per-file line budget (shared drive/oracle
  # rig in core/diode-clipper-test-support.h).
  core/diode-clipper-invariants-test.cpp
  # ---------------------------------------------------------------------------
  # Opamp-stages test suites (opamp-stages feature): T002 registration; story
  # tasks fill assertions. Tier-1 (no solver): builder topology / counts / frozen
  # vocabulary. Tier-2 (lab nullor MNA augmentation): circuit validations.
  # ---------------------------------------------------------------------------
  core/opamp-stage-builder-test.cpp
  core/opamp-stage-solve-test.cpp
  # US3 assembled-stage invariants, split from opamp-stage-solve-test.cpp to
  # stay under the Constitution VII per-file line budget (shared sine-drive /
  # DC-settle / energy rig in core/opamp-stage-test-support.h).
  core/opamp-stage-invariants-test.cpp
  # ---------------------------------------------------------------------------
  # Modified Nodal Analysis (MNA) primitive test suites
  # ---------------------------------------------------------------------------
  core/mna-system-test.cpp
  core/mna-assembler-test.cpp
  core/mna-assembler-companions-test.cpp
  core/mna-assembler-rtsafety-test.cpp
  core/mna-invariants-test.cpp
  core/mna-equivalence-test.cpp
  # ---------------------------------------------------------------------------
  # Newton-Raphson iteration primitive test suites (newton-iteration feature)
  # ---------------------------------------------------------------------------
  core/newton-solver-test.cpp
  core/newton-composition-test.cpp
  core/newton-rtsafety-test.cpp
  core/newton-closed-form-test.cpp
  core/newton-invariants-test.cpp
  core/newton-equivalence-test.cpp
  core/newton-nofallback-test.cpp
  core/newton-global-step-test.cpp
  core/newton-plan-drift-test.cpp
  # ---------------------------------------------------------------------------
  # Implicit-integration primitive test suites (implicit-integration feature)
  # ---------------------------------------------------------------------------
  core/integration-integrator-test.cpp
  # RT-safety (T017), no-fallback/rule-fidelity (T019), and plan-drift/
  # accessor guard cases split from integration-integrator-test.cpp to stay
  # under the Constitution VII per-file line budget.
  core/integration-rtsafety-test.cpp
  core/integration-nofallback-test.cpp
  core/integration-guards-test.cpp
  core/integration-closed-form-test.cpp
  core/integration-convergence-order-test.cpp
  core/integration-invariants-test.cpp
  core/integration-composition-test.cpp
  core/integration-equivalence-test.cpp
  # ---------------------------------------------------------------------------
  # WDF primitive test suites (wdf-primitives feature): T002 registration;
  # story tasks fill assertions.
  # ---------------------------------------------------------------------------
  core/wdf-port-interface-test.cpp
  core/wdf-resistor-test.cpp
  core/wdf-reactive-test.cpp
  core/wdf-sources-test.cpp
  core/wdf-terminations-test.cpp
  core/wdf-rt-safety-test.cpp
  core/wdf-passivity-test.cpp
  # WDF adaptor test suites (wdf-adaptors feature): T001 registration
  core/wdf-series-adaptor-test.cpp
  core/wdf-parallel-adaptor-test.cpp
  core/wdf-adaptor-tree-test.cpp
  core/wdf-adaptor-reflection-free-test.cpp
  core/wdf-adaptor-passivity-test.cpp
  core/wdf-adaptor-rt-safety-test.cpp
  core/wdf-adaptor-child-access-test.cpp
)

# tests/ as an include root so test files write #include "support/...".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# adapters/workbench on the include path so the serde test (and its TU) resolve
# #include "workbench-settings.h".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/adapters/workbench")
target_link_libraries(acfx_core_tests PRIVATE acfx_core acfx_host acfx_analysis doctest::doctest)
target_compile_features(acfx_core_tests PRIVATE cxx_std_20)

# Per-test-case ctest registration when doctest ships its CMake helper; otherwise
# register the whole binary as a single test (still satisfies the test preset).
set(_doctest_module "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
if(EXISTS "${_doctest_module}")
  include("${_doctest_module}")
  doctest_discover_tests(acfx_core_tests)
else()
  add_test(NAME acfx_core_tests COMMAND acfx_core_tests)
endif()
