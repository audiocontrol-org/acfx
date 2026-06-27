### Portability gate can pass when an adapter no longer links `acfx_core`

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:51-53

The one-source-many-targets gate checks adapter linkage with `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"`. That searches comments as well as commands, so it can pass even when the actual `target_link_libraries(... acfx_core ...)` edge has been removed. This is already structurally fragile because the workbench/plugin CMake files describe `acfx_core` in comments, so a downstream edit could break the build contract while the portability gate still reports success.

Blast radius is medium: the shipped code can still build today, but this validator is meant to enforce SC-001/SC-005. A consumer relying on it as a CI quality gate would get a false green on a core architectural invariant. A reasonable fix is to query CMake target link properties in a configure-time check, or at minimum match non-comment `target_link_libraries` lines for each concrete adapter target.

### MCU dependency-surface gate misses common JUCE and ProcessorNode spellings

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:36-38

The MCU gate claims to reject “JUCE / ProcessorNode” references, but it only greps for lowercase `juce` and hyphenated `processor-node`. That misses common C++/CMake spellings such as `JUCE::juce_audio_utils`, `JUCE_DECLARE...`, or the `ProcessorNode` type name. The current desktop surfaces use exactly those uppercase forms elsewhere, so this is not a theoretical spelling concern.

Blast radius is medium: this is a validator false negative for SC-007 rather than an immediate runtime bug. If an MCU adapter accidentally gained `JUCE::...` linkage or a `ProcessorNode` symbol reference, the gate could pass as written. A reasonable fix is to make the match case-insensitive and cover both symbol and include-path forms, while avoiding comment-only matches so the existing explanatory MCU comments do not become false positives.
