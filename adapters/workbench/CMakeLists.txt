# The JUCE standalone sketch-and-hear workbench. A thin adapter over the shared
# core: links acfx_core + acfx_host (the same host boundary the plugin uses) and
# adds only JUCE glue. The SAME sources build a workbench for ANY effect that
# satisfies acfx::Effect — the concrete effect type/header are injected by the
# acfx_add_effect_workbench factory via compile definitions (ACFX_EFFECT_TYPE /
# ACFX_EFFECT_HEADER). Built under the `desktop` preset. Add a new effect's
# workbench with one more factory call below.

set(_acfx_workbench_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/workbench-app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/parameter-view.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/audio-source.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/audio-settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source-bar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/workbench-settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/workbench-persistence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/level-meter.cpp
)

# SVF — preserve the existing target name + product exactly (no regression).
acfx_add_effect_workbench(
  NAME acfx_workbench
  EFFECT_TYPE acfx::SvfEffect
  EFFECT_HEADER effects/svf/svf-effect.h
  PRODUCT "acfx Workbench"
  SOURCES ${_acfx_workbench_sources}
)

# Modulated delay — new workbench for the second effect.
acfx_add_effect_workbench(
  NAME acfx_workbench_delay
  EFFECT_TYPE acfx::ModulatedDelayEffect
  EFFECT_HEADER effects/modulated-delay/modulated-delay-effect.h
  PRODUCT "acfx Workbench Delay"
  SOURCES ${_acfx_workbench_sources}
)
