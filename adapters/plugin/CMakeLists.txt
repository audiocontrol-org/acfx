# The DAW plugin (T028): one JUCE plugin target exported as VST3 + AU natively and
# CLAP via clap-juce-extensions. A thin adapter — links acfx_core + acfx_host (the
# same boundary the workbench uses) and adds only JUCE plugin glue. Built under the
# `desktop` preset.

juce_add_plugin(acfx_plugin
  PRODUCT_NAME "acfx SVF"
  COMPANY_NAME "acfx"
  PLUGIN_MANUFACTURER_CODE Acfx
  PLUGIN_CODE Asvf
  FORMATS VST3 AU
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(acfx_plugin PRIVATE
  plugin-processor.cpp
  plugin-parameters.cpp
)

target_compile_features(acfx_plugin PRIVATE cxx_std_20)

target_compile_definitions(acfx_plugin PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(acfx_plugin PRIVATE
  acfx_core
  acfx_host
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)

# Add the CLAP format to the same target (research.md decision 7).
clap_juce_extensions_plugin(TARGET acfx_plugin
  CLAP_ID "org.acfx.svf"
  CLAP_FEATURES audio-effect filter
)
