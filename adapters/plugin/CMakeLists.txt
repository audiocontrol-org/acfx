# The DAW plugin: one JUCE plugin target per effect, exported as VST3 + AU
# natively and CLAP via clap-juce-extensions. A thin adapter — links acfx_core +
# acfx_host (the same boundary the workbench uses) and adds only JUCE plugin glue.
# The SAME sources build a plugin for ANY effect that satisfies acfx::Effect — the
# concrete effect type/header are injected by the acfx_add_effect_plugin factory
# via compile definitions (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER). Each effect
# needs a UNIQUE PLUGIN_CODE, CLAP_ID, and PRODUCT_NAME (JUCE requires unique
# codes). Built under the `desktop` preset. Add a new effect's plugin with one
# more factory call below.

set(_acfx_plugin_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin-processor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/plugin-parameters.cpp
)

# SVF — preserve the existing target name, product, code, and CLAP id exactly.
acfx_add_effect_plugin(
  NAME acfx_plugin
  EFFECT_TYPE acfx::SvfEffect
  EFFECT_HEADER effects/svf/svf-effect.h
  PRODUCT "acfx SVF"
  PLUGIN_CODE Asvf
  CLAP_ID org.acfx.svf
  CLAP_FEATURES audio-effect filter
  SOURCES ${_acfx_plugin_sources}
)

# Modulated delay — new plugin for the second effect (unique code + CLAP id).
acfx_add_effect_plugin(
  NAME acfx_plugin_delay
  EFFECT_TYPE acfx::ModulatedDelayEffect
  EFFECT_HEADER effects/modulated-delay/modulated-delay-effect.h
  PRODUCT "acfx Modulated Delay"
  PLUGIN_CODE Amdl
  CLAP_ID org.acfx.modulated-delay
  CLAP_FEATURES audio-effect delay
  SOURCES ${_acfx_plugin_sources}
)
