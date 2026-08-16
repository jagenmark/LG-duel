#pragma once

#include <cstdint>
#include <limits>

namespace lg {

[[nodiscard]] constexpr std::uint32_t nextNonZeroSequence(
  std::uint32_t current
) {
  const std::uint32_t next = current + 1U;
  return next == 0U ? 1U : next;
}

[[nodiscard]] constexpr bool isSequenceNewer(std::uint32_t sequence, std::uint32_t previous) {
  return static_cast<std::int32_t>(sequence - previous) > 0;
}

// Counts nonzero sequence steps from older to newer. Sequence zero is reserved
// and therefore does not consume a step when the counter wraps.
[[nodiscard]] constexpr std::uint32_t nonZeroSequenceDistance(
  std::uint32_t newer,
  std::uint32_t older
) {
  return newer >= older
    ? newer - older
    : (std::numeric_limits<std::uint32_t>::max() - older) + newer;
}

[[nodiscard]] constexpr bool isSequenceAcknowledged(
  std::uint32_t sequence,
  std::uint32_t acknowledged
) {
  return !isSequenceNewer(sequence, acknowledged);
}

} // namespace lg
