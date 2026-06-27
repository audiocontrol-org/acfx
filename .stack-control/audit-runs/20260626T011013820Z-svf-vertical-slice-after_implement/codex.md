### Workbench parameter edits bypass the claimed RT-safe handoff

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:26-35

The file-level contract says GUI/MIDI parameter edits are “handed to the audio thread through a small lock-free queue so process() stays RT-safe,” but the GUI callback wired into `ParameterView` calls `node_->setParameter(id, norm)` directly from the message thread. If `ProcessorNode::setParameter` is anything other than a trivial atomic store for every current and future effect, this creates a cross-thread mutation path racing the audio callback; even if today’s SVF happens to use atomic pending state, the adapter boundary is documenting and implementing different contracts.

Blast radius is high because downstream adapter authors can copy this workbench pattern as the host boundary and assume the queue exists. A reasonable fix is to either implement the described queue for both GUI and MIDI paths, or update the boundary so `ProcessorNode::setParameter` is explicitly the only RT-safe cross-thread ingress and enforce that contract in the processor/effect interface.

### Discrete controls can divide by zero for malformed descriptors

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:15-30

For `ParamKind::discrete`, the constructor accepts `d.discreteCount` without validating it. If a descriptor arrives with `discreteCount == 0`, the combo has no items, `setSelectedItemIndex(0)` cannot select a valid item, and the `onChange` callback computes `(index + 0.5f) / count`, dividing by zero. `setNormalized()` defensively clamps discrete counts below 2, but the constructor’s callback path does not.

The blast radius is medium: the current SVF descriptors may be well-formed, but this auto-renderer is a shared descriptor-driven surface and a future discrete parameter can turn a descriptor bug into undefined UI/control behavior. A reasonable fix is to reject or normalize invalid discrete descriptors at construction, ideally using the same descriptor invariant enforced by core parameter validation.
