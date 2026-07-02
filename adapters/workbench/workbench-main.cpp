#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "workbench-app.h"

// The JUCE application bootstrap for the desktop sketch-and-hear workbench
// (T022, T026, T030). Extracted from workbench-app.cpp (which holds the
// WorkbenchComponent implementation, declared in workbench-app.h) so that file
// stays within the ~500-line file-size gate (Constitution VII).

namespace acfx::workbench {

class WorkbenchApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "acfx Workbench"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override {
        // A tailable log of the audio configuration + periodic peak levels (no audio
        // data). createDefaultAppLogger writes to ~/Library/Logs/acfx/acfx-workbench.log
        // on macOS. Created before the window so startup/config logging is captured.
        logger_.reset(juce::FileLogger::createDefaultAppLogger(
            "acfx", "acfx-workbench.log", "acfx Workbench session start"));
        juce::Logger::setCurrentLogger(logger_.get());
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override {
        mainWindow_ = nullptr;
        juce::Logger::setCurrentLogger(nullptr); // unset before the logger is destroyed
        logger_ = nullptr;
    }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name, juce::Colours::darkgrey,
                                   juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new WorkbenchComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<juce::FileLogger> logger_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace acfx::workbench

START_JUCE_APPLICATION(acfx::workbench::WorkbenchApplication)
