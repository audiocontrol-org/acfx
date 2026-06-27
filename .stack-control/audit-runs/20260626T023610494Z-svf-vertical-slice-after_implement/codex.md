### Workbench file length overflows before validation

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:19-24

`reader->lengthInSamples` is a 64-bit value, but the code casts it to `int` before validating it: `const int numSamples = static_cast<int>(reader->lengthInSamples);`. A valid long audio file whose sample count exceeds `INT_MAX` can wrap or truncate, after which the code either rejects it as “empty” or allocates/reads the wrong length.

The blast radius is high because this is a user-facing workbench source path: an adopter can hit it with a real long recording, and the failure mode is misleading or incorrect playback rather than a clear “file too large” diagnostic. A reasonable fix is to validate `reader->lengthInSamples` in its original integer width first, reject values larger than `std::numeric_limits<int>::max()` with a descriptive `AudioSourceError`, and only then cast for `AudioBuffer`.

### Workbench ignores decoder read failure

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:24-31

`reader->read(&decoded, 0, numSamples, 0, true, true);` returns a success/failure value, but the result is ignored. If decoding fails or only partially succeeds after the reader was created, `useFilePlayer()` still publishes `fileBuffer_`, flips `hasFile_` to true, and the audio thread loops whatever data made it into the buffer.

The blast radius is medium: this does not break the common happy path, but it turns a source setup failure into a silent or corrupted playback path, which conflicts with the adapter’s stated “descriptive error, never silent zeros or mock audio” contract in `audio-source.h`. A reasonable fix is to check the read result and throw `AudioSourceError` before assigning `fileBuffer_` or publishing `hasFile_`.
