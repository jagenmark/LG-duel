#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>

namespace lg {

[[nodiscard]] PlayerState interpolatePlayerState(
  const PlayerState& previous,
  const PlayerState& current,
  float alpha
);

class SnapshotInterpolation {
public:
  void push(const ServerSnapshot& snapshot);

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] PlayerState player(std::size_t playerIndex, float alpha) const;

private:
  ServerSnapshot previous_ = {};
  ServerSnapshot current_ = {};
  bool initialized_ = false;
};

} // namespace lg
