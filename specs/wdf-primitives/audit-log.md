---
slug: wdf-primitives
targetVersion: ""
---

# Audit log — wdf-primitives

## 2026-07-10 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260710-01 — `is_one_port` accepts an `isAdaptable` that isn't a compile-time constant, so a malformed leaf passes the trait yet breaks the `if constexpr` dispatch the concept exists to enable

Finding-ID: AUDIT-20260710-01 (claude-02 + codex-01; cross-model)
Status:     resolved — fixed in ff19fb8: `is_one_port` now requires `std::bool_constant<T::isAdaptable>` (forces a compile-time-constant `isAdaptable`), with regression fixtures rejecting a non-static and a non-constexpr `isAdaptable`; all 8 leaves still satisfy the trait. Re-govern converged (graduated), no re-surface.
Severity:   high
Per-lane:   claude=low, codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    core/primitives/circuit/wdf/one-port.h:73 (`isAdaptable` check in `is_one_port`)

The `isAdaptable` requirement is checked only as `std::is_convertible_v<decltype(T::isAdaptable), bool>`. `decltype(T::isAdaptable)` is well-formed in an unevaluated context even for a *non-static* data member, and `is_convertible` says nothing about `constexpr`-ness. So a leaf declaring `bool isAdaptable = true;` (non-static) or a non-`constexpr` static passes `is_one_port_v<T>` and satisfies the `static_assert`. But the entire point of `isAdaptable` (documented I2, "Call-ordering contract, selected by isAdaptable") is that sibling adaptor code branches on it at compile time via `if constexpr (T::isAdaptable)`, which *requires* a compile-time constant. Such a malformed leaf would compile-error only later, at the adaptor site, after the trait and its `static_assert` have already vouched for it.

Blast radius: bounded — every leaf in this chunk correctly declares `static constexpr bool isAdaptable`, so nothing ships broken today. The risk is future: the trait and its per-leaf `static_assert` give false confidence to a later leaf author, deferring the real error to a confusing failure in unrelated adaptor code. Tightening the check to force a constant-expression evaluation — e.g. requiring `std::bool_constant<T::isAdaptable>` to be well-formed, or `noexcept`/`is_same_v<..., const bool>` plus a `constexpr` probe — would keep the rejection local to the leaf, consistent with the header's own stated goal of making "present-but-wrong member" a hard `false` rather than a deferred failure.
