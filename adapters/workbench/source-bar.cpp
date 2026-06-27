#include "source-bar.h"

// T009 (US2) — Live/File source selection UI. Callbacks only; the workbench applies
// the actual source change through the audio-stopped restart.

namespace acfx::workbench {

SourceBar::SourceBar() {
    liveButton_.onClick = [this] {
        if (onSelectLive)
            onSelectLive();
    };
    fileButton_.onClick = [this] { openChooser(); };
    addAndMakeVisible(liveButton_);
    addAndMakeVisible(fileButton_);
}

void SourceBar::openChooser() {
    // Async chooser: keeps the message thread responsive and never touches the audio
    // thread. The FileChooser must outlive the launch, so it is owned by the bar.
    chooser_ = std::make_unique<juce::FileChooser>(
        "Choose an audio file to loop", juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.mp3");
    const auto flags =
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const juce::File result = fc.getResult();
        if (result == juce::File{}) {
            // Cancelled with no selection — let the workbench keep a valid source
            // (revert to Live if none) rather than enter a broken no-source state.
            if (onChooseCancelled)
                onChooseCancelled();
            return;
        }
        if (onChooseFile)
            onChooseFile(result);
    });
}

void SourceBar::resized() {
    auto area = getLocalBounds().reduced(4);
    liveButton_.setBounds(area.removeFromLeft(area.getWidth() / 2).reduced(4, 0));
    fileButton_.setBounds(area.reduced(4, 0));
}

} // namespace acfx::workbench
