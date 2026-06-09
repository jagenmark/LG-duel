#pragma once

#include "sim/Arena.hpp"
#include "sim/PlayerState.hpp"

namespace lg {

[[nodiscard]] bool resolvePlayerCollision(
  const Arena& arena,
  PlayerState& first,
  PlayerState& second
);

} // namespace lg
