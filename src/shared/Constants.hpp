#pragma once

#include <cstddef>

namespace lg {

inline constexpr float kFixedTickRate = 125.0F;
inline constexpr float kFixedTickSeconds = 1.0F / kFixedTickRate;
inline constexpr std::size_t kMaxPlayers = 6;

} // namespace lg
