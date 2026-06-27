### Portability gate misses common platform-header spellings

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:28-39

The new portability gate claims to enforce “No platform headers in core/” and “No JUCE / ProcessorNode in MCU adapters,” but both checks are case-sensitive text scans: `juce|libDaisy|daisy_seed|...` and `juce|processor-node`. That misses common real include/type forms such as `#include <JuceHeader.h>` or direct `ProcessorNode` references, so the gate can report a clean pass while the forbidden dependency is present.

Blast radius is medium: this is not an immediate runtime bug, but it weakens a stated CI/operator safety gate. A reasonable fix is to make the scan case-insensitive or enumerate the actual forbidden include/type spellings, ideally matching includes/imports rather than only lowercase substrings.

### Adapter core-link check can be satisfied by comments

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:51-55

The “every adapter links the same acfx_core” gate only runs `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"`. That proves the token appears somewhere in the file, not that any adapter target actually links `acfx_core`; the existing adapter CMake files already contain `acfx_core` in comments as well as link statements, so deleting the real `target_link_libraries(... acfx_core ...)` line while leaving the comment would still pass.

Blast radius is medium because this is a structural validation false positive in a quality gate for SC-001/SC-005. A reasonable fix is to parse or narrowly match `target_link_libraries` blocks for each expected target, or move the invariant into CMake target introspection so comments cannot satisfy it.
