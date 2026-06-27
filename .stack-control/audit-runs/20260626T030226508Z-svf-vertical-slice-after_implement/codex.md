### Host target cannot be added when desktop and tests are enabled together

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    host/processor-node/CMakeLists.txt:5-6

`host/processor-node/CMakeLists.txt` unconditionally creates `acfx_host` and `acfx::host`. In the integrated tree, the top-level build adds this subdirectory for both `ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS`, so a consumer configuring both options in one build will hit a CMake target redefinition instead of getting a desktop build with tests.

The blast radius is a configure-time failure for a natural build-option composition, not an audio/runtime defect, so I rate it `medium`. A reasonable fix is to make the host target definition idempotent, for example guarding it with `if(NOT TARGET acfx_host)`, or ensuring the top-level only adds the host subdirectory once before desktop/test consumers link it.

### Portability gate misses common uppercase JUCE includes in core

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:28-34

The core platform-header gate uses case-sensitive `grep -rEn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' core/`. That catches `juce::` and lowercase module includes, but it does not catch a common JUCE umbrella include such as `#include <JuceHeader.h>` because `grep` is case-sensitive by default and the pattern has only lowercase `juce`.

The blast radius is validator drift: an unattended consumer can trust a green “core/ is platform-independent” report while a JUCE core dependency has actually slipped in. A reasonable fix is to make the JUCE check case-insensitive or explicitly include `JuceHeader` / `JUCE` spellings in the forbidden pattern, ideally with a fixture that proves uppercase includes fail the gate.
