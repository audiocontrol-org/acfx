### CPM bootstrap writes into a directory it never creates

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    cmake/CPM.cmake:9-23

`CPM_DOWNLOAD_LOCATION` is always placed under a subdirectory, either `${CPM_SOURCE_CACHE}/cpm/...` or `${CMAKE_BINARY_DIR}/cmake/...`, but the bootstrap never creates that parent directory before `file(DOWNLOAD ...)`. On a clean checkout, the default cache from the top-level build is `external/.cpm-cache`, so the destination becomes `external/.cpm-cache/cpm/CPM_0.40.5.cmake`; neither `.cpm-cache` nor `cpm` is guaranteed to exist. CMake’s download step will then fail before dependencies can be configured.

The blast radius is high because a downstream adopter running the documented fresh configure path can hit a hard configure failure before any feature target builds. A reasonable fix is to compute the parent directory with `get_filename_component(... DIRECTORY)` and call `file(MAKE_DIRECTORY ...)` before `file(DOWNLOAD ...)`, covering both the repo cache and binary-dir fallback paths.

### Queued MIDI UI callback can outlive the workbench component

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:137-143

`handleIncomingMidiMessage()` schedules `juce::MessageManager::callAsync([this, id, norm] { paramView_.setNormalized(id, norm); });`. That lambda captures a raw `this` and can run after `WorkbenchComponent` destruction; the destructor removes the MIDI callback and shuts down audio, but it does not cancel already queued message-thread work. Closing the app while MIDI traffic is arriving can therefore dereference a destroyed component or `paramView_`.

The blast radius is high because this is a concrete lifetime bug in an interactive adapter path, not just cosmetic UI drift. A reasonable fix is to use JUCE’s safe weak-reference pattern for components, or otherwise gate the async callback on component liveness before touching `paramView_`.
