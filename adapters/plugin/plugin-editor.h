#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "primitives/analysis/capture-probe.h"

// The plugin's editor. This is a grouped control surface: it walks the
// processor's parameter tree (the "Group/Label" groups built in
// plugin-parameters.cpp) and lays each group out as a labelled section of
// rotary knobs (plus a combo box for discrete params), each attached to its
// RangedAudioParameter. No timer, no analysis — parameter attachments keep the
// controls and the host in sync both ways.
//
// The processor still owns the RT harmonic capture probe (it is pushed to on
// the audio thread regardless of whether a GUI is open); the constants and
// typedef below stay here because PluginProcessor references HarmonicProbe. The
// current editor does not display it — a live-readout view is a separate follow
// up, not part of this control surface.

namespace acfx::plugin {

class PluginProcessor; // full definition only needed in plugin-editor.cpp

inline constexpr std::size_t kHarmonicWindowSize = 8192;
inline constexpr std::size_t kHarmonicRingCapacity = kHarmonicWindowSize + 4096;
inline constexpr int kHarmonicNumHarmonics = 6;

using HarmonicProbe = acfx::CaptureProbeRing<kHarmonicRingCapacity>;

class PluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
    };
    struct Choice {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attach;
    };
    // One control cell in a section: either a knob or a choice box.
    struct Cell {
        bool isChoice = false;
        int  index    = 0; // into knobs_ or choices_
    };
    struct Section {
        juce::String          name;
        std::vector<Cell>     cells;
        juce::Rectangle<int>  headerArea; // filled by resized(), painted by paint()
        int                   column = 0; // which layout column (filled by planLayout)
        int                   rows   = 1; // wrapped rows of cells (filled by planLayout)
    };

    void buildControls(PluginProcessor& processor);
    // Assign sections to columns and compute the window size. Sections keep
    // their internal 5-wide knob grid; when the single-column height would run
    // off a laptop screen the sections are split into two balanced columns.
    void planLayout();

    std::unique_ptr<juce::LookAndFeel> lnf_;
    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<Section> sections_;
    juce::String title_;      // effect name shown in the header eyebrow
    int          numColumns_ = 1;
    int          contentW_   = 0;
    int          contentH_   = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace acfx::plugin
