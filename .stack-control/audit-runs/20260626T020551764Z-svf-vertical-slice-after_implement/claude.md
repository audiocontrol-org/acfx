I'll audit this chunk of the `svf-vertical-slice` diff directly against the format requested.

### Discrete-parameter mapping is not robust for `discreteCount ≤ 1` — div-by-zero/inf forward, out-of-range select reverse

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:21-33 (forward) and :51-66 (reverse, `setNormalized`)

The forward combo callback computes `norm = (index + 0.5f) / static_cast<float>(count)` where `count = d.discreteCount` (captured `std::uint8_t`). For a descriptor with `discreteCount == 0` the loop adds zero items, `getSelectedItemIndex()` returns `-1`, and the expression evaluates to `-0.5f / 0.0f` → `-inf`, which is then handed to `cb(id, norm)` → `ProcessorNode::setParameter`. The reverse path in `setNormalized` works in a *different* count space: `const int count = discreteCount < 2 ? 2 : discreteCount;` then `index = floor(normalized * count)` clamped only on the high side to `count - 1`. For `discreteCount == 1` the combo holds a single item (index 0 only), but `normalized == 0.5` (the center the forward path emits for that single bucket) yields `index = floor(0.5 * 2) = 1`, which survives the `>= count` (==2) check and calls `setSelectedItemIndex(1)` on a one-item box — an out-of-range selection. There is also no lower-bound clamp, so a negative `normalized` produces a negative index into `setSelectedItemIndex`.

This is the channel-enumeration sibling of the recently-landed "NaN-safe clamp" work (commit 3262fb3): that fix hardened one numeric path while this discrete path still divides by an unguarded count and clamps against a synthetic count rather than the actual item count (`discreteCount`). Blast radius: a downstream effect whose parameter table includes a 0- or 1-value discrete descriptor will receive `±inf`/NaN normalized values at the RT ingress, or silently desync the GUI selection. A reasonable fix: guard the forward divisor (`count >= 1`), and in the reverse path clamp the index against the real item count (`discreteCount`) with both lower and upper bounds, e.g. `index = clamp(floor(normalized * discreteCount), 0, discreteCount - 1)`.

### Teensy/Daisy toolchain comments claim automatic C++-standard detection that isn't implemented; Daisy pins no standard at all

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:5-11,28-32 and cmake/toolchains/daisy.cmake (no `CMAKE_CXX_STANDARD`)

The Teensy header comment states `ACFX_TEENSY_CXX_STANDARD is set to the highest standard that toolchain supports (>= 17)` and the inline comment says `Raised here if the installed toolchain supports more`. The actual code performs no detection: `if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD) set(ACFX_TEENSY_CXX_STANDARD 17)`. Nothing "raises" the value — it is fixed at 17 unless an external `-D` overrides it. Because the core "degrades to a duck-typed template (guarded by `__cpp_concepts`)" when concepts are unavailable, this means the concepts path never engages on Teensy even on a C++20-capable toolchain, contradicting the comment an adopter would rely on.

Compounding this, `daisy.cmake` sets `CMAKE_CXX_STANDARD` *nowhere*, so the Daisy build inherits the `arm-none-eabi-g++` default (gnu++17 on gcc 11+, gnu++14 on older). Whether `__cpp_concepts` is defined — and therefore whether the Effect contract compiles as a concept or a duck-typed template — becomes a function of the uncontrolled installed compiler version. For a feature whose headline claim is "core proven ARM-portable" (commit ae69f91), the portability path actually taken on Daisy is nondeterministic across machines. Fix: implement the claimed probe (or delete the overclaiming comment and state the fixed default honestly), and set an explicit `CMAKE_CXX_STANDARD` in `daisy.cmake` matching the Teensy default so both cross targets resolve the concepts-vs-duck-typed path deterministically.

### ParameterView renders discrete values as bare integers and continuous values as raw normalized numbers, not descriptor names / engineering units

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:18-19 (combo `addItem(juce::String(i), …)`), :36-39 (slider `TextBoxRight` over 0..1 range)

The auto-renderer labels each discrete option with its raw index — `row.combo->addItem(juce::String(i), i + 1)` produces "0", "1", "2"… rather than a semantic name (for an SVF the discrete parameter is filter type, so the operator sees "0/1/2/3" instead of "Lowpass/Highpass/…"). Likewise the continuous slider runs in normalized 0..1 space with a `TextBoxRight`, so its readout shows "0.42", not the engineering unit (Hz, dB, Q) the descriptor maps to. For a "sketch-and-hear workbench" (T022/T026) whose stated value is fast auditioning, surfacing only opaque numbers undercuts SC-006's auto-render intent.

Blast radius is bounded — the controls function, this is a usability shortfall, not a correctness defect — hence low. But it is worth surfacing because it points at a missing piece of the descriptor contract: if `ParameterDescriptor` carries no discrete-value-name table and no unit/formatter, then *every* adapter consuming that table (plugin, hardware) inherits the same opaque presentation, making this a shared-surface gap rather than a workbench-local one. A reasonable fix is to add optional value-name / unit-format data to the descriptor and have the view consume it, falling back to the index/normalized form only when absent.

### `params_` member is assigned but never read (dead state)

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:158 (declaration) and :29 (assignment)

`span<const ParameterDescriptor> params_;` is declared as a member and assigned `params_ = node_->parameters();` in the constructor body, but it is never subsequently read — `paramView_` is constructed directly from `node_->parameters()` and nothing else references `params_`. It is dead state. Beyond the hygiene cost, a retained `span` into `node_`'s storage is a latent footgun: a future edit that reorders or rebuilds `node_` could leave `params_` dangling, and a maintainer might "use" it assuming it is maintained. Blast radius today is nil (it is simply unused), so low; the fix is to delete the member and its assignment.

---

I checked the RT-path channel bounds in `getNextAudioBlock` (`jmin(buffer.getNumChannels(), preparedChannels_)` with `preparedChannels_` jlimited to `kMaxChannels`, so the `std::array<float*, kMaxChannels>` cannot overflow — clean), the member-init ordering (`node_` precedes `paramView_` in both declaration and init-list, so `node_->parameters()` is valid during `paramView_` construction — clean), the MIDI→GUI reflection (SafePointer-guarded `callAsync`, `dontSendNotification` prevents onChange re-fire / double-apply — clean), the `&cb` reference capture (binds to the `onChange_` member referent, which outlives the controls — clean), and `file(DOWNLOAD … EXPECTED_HASH)` in CPM.cmake (skips re-download on hash match, fails loud on mismatch — clean). The four findings above are what survived that pass.