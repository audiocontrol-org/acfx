# Native host asset-tool (svf-training-site feature, T008-T010). Sweeps the
# REAL acfx::SvfEffect (never faked/mock DSP -- Principle VII) to produce the
# SVF lesson's static assets (audio/response/impulse) + static.fragment.json.
# Gated by ACFX_BUILD_LESSON_ASSETS (root CMakeLists.txt); host-only, like the
# acfx_lab_* harnesses -- never built under MCU (daisy/teensy) toolchains.

add_executable(acfx_lesson_assets_tool
  asset-tool-main.cpp
  svf-asset-sweep.cpp
  synthetic-source.cpp
  wav-writer.cpp
  fragment-writer.cpp
  sha256.cpp
)
target_link_libraries(acfx_lesson_assets_tool PRIVATE acfx_core)
target_compile_features(acfx_lesson_assets_tool PRIVATE cxx_std_20)
