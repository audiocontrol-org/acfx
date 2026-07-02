// analysis-window-test.cpp
// T005 -- harmonic-analysis feature, Foundational phase: RED test for the
// selectable analysis window (FR-025, research.md Decision 3, data-model.md
// "Window" entity). Asserts:
//   - default WindowKind is BlackmanHarris (4-term, ~-92 dB sidelobes)
//   - all three selectable kinds exist: BlackmanHarris, Hann, FlatTop
//   - coefficient correctness vs known closed forms at a small N
//   - symmetry: w[n] == w[N-1-n] within tolerance
//
// host/analysis/window.h does not exist yet at RED time -- this test is
// expected to fail to build until T006 lands it.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "analysis/window.h"

using acfx::analysis::Window;
using acfx::analysis::WindowKind;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTolerance = 1e-9;

// Closed-form 4-term Blackman-Harris coefficient (research.md Decision 3 /
// T005 brief): w[n] = a0 - a1*cos(2*pi*n/(N-1)) + a2*cos(4*pi*n/(N-1))
//                       - a3*cos(6*pi*n/(N-1))
double blackmanHarrisReference(int n, int size) {
    constexpr double a0 = 0.35875;
    constexpr double a1 = 0.48829;
    constexpr double a2 = 0.14128;
    constexpr double a3 = 0.01168;
    const double denom = static_cast<double>(size - 1);
    const double phase = 2.0 * kPi * static_cast<double>(n) / denom;
    return a0 - a1 * std::cos(phase) + a2 * std::cos(2.0 * phase) - a3 * std::cos(3.0 * phase);
}

// Closed-form Hann coefficient: w[n] = 0.5 * (1 - cos(2*pi*n/(N-1)))
double hannReference(int n, int size) {
    const double denom = static_cast<double>(size - 1);
    const double phase = 2.0 * kPi * static_cast<double>(n) / denom;
    return 0.5 * (1.0 - std::cos(phase));
}

} // namespace

TEST_CASE("Window defaults to Blackman-Harris") {
    const Window w(16);
    CHECK(w.kind() == WindowKind::BlackmanHarris);
}

TEST_CASE("Window selectable kinds exist: BlackmanHarris, Hann, FlatTop") {
    const Window bh(16, WindowKind::BlackmanHarris);
    const Window hann(16, WindowKind::Hann);
    const Window flatTop(16, WindowKind::FlatTop);

    CHECK(bh.kind() == WindowKind::BlackmanHarris);
    CHECK(hann.kind() == WindowKind::Hann);
    CHECK(flatTop.kind() == WindowKind::FlatTop);

    CHECK(bh.coeffs().size() == 16);
    CHECK(hann.coeffs().size() == 16);
    CHECK(flatTop.coeffs().size() == 16);
}

TEST_CASE("Window Blackman-Harris coefficients match the closed form at N=16") {
    constexpr int kSize = 16;
    const Window w(kSize, WindowKind::BlackmanHarris);
    const auto& coeffs = w.coeffs();
    REQUIRE(coeffs.size() == static_cast<std::size_t>(kSize));

    for (int n = 0; n < kSize; ++n) {
        const double expected = blackmanHarrisReference(n, kSize);
        CHECK(coeffs[static_cast<std::size_t>(n)] == doctest::Approx(expected).epsilon(kTolerance));
    }
}

TEST_CASE("Window Hann coefficients match the closed form at N=16") {
    constexpr int kSize = 16;
    const Window w(kSize, WindowKind::Hann);
    const auto& coeffs = w.coeffs();
    REQUIRE(coeffs.size() == static_cast<std::size_t>(kSize));

    for (int n = 0; n < kSize; ++n) {
        const double expected = hannReference(n, kSize);
        CHECK(coeffs[static_cast<std::size_t>(n)] == doctest::Approx(expected).epsilon(kTolerance));
    }
}

TEST_CASE("Window coefficients are symmetric: w[n] == w[N-1-n]") {
    constexpr int kSize = 16;
    const WindowKind kinds[] = {WindowKind::BlackmanHarris, WindowKind::Hann, WindowKind::FlatTop};

    for (WindowKind kind : kinds) {
        const Window w(kSize, kind);
        const auto& coeffs = w.coeffs();
        REQUIRE(coeffs.size() == static_cast<std::size_t>(kSize));

        for (int n = 0; n < kSize / 2; ++n) {
            const double lo = coeffs[static_cast<std::size_t>(n)];
            const double hi = coeffs[static_cast<std::size_t>(kSize - 1 - n)];
            CHECK(lo == doctest::Approx(hi).epsilon(kTolerance));
        }
    }
}
