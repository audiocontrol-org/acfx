#include <juce_audio_basics/juce_audio_basics.h> // juce::Decibels

#include "level-meter.h"

// Implementation of the input/output peak meter (see level-meter.h). All audio-thread
// interaction is confined to the atomic stores in pushPeaks(); everything here runs on
// the message thread.

namespace acfx::workbench {

namespace {
constexpr int kTimerHz = 15;
constexpr int kLogEveryTicks = kTimerHz; // ~once per second
constexpr float kDecay = 0.7f;           // peak-hold decay between ticks
constexpr float kFloorDb = -60.0f;
} // namespace

LevelMeter::LevelMeter() { startTimerHz(kTimerHz); }

LevelMeter::~LevelMeter() { stopTimer(); }

void LevelMeter::timerCallback() {
    const float in = inPeak_.load(std::memory_order_relaxed);
    const float out = outPeak_.load(std::memory_order_relaxed);

    // Peak-hold with decay so a transient stays visible for a few frames.
    inDisplay_ = juce::jmax(in, inDisplay_ * kDecay);
    outDisplay_ = juce::jmax(out, outDisplay_ * kDecay);
    repaint();

    if (++logDivider_ >= kLogEveryTicks) {
        logDivider_ = 0;
        if (onLog)
            onLog(in, out);
    }
}

float LevelMeter::toBarFraction(float peak) {
    if (peak <= 0.0f)
        return 0.0f;
    const float db = juce::Decibels::gainToDecibels(peak, kFloorDb);
    return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / (0.0f - kFloorDb));
}

void LevelMeter::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    auto area = getLocalBounds().reduced(4);

    auto drawBar = [&g](juce::Rectangle<int> r, float level, const juce::String& label) {
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(r);
        const int w = juce::roundToInt(static_cast<float>(r.getWidth() - 2) * toBarFraction(level));
        g.setColour(level >= 1.0f ? juce::Colours::red : juce::Colours::limegreen);
        g.fillRect(r.getX() + 1, r.getY() + 1, w, r.getHeight() - 2);
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        g.drawText(label, r.reduced(6, 0), juce::Justification::centredLeft);
    };

    const int half = area.getHeight() / 2;
    drawBar(area.removeFromTop(half).reduced(0, 1), inDisplay_, "IN");
    drawBar(area.reduced(0, 1), outDisplay_, "OUT");
}

} // namespace acfx::workbench
