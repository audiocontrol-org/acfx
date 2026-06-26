### Primitive reset drops the active filter coefficients

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:25-36

`SvfPrimitive::reset()` calls `svf_.Init(sampleRate_)`, but the primitive does not store the last frequency or resonance values set through `setFreq()` and `setRes()`. DaisySP `Init()` clears state and also reinitializes filter parameters, so a direct primitive consumer that calls `reset()` after configuring cutoff/resonance gets a different filter until it manually reapplies both values.

`SvfEffect::reset()` currently masks this by calling `applyAll()` after resetting each channel, but the primitive is a public composable core surface and its own comment says reset returns a “cleared-but-prepared state,” not “cleared with default coefficients.” The blast radius is medium: the vertical slice may pass through the effect wrapper, but future primitive users can silently get wrong audio after reset. A reasonable fix is for the primitive to cache `freq` and `res`, then reapply them after `Init()`.

### Portability gate misses target forks written as `#ifndef`

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:44-50

The portability gate claims to reject “per-target `#ifdef` forks inside the effect source,” but the grep only matches `#if` and `#ifdef` forms: `#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)`. It does not catch `#ifndef TEENSY`, `#ifndef DAISY`, or similar inverse target forks, even though those are the same portability violation.

The blast radius is medium because this is a quality gate: downstream consumers may treat a passing gate as evidence that one-source-many-targets is enforced, while a common preprocessor spelling still slips through. A reasonable fix is to match all preprocessor conditional forms, including `#ifndef`, `#elif`, and `defined(...)` spellings for the target symbols the gate intends to ban.
