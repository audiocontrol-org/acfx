#include <cmath>
#include <cstdint>

#include "harmonic-readout.h"

// Implementation of the live harmonic readout (see harmonic-readout.h). All
// audio-thread interaction is confined to CaptureProbeRing::push(), called
// directly by workbench-app.cpp's getNextAudioBlock; everything here runs on
// the message thread.

namespace acfx::workbench {

namespace {
constexpr int kTimerHz = 20; // FR-027: ~15-30 Hz nominal live-readout cadence
constexpr double kMinFundamentalHz = 20.0;
constexpr double kMaxFundamentalHz = 20000.0;
constexpr double kDefaultFundamentalHz = 1000.0;
constexpr int kRowHeight = 14;
} // namespace

HarmonicReadout::HarmonicReadout(HarmonicProbe& probe) : probe_(probe) {
    fundamentalLabel_.setText("Fundamental (Hz)", juce::dontSendNotification);
    fundamentalLabel_.setJustificationType(juce::Justification::centredLeft);
    fundamentalLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(fundamentalLabel_);

    fundamentalSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    fundamentalSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    fundamentalSlider_.setRange(kMinFundamentalHz, kMaxFundamentalHz, 1.0);
    fundamentalSlider_.setSkewFactorFromMidPoint(kDefaultFundamentalHz);
    fundamentalSlider_.setValue(kDefaultFundamentalHz, juce::dontSendNotification);
    // A fundamental change invalidates the running window (the Goertzel bin it
    // targets moves), so it rebuilds the readout rather than mutating it in place.
    fundamentalSlider_.onValueChange = [this] { rebuildReadout(); };
    addAndMakeVisible(fundamentalSlider_);

    rebuildReadout();
    startTimerHz(kTimerHz);
}

HarmonicReadout::~HarmonicReadout() { stopTimer(); }

void HarmonicReadout::prepare(double sampleRate) {
    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;
    rebuildReadout();
}

void HarmonicReadout::rebuildReadout() {
    acfx::analysis::LiveReadoutConfig config;
    config.fundamentalHz = fundamentalSlider_.getValue();
    config.sampleRate = sampleRate_;
    config.numHarmonics = kHarmonicNumHarmonics;
    config.windowSize = kHarmonicWindowSize;
    // A fresh LiveReadout starts with no result; the ring itself is untouched (it
    // still holds whatever the audio thread already pushed), so the very next
    // timerCallback picks back up as soon as a full window is available.
    readout_ = std::make_unique<acfx::analysis::LiveReadout<kHarmonicRingCapacity>>(probe_,
                                                                                     config);
}

void HarmonicReadout::timerCallback() {
    if (readout_)
        readout_->update(); // drains the ring + runs the shared engine (analysis thread role)
    repaint();
}

void HarmonicReadout::resized() {
    auto area = getLocalBounds().reduced(4);
    auto controlRow = area.removeFromTop(24);
    fundamentalLabel_.setBounds(controlRow.removeFromLeft(120));
    fundamentalSlider_.setBounds(controlRow);
}

void HarmonicReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    auto area = getLocalBounds().reduced(4);
    area.removeFromTop(28); // controls row (fundamental label + slider)

    g.setColour(juce::Colours::white);
    g.setFont(12.0f);

    if (readout_ == nullptr || !readout_->hasResult()) {
        g.drawText("analyzing... (needs a full analysis window of audio)", area,
                   juce::Justification::topLeft);
        return;
    }

    const acfx::analysis::HarmonicSpectrum& spectrum = readout_->spectrum();
    const acfx::analysis::ThdnResult& thdn = readout_->thdn();

    auto row = area.removeFromTop(kRowHeight);
    const juce::String snrText =
        std::isfinite(thdn.snr) ? juce::String(thdn.snr, 1) + " dB" : juce::String("inf dB");
    g.drawText("THD+N: " + juce::String(thdn.thdPlusN * 100.0, 3) + " %   SNR: " + snrText
                   + "   noise floor: " + juce::String(thdn.noiseFloor, 5),
               row, juce::Justification::topLeft);

    for (int k = 1; k <= spectrum.numHarmonics; ++k) {
        const acfx::analysis::HarmonicSpectrum::Bin bin = spectrum.at(k);
        row = area.removeFromTop(kRowHeight);
        const juce::String magText =
            std::isnan(bin.magnitude) ? juce::String("--") : juce::String(bin.magnitude, 5);
        const juce::String phaseText =
            std::isnan(bin.phaseRad) ? juce::String("--") : juce::String(bin.phaseRad, 3);
        g.drawText("H" + juce::String(k) + "  mag=" + magText + "  phase=" + phaseText + " rad",
                   row, juce::Justification::topLeft);
    }

    const std::uint64_t overruns = probe_.overrunCount();
    if (overruns > 0) {
        row = area.removeFromTop(kRowHeight);
        g.setColour(juce::Colours::orange);
        g.drawText("ring overruns: " + juce::String(static_cast<juce::int64>(overruns)), row,
                   juce::Justification::topLeft);
    }
}

} // namespace acfx::workbench
