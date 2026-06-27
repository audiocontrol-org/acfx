#include "workbench-persistence.h"

#include <string>

// T012 (US3) — persistence over juce::ApplicationProperties. The SourceConfig serde it
// calls (serialize/parse) lives in the JUCE-free workbench-settings TU; this TU is the
// JUCE boundary that converts std::string <-> juce::String and owns the settings file.

namespace acfx::workbench {

namespace {
constexpr const char* kDeviceStateKey = "audioDeviceState";
constexpr const char* kSourceConfigKey = "sourceConfig";
} // namespace

WorkbenchPersistence::WorkbenchPersistence() {
    juce::PropertiesFile::Options options;
    options.applicationName = "acfx Workbench";
    options.filenameSuffix = "settings";
    options.folderName = "acfx";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    applicationProperties_.setStorageParameters(options);
}

LoadedSettings WorkbenchPersistence::load() {
    LoadedSettings out;
    auto* props = applicationProperties_.getUserSettings();

    // A settings file that exists and is non-empty but does not parse as XML is
    // corrupt — surface it rather than silently starting fresh (FR-009).
    const juce::File file = props->getFile();
    if (file.existsAsFile() && file.getSize() > 0 && juce::parseXML(file) == nullptr)
        out.corrupt = true;

    if (props->containsKey(kDeviceStateKey)) {
        out.deviceState = props->getXmlValue(kDeviceStateKey);
        // A present-but-unparseable device-state value is also corruption.
        if (out.deviceState == nullptr && props->getValue(kDeviceStateKey).isNotEmpty())
            out.corrupt = true;
    }

    out.source = parse(props->getValue(kSourceConfigKey).toStdString());
    return out;
}

void WorkbenchPersistence::save(const juce::AudioDeviceManager& deviceManager,
                                const SourceConfig& source) {
    const auto xml = deviceManager.createStateXml();
    writeSettings(xml.get(), source);
}

void WorkbenchPersistence::savePreserving(const juce::XmlElement* deviceState,
                                          const SourceConfig& source) {
    writeSettings(deviceState, source);
}

void WorkbenchPersistence::writeSettings(const juce::XmlElement* deviceState,
                                         const SourceConfig& source) {
    auto* props = applicationProperties_.getUserSettings();

    if (deviceState != nullptr)
        props->setValue(kDeviceStateKey, deviceState);
    else
        props->removeValue(kDeviceStateKey);

    const std::string serialized = serialize(source);
    props->setValue(kSourceConfigKey,
                    juce::String::fromUTF8(serialized.c_str(),
                                           static_cast<int>(serialized.length())));
    props->saveIfNeeded();
}

} // namespace acfx::workbench
