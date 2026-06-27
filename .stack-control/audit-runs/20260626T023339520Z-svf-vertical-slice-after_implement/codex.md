### MCU dependency gate misses the actual host target

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:36-56

The MCU dependency check only greps Daisy/Teensy adapters for lowercase `juce` or `processor-node`, then separately verifies that each adapter links `acfx_core`. That does not enforce the stated dependency invariant. An MCU adapter could link `acfx_host` / `acfx::host` in its CMake file while still linking `acfx_core`, and this gate would pass because `acfx_host` contains neither `juce` nor `processor-node`.

This matters because `host/processor-node/CMakeLists.txt` explicitly defines the forbidden desktop host boundary as `acfx_host`, while the portability gate is supposed to prove “No JUCE / ProcessorNode in the MCU dependency surface.” A downstream consumer relying on this CI gate could accidentally pull the desktop host boundary into Daisy/Teensy builds without the advertised failure. A reasonable fix is to also scan MCU `CMakeLists.txt` for `acfx_host` / `acfx::host`, or better, query CMake target link interfaces for `acfx_daisy` and `acfx_teensy` and fail if host targets appear.

### Per-target fork gate does not catch all preprocessor forks

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:44-50

The one-source-many-targets gate only searches `core/effects/` for `#if` / `#ifdef` lines containing target tokens. It misses target-specific forks expressed on adjacent preprocessor branches, such as `#elif defined(TEENSY)` after a generic `#if`, because the regex does not include `#elif`.

The blast radius is medium: the feature’s portability claim depends on this script as an explicit CI gate, and an unattended maintainer could introduce a real per-target effect fork that passes the advertised check. The fix should enumerate all conditional forms that can carry the target split, at minimum `#if`, `#ifdef`, `#ifndef`, and `#elif`, with tests or fixture files proving each forbidden channel fails.
