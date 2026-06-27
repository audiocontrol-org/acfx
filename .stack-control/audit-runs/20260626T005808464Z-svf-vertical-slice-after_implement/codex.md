### Portability gate misses common uppercase JUCE/ProcessorNode references

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:24-35

The new portability gate uses case-sensitive grep patterns: `juce|libDaisy|daisy_seed|...` for `core/` and `juce|processor-node` for MCU adapters. That misses common actual include/symbol spellings such as `#include <JuceHeader.h>`, `JUCE_*`, `juce::`, and `ProcessorNode`. A downstream consumer relying on `scripts/check-portability.sh` as the explicit SC-007/Constitution IV gate can get a false green result while desktop/JUCE dependencies have leaked into surfaces the script claims to protect.

Blast radius is medium: this does not directly break runtime behavior, but it undermines the advertised governance gate and can let portability regressions ship unnoticed. A reasonable fix is to make the checks case-insensitive or enumerate the real spellings used by the codebase, and include a negative fixture or test script case proving uppercase `JuceHeader` / `ProcessorNode` references fail the gate.
