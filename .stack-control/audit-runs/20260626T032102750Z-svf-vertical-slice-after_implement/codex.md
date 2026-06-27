### Live input mode never copies device input into the processed buffer

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    adapters/workbench/workbench-app.cpp:116-123

`getNextAudioBlock()` assumes the `AudioSourceChannelInfo` buffer already contains live input: it constructs a writable output-region wrapper, calls `source_.fillBlock(region)`, and the live-input source path is documented/implemented to pass the block through unchanged. With `juce::AudioAppComponent`, this callback receives an output buffer; input channels are not automatically copied into that buffer for an `AudioSource`-style callback. As written, live-input mode will process silence or whatever the output buffer was initialized with, so the “sketch-and-hear” live path can fail even though the app successfully opened input channels.

Blast radius is high because a user running the workbench without `ACFX_WORKBENCH_FILE` is routed into live input at lines 82-83, but the audio callback never has access to `inputChannelData` to seed the buffer. A reasonable fix is to use an `AudioIODeviceCallback`/custom callback path that receives input pointers and explicitly copies them into the output region before processing, or otherwise use a JUCE abstraction that actually supplies live input samples to the workbench source.

### Discrete controls ignore descriptor defaults

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    adapters/workbench/parameter-view.cpp:16-20

The auto-generated workbench UI initializes every discrete parameter combo to item index `0`, while continuous sliders initialize from `normalize(d, d.defaultValue)` at lines 39-41. That means the UI contract is not truly descriptor-driven for discrete parameters: any future descriptor with a nonzero discrete default will render the wrong state until an external update arrives, and the first user interaction can publish a value from the wrong apparent starting point.

The current SVF mode default happens to be `0`, so this does not break today’s only discrete parameter. The blast radius is medium because `ParameterView` is presented as generic descriptor-driven infrastructure, and a downstream adopter adding another effect or changing the mode default would get a plausible but incorrect UI without touching this file. The fix should derive the selected combo index from the descriptor default, clamped to `[0, discreteCount - 1]`, matching the continuous branch’s use of descriptor metadata.
