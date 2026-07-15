#include "svf-asset-sweep.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "dft.h"
#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include "json-utils.h"
#include "sha256.h"
#include "synthetic-source.h"
#include "wav-writer.h"

namespace lessonassets {
namespace {

constexpr int kSampleRate = 48000;
constexpr double kAudioClipSeconds = 2.0;
constexpr int kAudioClipSamples = static_cast<int>(kSampleRate * kAudioClipSeconds);

// Impulse-response render length: long enough (~170 ms) for every preset in
// defaultPresets() -- including the higher-resonance one -- to settle back
// toward silence, while keeping the impulse.json payload a reasonable size.
// A fixed length, not a decay-detector: documented, not claimed precise.
constexpr int kImpulseSamples = 8192;

constexpr double kResponseFreqMinHz = 20.0;
constexpr double kResponseFreqMaxHz = 20000.0;
constexpr int kResponseFreqCount = 64;

void writeFileBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("failed to open for write: " + path);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error("failed writing: " + path);
}

void writeFileText(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("failed to open for write: " + path);
    file << text;
    if (!file)
        throw std::runtime_error("failed writing: " + path);
}

// Configures a fresh SvfEffect from a preset's plain-unit values, converting
// through the effect's OWN descriptor table (acfx::normalize), never a
// hand-rolled mapping -- so this tool can never drift from the real param
// law. Takes the effect by reference (not returned by value): SvfEffect
// holds std::atomic members for its lock-free cross-thread parameter
// handoff (svf-effect.h), so it is non-copyable/non-movable by design.
void prepareEffect(acfx::SvfEffect& effect, const SvfAssetPreset& preset, int maxBlockSize) {
    effect.prepare(acfx::ProcessContext{static_cast<double>(kSampleRate), maxBlockSize, 1});

    const auto& params = acfx::SvfEffect::kParams;
    effect.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},
                         acfx::normalize(params[acfx::SvfEffect::kCutoff], preset.cutoffHz));
    effect.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance},
                         acfx::normalize(params[acfx::SvfEffect::kResonance], preset.resonance));
    effect.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},
                         acfx::normalize(params[acfx::SvfEffect::kMode], static_cast<float>(preset.modeIndex)));
}

std::map<std::string, double> presetParams(const SvfAssetPreset& preset) {
    return {
        {"cutoffHz", preset.cutoffHz},
        {"resonance", preset.resonance},
        {"mode", static_cast<double>(preset.modeIndex)},
    };
}

// Renders the documented synthetic source through the real effect and writes
// the content-addressed WAV. Appends the resulting AssetEntry to `out`.
void renderAudioClip(const SvfAssetPreset& preset, const std::string& outDir, const std::string& provenance,
                      std::vector<AssetEntry>& out) {
    acfx::SvfEffect effect;
    prepareEffect(effect, preset, kAudioClipSamples);
    std::vector<float> buffer = generateSyntheticSource(kSampleRate, kAudioClipSamples);

    float* channels[1] = {buffer.data()};
    acfx::AudioBlock block(channels, 1, kAudioClipSamples);
    effect.process(block); // real acfx::SvfEffect::process() -- no faked DSP

    const std::vector<std::uint8_t> wavBytes = buildMonoWav16(buffer, kSampleRate);
    const std::string hash = sha256Hex(wavBytes);
    const std::string filename = preset.name + "." + hash.substr(0, 8) + ".wav";
    writeFileBytes((std::filesystem::path(outDir) / filename).string(), wavBytes);

    out.push_back(AssetEntry{"audio", filename, hash, "audio/wav", presetParams(preset), kSampleRate,
                              provenance});
}

// Renders a unit impulse through a FRESH effect instance (so the response is
// the filter's own impulse response, not tainted by the audio clip's tail),
// then writes both the impulse.json and the response.json (frequency
// response measured from that same real impulse response via direct DTFT
// evaluation -- dft.h -- never an analytic re-derivation of the SVF transfer
// function).
void renderResponseAndImpulse(const SvfAssetPreset& preset, const std::string& outDir,
                               const std::string& provenance, std::vector<AssetEntry>& out) {
    acfx::SvfEffect effect;
    prepareEffect(effect, preset, kImpulseSamples);

    std::vector<float> impulse(static_cast<std::size_t>(kImpulseSamples), 0.0f);
    impulse[0] = 1.0f;
    float* channels[1] = {impulse.data()};
    acfx::AudioBlock block(channels, 1, kImpulseSamples);
    effect.process(block); // real acfx::SvfEffect::process() -- the actual impulse response

    // impulse.json
    std::vector<double> impulseAsDouble(impulse.begin(), impulse.end());
    const std::string impulseJson = "{\n  \"sampleRate\": " + jsonNumber(kSampleRate) +
                                     ",\n  \"samples\": " + jsonNumberArray(impulseAsDouble) + "\n}\n";
    const std::string impulseHash = sha256Hex(impulseJson);
    const std::string impulseFilename = preset.name + "-impulse." + impulseHash.substr(0, 8) + ".json";
    writeFileText((std::filesystem::path(outDir) / impulseFilename).string(), impulseJson);
    out.push_back(AssetEntry{"impulse", impulseFilename, impulseHash, "application/json",
                              presetParams(preset), kSampleRate, provenance});

    // response.json -- measured from the impulse response above (T009: authoritative measurement).
    const std::vector<double> freqs = logSpacedFrequencies(kResponseFreqMinHz, kResponseFreqMaxHz,
                                                             kResponseFreqCount);
    const std::vector<double> magsDb = measureFrequencyResponseDb(impulse, kSampleRate, freqs);
    const std::string responseJson = "{\n  \"freqsHz\": " + jsonNumberArray(freqs) +
                                      ",\n  \"magsDb\": " + jsonNumberArray(magsDb) + "\n}\n";
    const std::string responseHash = sha256Hex(responseJson);
    const std::string responseFilename = preset.name + "-response." + responseHash.substr(0, 8) + ".json";
    writeFileText((std::filesystem::path(outDir) / responseFilename).string(), responseJson);
    out.push_back(AssetEntry{"response", responseFilename, responseHash, "application/json",
                              presetParams(preset), kSampleRate, provenance});
}

} // namespace

std::vector<SvfAssetPreset> defaultPresets() {
    return {
        // Three lowpass cutoffs at fixed low resonance: demonstrates roll-off
        // moving with cutoff (the lesson's core "hear it, see it" scenario).
        SvfAssetPreset{"lp-cutoff-low", 0, 200.0f, 0.1f},
        SvfAssetPreset{"lp-cutoff-mid", 0, 1000.0f, 0.1f},
        SvfAssetPreset{"lp-cutoff-high", 0, 5000.0f, 0.1f},
        // One higher-resonance lowpass preset: demonstrates the resonant peak.
        SvfAssetPreset{"lp-resonant", 0, 800.0f, 0.85f},
    };
}

std::vector<AssetEntry> sweepSvfPresets(const std::string& outDir, const std::vector<SvfAssetPreset>& presets,
                                         const std::string& provenance) {
    std::filesystem::create_directories(outDir);

    std::vector<AssetEntry> entries;
    for (const SvfAssetPreset& preset : presets) {
        renderAudioClip(preset, outDir, provenance, entries);
        renderResponseAndImpulse(preset, outDir, provenance, entries);
    }
    return entries;
}

} // namespace lessonassets
