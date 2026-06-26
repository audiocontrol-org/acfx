### Non-lock-free float atomics leak into the embedded audio/control path

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    core/effects/svf/svf-effect.h:83-89, core/effects/svf/svf-effect.h:107-123, core/effects/svf/svf-effect.h:167-169

`SvfEffect` makes the cross-thread parameter handoff depend on `std::atomic<float>` for normalized values, and those atomics are used by both `setParameter()` and `process()` via `applyPending()`. The surrounding comments claim this is the core RT-safe boundary for UI/MIDI/MCU callbacks and the audio thread, but the code never proves that `std::atomic<float>` is lock-free on the target toolchains. On Cortex-M/embedded standard libraries this can degrade to compiler-runtime atomic calls or a locking/critical-section implementation, and in some configurations it can fail at link time if the atomic runtime is not present.

The blast radius is high because this surface is the shared core path for Daisy/Teensy and desktop adapters: a downstream adopter can correctly call `setParameter()` from a control callback and still end up with a non-RT-safe or non-portable primitive inside the feature’s advertised RT-safe handoff. A reasonable fix would store the normalized value in a lock-free integer representation, for example fixed-point `std::atomic<std::uint32_t>` with explicit encode/decode, and add compile-time checks such as `static_assert(decltype(pendingNorm_)::value_type::is_always_lock_free)` or equivalent target-specific portability tests.
