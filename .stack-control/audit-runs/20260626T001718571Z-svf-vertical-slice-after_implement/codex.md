### Workbench runtime source switching installs an unprepared file transport

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    adapters/workbench/audio-source.cpp:15-36

`useFilePlayer()` replaces `readerSource_` and calls `transport_.setSource()` on lines 15-17, but only `prepare()` calls `transport_.prepareToPlay()` on lines 30-36. The class and comments advertise runtime selection between a looping file player and live input, so the normal “audio device already running, user picks a file” path installs a new transport source after preparation without preparing it.

The blast radius is high because a downstream workbench UI built against this API will naturally call `useFilePlayer()` from the message thread while audio is already active, and the next audio callback will read from a source that was never prepared for the current sample rate/block size. A reasonable fix is to make source changes go through an explicit prepared-state transition: store the current sample rate/block size, prepare the newly installed source when `configured_` is already true, and release/stop the previous source in a defined order.

### Audio-source public switching API is not safe against the audio callback

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   high  
Surface:    adapters/workbench/audio-source.cpp:15-28,45-56

`useFilePlayer()` and `useLiveInput()` mutate `readerSource_`, `transport_`, and `live_` directly on lines 15-28, while `fillBlock()` reads the same state and calls into `transport_` on lines 45-56. There is no synchronization, atomic handoff, device stop, or command queue boundary around those mutations.

The blast radius is high because “selectable at runtime” implies calls from the GUI/message thread while the JUCE audio thread is concurrently in `fillBlock()`. That creates a data race and can invalidate `readerSource_` or change the transport source while audio is rendering. A reasonable fix is to route source changes through the audio device lifecycle or an audio-thread-owned handoff mechanism so the callback observes a consistent source state.
