#pragma once
#include <cstdint>
#include "effects/modulated-delay/modulated-delay-effect.h"
namespace acfx {
using NucleoModulatedDelay = ModulatedDelayEffect<14400, std::int16_t, 2>;
}
// ~300 ms stereo int16 delay core; keep the storage inside the SRAM headroom.
static_assert(sizeof(acfx::NucleoModulatedDelay) <= 96u * 1024u,
              "NucleoModulatedDelay exceeds its SRAM budget");
