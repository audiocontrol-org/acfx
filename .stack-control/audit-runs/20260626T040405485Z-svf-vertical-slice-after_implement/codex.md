### Workbench accepts failed file decodes as usable source audio

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:36-42

`useFilePlayer()` validates that a reader exists, allocates the decoded buffer, and then calls `reader->read(&decoded, 0, numSamples, 0, true, true)` without checking the returned success value. If JUCE can open the file but fails to decode the requested sample range, the code still moves the buffer into `fileBuffer_`, sets `hasFile_` true, and marks live input false. That conflicts with the documented contract in `adapters/workbench/audio-source.h:11-13` that unavailable source audio raises a descriptive error rather than falling back to silence or mock audio.

The blast radius is medium: normal valid files work, but a corrupt/truncated/unsupported-in-practice file can be treated as a valid deterministic player source and then emit zeroed or partially initialized decoded audio through the workbench. A reasonable fix is to check the boolean result of `reader->read(...)` and throw `AudioSourceError` with the file path when the requested decode fails, before publishing `fileBuffer_` / `hasFile_`.
