#pragma once

// tests/support/measurement/stimulus.h
//
// RE-EXPORT SHIM (harmonic-analysis T007; research.md Decision 1/8; analyze
// finding F1). The implementation relocated to host/analysis/stimulus.h so
// product adapters can reuse it without depending on the test tree.
// Dependency direction: tests/support -> host/analysis, never the reverse.
// Existing `#include "support/measurement/stimulus.h"` call sites and
// `acfx::measure::...` usages are unaffected.

#include "analysis/stimulus.h"
