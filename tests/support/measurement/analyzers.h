#pragma once

// tests/support/measurement/analyzers.h
//
// RE-EXPORT SHIM (harmonic-analysis T007; research.md Decision 1/8; analyze
// finding F1). The implementation relocated to host/analysis/analyzers.h so
// product adapters can reuse it without depending on the test tree.
// Dependency direction: tests/support -> host/analysis, never the reverse.
// Existing `#include "support/measurement/analyzers.h"` call sites and
// `acfx::measure::...` usages are unaffected.

#include "analysis/analyzers.h"
