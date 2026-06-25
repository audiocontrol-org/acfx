# acfx_host — the desktop-only host boundary (ProcessorNode + EffectNode<T>).
# Header-only; pure C++ (no JUCE). Shared by the workbench + plugin adapters and
# exercised by the host-side tests. NEVER linked into the Daisy/Teensy builds.

add_library(acfx_host INTERFACE)
add_library(acfx::host ALIAS acfx_host)

# Expose host/ as an include root so consumers write
# #include "processor-node/processor-node.h".
target_include_directories(acfx_host INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(acfx_host INTERFACE acfx_core)
