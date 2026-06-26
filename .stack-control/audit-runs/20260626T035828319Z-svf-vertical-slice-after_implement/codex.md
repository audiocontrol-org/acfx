### CI desktop build always requests an AU target on Linux-incompatible hosts

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    `.github/workflows/ci.yml:33-48`, `adapters/plugin/CMakeLists.txt:6-13`

The CI desktop job always builds `acfx_plugin_AU` on `macos-latest`, and the plugin target always declares `FORMATS VST3 AU`. That is coherent for the current runner, but the workflow step is named “Install JUCE Linux/macOS build prerequisites” and the build surface is not actually portable across the implied hosts: AU is macOS-only. If this job is later moved to Linux, matrixed across Linux/macOS, or copied as the desktop verification recipe, the target list fails at configure/build rather than proving the portable VST3/CLAP surface.

Blast radius is high because a downstream CI consumer acting on this as the declared “Desktop workbench + plugin build” gate can produce a broken required gate simply by using the documented Linux/macOS host shape. A reasonable fix is to make plugin formats host-conditional in CMake or split CI targets by OS, with AU built only on Apple runners and VST3/CLAP built where supported.

### README duplicates the ARM toolchain requirement and obscures the actual hardware contract

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   low
Surface:    `README.md:65-78`

The hardware section states the same requirement twice: “Requires an ARM embedded toolchain with the C++ standard library” appears before and after the `daisy` / `teensy` commands. The second copy changes the wording slightly by naming “ARM's gcc-arm-embedded or the vendor toolchain,” but it does not add a separate operational condition.

This is low severity because it is documentation hygiene rather than an implementation break, but it matters in this slice because the README is the primary operator path for validating hardware builds. A cleaner version should state the toolchain requirement once, name the acceptable toolchains there, and keep the flashing/listening boundary separate.
