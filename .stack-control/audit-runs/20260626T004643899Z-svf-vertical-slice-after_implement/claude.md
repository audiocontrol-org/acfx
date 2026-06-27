I'll audit this chunk (the test sources + CMake test wiring for `svf-vertical-slice`). I walked each diff line by line; here are my findings.

### Discrete-parameter test cannot distinguish two materially different quantization schemes

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:52-65 (the `discreteMode` descriptor + "discrete mapping quantizes to buckets" / round-trip cases)

The discrete descriptor is `discreteMode{ParamId{2}, "mode", …, 0.0f, 2.0f, 0.0f, …, ParamKind::discrete, 3}` and the assertions are `denormalize(0.0)=0`, `denormalize(0.5)=1`, `denormalize(0.99)=2`, `denormalize(1.0)=2`. Every one of those points is satisfied **both** by a `min(floor(n·count), count-1)` scheme **and** by a `round(n·(count-1))` nearest-bucket scheme: n=0→0/0, n=0.5→floor(1.5)=1 / round(1.0)=1, n=0.99→floor(2.97)=2 / round(1.98)=2, n=1.0→clamp(3)=2 / round(2)=2. The two schemes diverge at points the test never probes — e.g. n=0.33 gives bucket **0** under floor but bucket **1** under round. The round-trip loop only feeds back the exact bucket centers the forward map produced, so it also passes under either scheme.

Blast radius: this is the file a downstream agent (or a future refactor of `parameter.h`, which lives in another chunk and is the actual source of truth) would read to understand discrete-mode semantics, and it gives false confidence that the quantization rule is pinned. A scheme switch in the implementation would silently change which DSP mode a knob selects at intermediate positions while this test stays green. A reasonable fix adds at least one assertion in the divergence region (e.g. `denormalize(discreteMode, 0.33f) == Approx(<expected bucket>)`) so the intended scheme is actually constrained.

### Discrete test conflates bucket index with mapped value — min/max range never exercised

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:55-65

`discreteMode` is deliberately constructed with `min=0.0, max=2.0, count=3`, so bucket index and mapped value coincide (index 0/1/2 == value 0.0/1.0/2.0). Because of that coincidence the test cannot tell whether `denormalize` returns a raw bucket *index* or a *value* scaled into `[min,max]`. The case header even calls this "round-trips **by index**," and the round-trip uses `static_cast<int>(denormalize(...)) == idx`, baking in the index reading. If a discrete parameter were declared with `min=10, max=20, count=3` (expected denormalized values 10/15/20), a regression that returned the bare index would pass none of the real contract yet nothing here would catch it.

Blast radius is limited to test confidence rather than runtime behavior, hence low — but it's the same class of "test doesn't test the contract it claims" as finding-01. A fix is to add a second discrete descriptor whose `[min,max]` is offset/scaled away from `[0,count-1]` so index-vs-value is observable.

### tasks.md is listed in chunk scope but no diff was provided — surface is unauditable in this chunk

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   informational
Surface:    specs/svf-vertical-slice/tasks.md (declared in "Files in scope" header for chunk b74f59c0c4fc198b)

The chunk header declares four files in scope — `specs/svf-vertical-slice/tasks.md`, `tests/CMakeLists.txt`, `tests/core/no-allocation-test.cpp`, `tests/core/parameter-test.cpp` — but the Diffs section only renders the three test/CMake files. The `tasks.md` diff is absent, so I cannot verify that the task ledger (T014/T018 status, acceptance wording, the honest verified/manual split referenced by commit `f88525b`) actually matches the tests landed here. Per the audit instruction to "call out a missing surface that should be in the diff but isn't," I'm flagging that this chunk's task-doc claims went un-reviewed in this lane. No defect asserted — operator should confirm `tasks.md` was covered in another chunk or re-dispatch it; if it was meant to be here, the chunking dropped it.

---

I checked the rest of the chunk and found it clean for these reasons. **`tests/CMakeLists.txt`**: the source list, `tests/`-as-include-root, the `acfx_core`/`acfx_host`/`doctest` link set (host is genuinely needed for `EffectNode` in the no-alloc test), and the `doctest_discover_tests`-else-`add_test` fallback are all consistent and correctly gated on the module's existence. **`no-allocation-test.cpp`**: discipline is correct — `prepare()` and the backing `std::vector`s are constructed *before* `reset()` so setup allocation is excluded; the count is read into a local *before* any `CHECK_MESSAGE` (whose failure formatting could allocate); the sentinel is thread-local and `process()`/`setParameter()` run on the same thread; both the bare effect and the host-boundary node are covered. The variadic `CHECK_MESSAGE(cond, "…", var, …)` form is valid doctest. **`parameter-test.cpp`**: the linear and logarithmic cases are mathematically sound — geometric midpoint `sqrt(20·20000)≈632.46`, exact round-trips with adequate epsilon, and both-ends clamping are all correctly asserted. My findings are confined to the discrete-mapping case's under-constrained contract; I would have flagged an actual off-by-one in the `floor`/clamp comments or a wrong geometric-mean expectation had either been present, and neither was.