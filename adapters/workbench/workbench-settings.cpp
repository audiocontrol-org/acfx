#include "workbench-settings.h"

// T001 stub — intentionally returns defaults so the serde round-trip test (T003)
// fails RED until the real implementation lands in T002.

namespace acfx::workbench {

std::string serialize(const SourceConfig&) { return {}; }

SourceConfig parse(const std::string&) { return {}; }

} // namespace acfx::workbench
