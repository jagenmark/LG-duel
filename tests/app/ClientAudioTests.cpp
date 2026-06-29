#include "app/ClientAudio.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void expect(bool condition, const char* context) {
  if (condition) {
    return;
  }
  std::cerr << context << '\n';
  std::exit(1);
}

} // namespace

int main() {
  const std::size_t noVariant = lg::selectFootstepVariantIndex(
    0,
    std::numeric_limits<std::size_t>::max(),
    0
  );
  expect(
    noVariant == std::numeric_limits<std::size_t>::max(),
    "no footstep variants should return an invalid index"
  );

  for (std::uint32_t randomValue : {0U, 1U, 99U}) {
    expect(
      lg::selectFootstepVariantIndex(1, 0, randomValue) == 0,
      "one footstep variant should always select the only clip"
    );
  }

  for (std::size_t previous = 0; previous < 4; ++previous) {
    for (std::uint32_t randomValue = 0; randomValue < 32; ++randomValue) {
      const std::size_t selected =
        lg::selectFootstepVariantIndex(4, previous, randomValue);
      expect(selected < 4, "footstep variant selection should stay in range");
      expect(
        selected != previous,
        "footstep variant selection should not repeat the previous clip"
      );
    }
  }

  const std::array<std::size_t, 6> expected{1, 2, 3, 1, 2, 3};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    expect(
      lg::selectFootstepVariantIndex(4, 0, static_cast<std::uint32_t>(index)) ==
        expected[index],
      "footstep variant repeat protection should remap around the previous clip"
    );
  }

  return 0;
}
