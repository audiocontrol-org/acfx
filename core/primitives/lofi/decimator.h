#pragma once
namespace acfx {
class SampleHoldDecimator {
public:
    void setDivisor(int d) noexcept { divisor_ = (d < 1) ? 1 : d; phase_ = 0; }
    int  divisor() const noexcept { return divisor_; }
    bool isTick() noexcept {
        const bool tick = (phase_ == 0);
        phase_ = (phase_ + 1) % divisor_;
        return tick;
    }
private:
    int divisor_ = 1;
    int phase_   = 0;   // 0 on the next tick (reset makes the next call a tick)
};
} // namespace acfx
