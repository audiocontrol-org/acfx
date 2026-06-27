### CI builds the JUCE shared plugin target, not the plugin format products

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    .github/workflows/ci.yml:44-47; adapters/plugin/CMakeLists.txt:6-15; adapters/plugin/CMakeLists.txt:40-44

The workflow labels the step “Build plugin (VST3 / AU / CLAP)” but runs only `cmake --build --preset desktop --target acfx_plugin`. With JUCE CMake, `juce_add_plugin(acfx_plugin ...)` defines the shared plugin target plus format-specific products; building the base target can compile shared code without proving that the VST3/AU bundles, and the CLAP product added by `clap_juce_extensions_plugin`, actually generate. The CMake file itself lists only `FORMATS VST3 AU` on the JUCE target, with CLAP added separately at lines 40-44, so the CI target needs to name the aggregate/product targets that really emit those formats.

Blast radius is high because the feature’s Scenario C claim is that the DAW plugin builds as VST3/AU/CLAP. A downstream consumer could treat this CI job as proof that all plugin artifacts build, while format packaging failures would remain invisible. A reasonable fix is to build the JUCE aggregate target or each generated format target explicitly, including the CLAP target name exposed by `clap-juce-extensions`.

### CI comment contains future-work wording for the absent hardware build gate

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    .github/workflows/ci.yml:3-7

The CI header documents that hardware presets are only build-checked when ARM toolchains are available and points the missing coverage to a later checkpoint. That is exactly the kind of operator-discipline trap this audit prompt forbids: the workflow has no Daisy or Teensy configure/build job, but the comment normalizes the gap instead of expressing a crisp invariant for what CI does and does not validate.

Blast radius is medium because this does not break host CI, but it weakens the acceptance signal for a cross-platform vertical slice. An unattended maintainer or agent could read the workflow as an intentional quality boundary and continue to claim hardware build coverage from portability checks alone. A reasonable fix is either to add explicit Daisy/Teensy build jobs with provisioned toolchains, or to rewrite the comment and README/quickstart claims so CI’s invariant is unambiguous: host tests, portability checks, and desktop build only, with hardware builds requiring a separately named manual command.
