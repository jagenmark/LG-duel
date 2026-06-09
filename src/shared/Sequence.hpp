#pragma once

#include <cstdint>

namespace lg {

[[nodiscard]] constexpr bool isSequenceNewer(std::uint32_t sequence, std::uint32_t previous) {
  return static_cast<std::int32_t>(sequence - previous) > 0;
}

[[nodiscard]] constexpr bool isSequenceAcknowledged(
  std::uint32_t sequence,
  std::uint32_t acknowledged
) {
  return !isSequenceNewer(sequence, acknowledged);
}

} // namespace lg
