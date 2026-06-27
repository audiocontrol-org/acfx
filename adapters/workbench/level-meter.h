#pragma once

#include <atomic>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

// A simple input/output peak meter for the workbench. The audio thread feeds it the
// per-block peak via lock-free atomic stores (RT-safe — no allocation, no lock, no
// audio data crosses the boundary, only a scalar peak per block). A message-thread
// Timer reads those atomics to repaint the bars and, ~once per second, invokes onLog so
// the workbench can write the levels to its log. This is what makes "is the workbench
// actually receiving audio?" visible — on screen and in the log.

namespace acfx::workbench {

class LevelMeter final : public juce::Component, private juce::Timer {
public:
    LevelMeter();
    ~LevelMeter() override;

    // AUDIO THREAD: store the latest block peaks (lock-free; RT-safe).
    void pushPeaks(float inputPeak, float outputPeak) noexcept {
        inPeak_.store(inputPeak, std::memory_order_relaxed);
        outPeak_.store(outputPeak, std::memory_order_relaxed);
    }

    // MESSAGE THREAD: invoked ~1/sec with the current (input, output) peaks for logging.
    std::function<void(float inputPeak, float outputPeak)> onLog;

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    static float toBarFraction(float peak); // linear peak -> 0..1 across a -60..0 dB span

    std::atomic<float> inPeak_{0.0f};
    std::atomic<float> outPeak_{0.0f};
    float inDisplay_{0.0f};
    float outDisplay_{0.0f};
    int logDivider_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace acfx::workbench
