#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

// The main-window source bar: a "Live" button plus a "Load file..." button
// (research.md decision 4). It owns NO audio logic — it emits callbacks only, so the
// workbench decides how to apply a source change (always via the audio-stopped
// restart). The file chooser is async (juce::FileChooser::launchAsync) so it never
// blocks the message thread; on a valid pick it reports the file, and on cancel it
// reports the cancellation so the workbench can keep a valid source (FR-003/004/009).

namespace acfx::workbench {

class SourceBar final : public juce::Component {
public:
    SourceBar();

    // The user chose live input.
    std::function<void()> onSelectLive;
    // The user picked a valid file in the chooser.
    std::function<void(const juce::File&)> onChooseFile;
    // The user opened the chooser but cancelled without choosing a file.
    std::function<void()> onChooseCancelled;

    // Set the chooser's file filter to the formats the decoder actually supports, so
    // the picker never advertises a format that cannot be read (AUDIT-20260627-06). The
    // workbench passes WorkbenchAudioSource::supportedFileWildcard().
    void setFileFilter(const juce::String& wildcard) { fileFilter_ = wildcard; }

    void resized() override;

private:
    void openChooser();

    juce::TextButton liveButton_{"Live"};
    juce::TextButton fileButton_{"Load file..."};
    // Conservative default; the workbench overrides it with the decoder's real wildcard.
    juce::String fileFilter_{"*.wav;*.aiff;*.aif"};
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceBar)
};

} // namespace acfx::workbench
