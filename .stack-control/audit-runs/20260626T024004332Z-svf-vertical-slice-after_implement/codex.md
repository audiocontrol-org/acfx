### C++17 effect contract does not enforce the declared signatures

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    core/dsp/effect.h:42-63

The C++17 branch says “Template instantiation still enforces the real member signatures,” but the implementation does not actually enforce the return types. `is_effect` only checks that expressions are well-formed via `decltype(...)`, and `ACFX_EFFECT_CONCEPT` becomes plain `typename`, so a type with `int prepare(...)`, `bool process(...)`, or another discardable return type can still pass through templates that call those methods and ignore the return value. That diverges from the C++20 concept at lines 19-26, which requires `std::same_as<void>` and `std::convertible_to<span<const ParameterDescriptor>>`.

The blast radius is medium: the shipped `SvfEffect` satisfies the intended shape, but the contract surface is what downstream effects and adapters will copy. On Teensy/C++17, an unattended adopter can believe the same compile-time contract exists while accidentally accepting effects that would be rejected on desktop C++20. A reasonable fix is to make the C++17 trait check return types with `std::is_same` / `std::is_convertible`, and either use a `static_assert(is_effect_v<T>)` in the C++17 wrapper boundary or provide a helper that templates instantiate deliberately.
