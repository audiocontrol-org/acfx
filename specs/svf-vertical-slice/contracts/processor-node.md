# Contract — The `ProcessorNode` host boundary (host/processor-node)

Desktop-only. The single place a virtual call touches the audio path — at most once
per block (FR-004 / Constitution VI). **Never compiled into the Daisy/Teensy
builds.**

## Interface

```cpp
struct ProcessorNode {
    virtual ~ProcessorNode() = default;
    virtual void prepare(const ProcessContext& ctx) = 0;
    virtual void processBlock(AudioBlock& io) = 0;          // the one virtual call / block
    virtual void reset() = 0;
    virtual std::span<const ParameterDescriptor> parameters() const = 0;
    virtual void setParameter(ParamId id, float normalized) = 0;
};
```

## Realization (zero hot-path overhead)

```cpp
template <Effect T>
class EffectNode final : public ProcessorNode {
public:
    void prepare(const ProcessContext& ctx) override { fx_.prepare(ctx); }
    void processBlock(AudioBlock& io)       override { fx_.process(io); }   // inlined concrete call
    void reset()                            override { fx_.reset(); }
    std::span<const ParameterDescriptor> parameters() const override { return T::parameters(); }
    void setParameter(ParamId id, float n)  override { fx_.setParameter(id, n); }
private:
    T fx_;   // stored by value — no indirection beyond the single virtual dispatch
};
```

## Guarantees (normative)

- The only virtual dispatch in the audio path is `processBlock` — **once per
  block**, not per sample. Everything inside `fx_.process` is non-virtual and
  inlinable.
- `EffectNode<T>` requires `T` to satisfy the `Effect` concept (compile error
  otherwise on C++20 toolchains).
- Construction/teardown of the node happens off the audio thread; `processBlock`
  itself allocates nothing.

## Consumers

- **workbench** and **plugin** both hold `std::unique_ptr<ProcessorNode>` and share
  this boundary — this is the code they have in common.
- MCU adapters do **not** use `ProcessorNode`; they hold the concrete effect
  directly and call `process` from the target's audio callback.
