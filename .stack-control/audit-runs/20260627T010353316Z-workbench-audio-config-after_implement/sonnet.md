### CI visibility improvement is comments-only; behavioral gap formalized rather than closed

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:37-45

The `desktop-build` comment added at lines 38-40 explicitly states that "device/source/MIDI selection + persistence surfaces" are "otherwise interactive manual acceptance." This is honest documentation of a gap, but the framing formalizes the gap rather than closing it. The CI as written will report green even if every behavioral surface of the feature (audio device selection, source selection, MIDI input selection, persistence round-trip) is broken — provided the code compiles. A breaking change to `WorkbenchSettings`, `AudioSettings`, or the persistence layer will pass CI undetected as long as it does not cause a compile error.

The serde test (mentioned in the `core-tests` comment at line 17-18) provides some coverage of the serialization contract, but none of the selection logic, none of the restore-on-startup path, and none of the MIDI enumeration path are exercised in CI. Given that the commit range includes T009/T010 (in-UI source selection), T012/T013 (persist + restore), and T015 (MIDI input selection), these are the high-value surfaces added by the feature — and they have zero automated coverage beyond compile-verify.

A reasonable path forward: extract the persistence round-trip and source-list population logic into JUCE-free testable units (or use `juce::UnitTest` headlessly), then wire those into the `core-tests` or a new headless workbench test binary. The comment should track this as a known constraint with a backlog reference, not leave it implicit.

---

### `core-tests` comment claims SourceConfig test links into `acfx_core_tests` — unverifiable from this chunk

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:17-18

The comment added at lines 17-18 asserts that the SourceConfig serde test "links into acfx_core_tests." This claim is only correct if `tests/CMakeLists.txt` (visible in chunk `cc36a7e4cc6d3feb`) actually registers the test source under that target. The CI comment cannot be verified from this chunk alone, and if the test target was named differently or was not wired up, the comment would silently misrepresent CI coverage — precisely the kind of documentation drift the audit framework flags.

Blast-radius: low because the comment's inaccuracy would mislead a reader about test coverage scope, not cause a runtime failure. However, if an operator reads this comment and concludes the serde contract is covered, they may deprioritize adding coverage elsewhere. The operator should cross-reference against `tests/CMakeLists.txt` to confirm the target name matches.