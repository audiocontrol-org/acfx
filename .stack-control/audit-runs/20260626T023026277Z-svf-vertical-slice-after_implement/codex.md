### README still tells users to build the aggregate plugin target, not the actual plugin formats

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    README.md:58-63; .github/workflows/ci.yml:46-49; adapters/plugin/CMakeLists.txt:6-16

The README's Scenario C command still says `cmake --build --preset desktop --target acfx_plugin` for “Desktop plugin (VST3 / AU / CLAP)” at README.md:58-63. In the same diff, CI was corrected to build `acfx_plugin_VST3`, `acfx_plugin_AU`, and `acfx_plugin_CLAP`, with a comment explicitly noting that the aggregate `acfx_plugin` target builds only shared code. The plugin CMake target also declares VST3/AU formats through `juce_add_plugin`, with CLAP added separately, so the format wrapper targets are the surface a user needs for actual plugin bundles.

Blast radius is high because a downstream adopter following the documented validation path will run the wrong command and can incorrectly believe Scenario C was built. A reasonable correction is to make the README match CI: build the explicit wrapper targets, or document both the shared target and the bundle-producing targets with clear semantics.

### CI comment contains a prohibited postponement marker

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:3-7

The CI header comment includes postponement wording at line 6 while describing hardware preset coverage. That is exactly the kind of operator-discipline trap this audit prompt bans: it records an incomplete quality boundary in prose instead of making the invariant mechanically checkable or naming the current invariant plainly.

Blast radius is medium because it does not change build behavior directly, but it weakens the governance artifact unattended agents will read when deciding whether hardware build coverage is complete. The comment should state the present invariant without postponement language, for example that CI currently covers host tests, portability checks, and desktop bundles, while hardware presets require explicitly provisioned ARM toolchains.
