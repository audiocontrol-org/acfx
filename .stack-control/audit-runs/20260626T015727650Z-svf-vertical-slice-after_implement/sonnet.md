### `#ifndef` and `#elif` variants escape the per-target ifdef gate

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:43

The regex on line 43 is `#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)`. The optional group `(def)?` extends `#if` to also match `#ifdef`, but it does not match `#ifndef`, `#elif`, `#elifdef`, or `#elifndef`. A developer writing `#ifndef __arm__` or `#elif defined(JUCE)` inside `core/effects/` would pass this check while introducing exactly the per-target fork the gate is designed to prevent. Because the check's stated purpose is "no per-target #ifdef forks of the effect," missing two common conditional forms (`#ifndef` and `#elif`) leaves the invariant enforceable by bypass via name alone. A reasonable fix is to broaden the pattern to catch the full family: `'#(if(n?def)?|elif).*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)'`.

---

### EffectNode no-allocation test never exercises setParameter through the host boundary

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:44–58

The first test case (`SvfEffect::process`) explicitly calls `fx.setParameter(...)` inside the measured region (line 36) and confirms parameter changes on the audio thread are allocation-free. The second test case (`EffectNode<SvfEffect>::processBlock`, lines 44–58) calls only `node.processBlock(block)` and never calls `node.setParameter()` during the measured window. If `EffectNode` (the host boundary) allocates for parameter routing — for example, pushing to a lock-free queue backed by a dynamically-sized ring buffer, or any lazy initialization triggered by the first setParameter call — that allocation would go undetected. The two test cases are asymmetric on a surface that the prior audit rounds explicitly hardened. Fixing this is straightforward: add `node.setParameter(ParamId{SvfEffect::kCutoff}, ...)` inside the `EffectNode` iteration loop, mirroring the first test case.

---

### EffectNode test hardcodes one block size and one channel count

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:45–46

The bare `SvfEffect` test iterates over four block sizes (16, 64, 256, 512) to catch any block-size-dependent allocation path. The `EffectNode` test hardcodes `blockSize = 256` and two channels. If the host boundary has any channel-count-dependent or block-size-dependent allocation (e.g., resizing an internal staging buffer on first call or on size change), it would not be detected. Given that channel consistency was a prior-round finding and the first test case varies block sizes precisely to catch these paths, the omission in the second test is an asymmetry worth closing.

---

### `pipefail` absent; an unreadable source file silently passes the line-count budget

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:13,19–22

The script opens with `set -u` but omits `set -o pipefail`. At line 19, `lines=$(wc -l < "$f" | tr -d ' ')` runs as a pipeline. If `wc -l` fails (e.g., a file found by `find` is no longer readable by the time the loop body runs, a race during a build), `tr` still succeeds on empty input, and `$lines` is set to the empty string. The subsequent test `[ "$lines" -gt 500 ]` evaluates the empty string as an integer, producing a bash error ("integer expression expected") with exit code 2; the `if` branch treats exit code ≠ 0 as false, so no FAIL is recorded and the file is silently treated as within budget. Adding `set -o pipefail` at line 13 (alongside `set -u`) would cause the pipeline to propagate the non-zero exit from `wc` and expose the failure rather than masking it.