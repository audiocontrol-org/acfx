#pragma once
#include <cmath>
namespace acfx {
inline float crushToGrid(float x, int bits) noexcept {
    if (bits >= 16) return x;                       // hard bypass — load-bearing for float identity
    const float steps = static_cast<float>(1 << (bits - 1));  // 2^(bits-1)
    const float q     = 1.0f / steps;               // step = 2^(1-bits) over [-1,1)
    const float g     = std::round(x / q) * q;      // mid-tread; 0 -> 0
    if (g >= 1.0f)  return 1.0f;                     // saturate the top grid point
    if (g < -1.0f)  return -1.0f;
    return g;
}
} // namespace acfx
