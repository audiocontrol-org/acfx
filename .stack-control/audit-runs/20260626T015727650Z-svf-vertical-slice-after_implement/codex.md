### Portability gate only scans `core/effects`, leaving adjacent core implementation forks unchecked

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   medium  
Surface:    scripts/check-portability.sh:44-45

The gate advertises “One-source-many-targets” and “no per-target `#ifdef` forks of the effect,” but the actual check only greps `core/effects/`. In this slice, effect implementation is split across `core/effects/svf/svf-effect.h` and the primitive layer named in the file list, `core/primitives/svf-primitive.h`; a platform fork placed in that primitive would pass this CI gate while still changing the shared DSP implementation per target.

Blast radius is medium because downstream consumers can treat a green portability gate as proof of the one-source invariant, while nearby core DSP files remain outside the mechanism. The reasonable fix is to scan the complete platform-independent DSP/effect surface that participates in the SVF implementation, or define and enforce a narrower invariant that deliberately excludes primitives.

### Host-boundary no-allocation test skips the host-boundary parameter ingress path

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    tests/core/no-allocation-test.cpp:42-55

The test comment says the allocation sentinel covers “both the bare `SvfEffect` and the `EffectNode<SvfEffect>` host boundary,” and the bare-effect case exercises `fx.setParameter(...)` inside the measured region at lines 29-35. The host-boundary case only calls `node.processBlock(block)` at lines 51-55; it never calls `node.setParameter(...)` under the sentinel, even though `ProcessorNode::setParameter` is the documented cross-thread control ingress for the host boundary.

Blast radius is medium because CI can report the FR-014 host-boundary invariant as covered while leaving the host parameter path unmeasured. A reasonable fix is to include representative `node.setParameter(...)` calls inside the measured loop, matching the bare-effect test’s audio-thread/control-ingress coverage.
