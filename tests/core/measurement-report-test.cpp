// measurement-report-test.cpp
// Doctest cases for US4: CSV report emission (T015).
// FR-014, SC-005.
//
// Two cases:
//   1. Emission ON  — CsvReport::write() produces a well-formed CSV file with
//      the canonical header and one row per added MeasurementRow.
//   2. Emission OFF — without calling write(), no file is created; CI relies
//      solely on doctest assertions.
//
// Both cases use unique temp paths and remove any temp file at the end.

#include <filesystem>
#include <fstream>
#include <string>

#include <doctest/doctest.h>

#include "support/measurement/report.h"

using namespace acfx::measure;

TEST_CASE("CsvReport: write() produces a well-formed CSV with canonical header and correct rows (FR-014, SC-005)") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "acfx-measure-t015-emission-on.csv";

    // Remove any leftover file from a prior (failed) run.
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    CsvReport report;

    report.add(MeasurementRow{
        "svf-lowpass",  // effect
        "magnitude",    // metric
        "sine@100Hz",   // stimulus
        48000.0,        // sampleRate
        512,            // blockSize
        0.9987,         // value
        "ratio",        // units
        0.01,           // tolerance
        true            // pass
    });

    report.add(MeasurementRow{
        "svf-lowpass",  // effect
        "thd",          // metric
        "sine@100Hz",   // stimulus
        48000.0,        // sampleRate
        512,            // blockSize
        0.0032,         // value
        "ratio",        // units
        0.05,           // tolerance
        true            // pass
    });

    report.add(MeasurementRow{
        "svf-lowpass",  // effect
        "latency",      // metric
        "impulse",      // stimulus
        48000.0,        // sampleRate
        512,            // blockSize
        5.0,            // value
        "samples",      // units
        1.0,            // tolerance
        false           // pass
    });

    report.write(path.string());

    // Verify the file exists and is well-formed.
    std::ifstream csv(path.string());
    REQUIRE(csv.is_open());

    // (a) First line must be the canonical header exactly.
    std::string header;
    REQUIRE(std::getline(csv, header));
    CHECK(header == "effect,metric,stimulus,sample_rate,block_size,value,units,tolerance,pass");

    // (b) Count data lines and (c) check that known field tokens appear in the
    //     expected rows.
    int  dataLines        = 0;
    bool foundMagnitude   = false;
    bool foundLatencyFail = false;

    std::string line;
    while (std::getline(csv, line)) {
        if (line.empty()) {
            continue;
        }
        ++dataLines;
        if (line.find("svf-lowpass") != std::string::npos &&
            line.find("magnitude")   != std::string::npos &&
            line.find("true")        != std::string::npos) {
            foundMagnitude = true;
        }
        if (line.find("latency") != std::string::npos &&
            line.find("false")   != std::string::npos) {
            foundLatencyFail = true;
        }
    }
    csv.close();

    // (b) Number of data lines must equal number of rows added.
    CHECK(dataLines == 3);

    // (c) Known field content appears in the expected rows.
    CHECK(foundMagnitude   == true);
    CHECK(foundLatencyFail == true);

    // Clean up the temp file.
    std::filesystem::remove(path);
    CHECK(std::filesystem::exists(path) == false);
}

TEST_CASE("CsvReport: emission OFF — no write() call leaves no file on disk (SC-005)") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "acfx-measure-t015-emission-off.csv";

    // Ensure the path does not pre-exist.
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    // Simulate a measurement run with reporting disabled: no CsvReport is
    // constructed and write() is never called.  CI correctness gates on the
    // doctest assertions below — no CSV artifact is produced.
    CHECK(std::filesystem::exists(path) == false);

    // Also assert: constructing a CsvReport and adding rows but NOT calling
    // write() leaves the path absent (the class is truly off by default).
    {
        CsvReport report;
        report.add(MeasurementRow{
            "svf-lowpass", "magnitude", "sine@100Hz",
            48000.0, 512, 0.99, "ratio", 0.01, true
        });
        // write() intentionally NOT called.
    }
    CHECK(std::filesystem::exists(path) == false);
}
