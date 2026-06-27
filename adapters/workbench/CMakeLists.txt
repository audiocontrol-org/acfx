# The JUCE standalone sketch-and-hear workbench (T021). A thin adapter over the
# shared core: it links acfx_core + acfx_host (the same host boundary the plugin
# uses) and adds only JUCE glue. Built under the `desktop` preset.

juce_add_gui_app(acfx_workbench
  PRODUCT_NAME "acfx Workbench"
  COMPANY_NAME "acfx"
  # Live input is a core workbench use (filter the live device input). On macOS an app
  # that opens an audio input MUST declare NSMicrophoneUsageDescription or the OS
  # silently denies/zeros the input (TCC). Enabling this adds that Info.plist key so
  # macOS prompts for — and can grant — audio-input access.
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "acfx Workbench needs audio-input access to filter your live input device."
)

target_sources(acfx_workbench PRIVATE
  workbench-app.cpp
  parameter-view.cpp
  audio-source.cpp
  audio-settings.cpp
  source-bar.cpp
  workbench-settings.cpp
  workbench-persistence.cpp
  level-meter.cpp
)

target_compile_features(acfx_workbench PRIVATE cxx_std_20)

target_compile_definitions(acfx_workbench PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_APPLICATION_NAME_STRING="acfx Workbench"
)

target_link_libraries(acfx_workbench PRIVATE
  acfx_core
  acfx_host
  juce::juce_audio_utils      # pulls audio_basics/devices/formats/processors + gui
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)
