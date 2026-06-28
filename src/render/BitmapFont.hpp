#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lg {

struct BitmapGlyphLookup {
  std::uint32_t atlasCodepoint = 0;
  std::size_t byteLength = 0;
  bool drawable = false;
  bool fallback = false;
};

[[nodiscard]] BitmapGlyphLookup bitmapGlyphAt(
  std::string_view text,
  std::size_t offset
);

[[nodiscard]] std::optional<std::array<std::uint8_t, 8>>
supplementalBitmapGlyph(std::uint32_t codepoint);

} // namespace lg
