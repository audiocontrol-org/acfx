# Contract — Potentiometer (build-time, `taper.h`)

The pot as build-time math over the frozen vocabulary — **not** a new circuit element (FR-006). Portable, header-only, C++17, standard-library only.

## Shape

```
enum class Taper { Linear, Log };
struct WiperSplit { double rTop; double rBottom; };

WiperSplit wiper(double rTrack, double pos, Taper taper);   // 3-terminal divider
double     rheostat(double rTrack, double pos, Taper taper); // 2-terminal (one leg)
```

## `wiper(...)` contract

- Maps `pos ∈ [0,1]` through the taper law to fraction `f` (`Linear: f=pos`; `Log: f=(pow(10,pos)-1)/9`), then `rBottom = f·rTrack`, `rTop = (1-f)·rTrack` **before flooring**.
- **End-resistance floor (FR-008):** each returned leg `= max(leg, 10.0)`. Both legs `≥ 10 Ω`; near an extreme the sum may exceed `rTrack` by up to 10 Ω (physical series end resistance). A returned leg is **never** `0`.
- **Sum invariant:** away from the extremes (`f·rTrack ≥ 10` and `(1-f)·rTrack ≥ 10`), `rTop + rBottom == rTrack` exactly.
- **`pos = 0.5`, `Linear`:** `rTop == rBottom == rTrack/2` (when `rTrack/2 ≥ 10`).
- **`Log` at a given `pos`:** the split matches the reference exponential fraction at that `pos`.

## `rheostat(...)` contract

- Returns a single floored leg for 2-terminal use (the FMV mid pot): `max(f·rTrack, 10.0)`. Same taper + floor semantics.

## Errors (FR-010) — no fallback

- `pos ∉ [0,1]` → `std::invalid_argument` (no silent clamp).
- `rTrack ≤ 0` → `std::invalid_argument`.
- All throws occur on the build/control thread; there is no audio path.

## What the potentiometer does NOT do

- It does **not** add a `Potentiometer` element to the vocabulary (build-time only; the frozen v1 set is untouched — FR-003).
- It does **not** carry `Antilog` (D10) — the enum has exactly `{ Linear, Log }`.
- It does **not** retain state — a control change is a fresh `wiper()` call inside a rebuild (FR-004).
