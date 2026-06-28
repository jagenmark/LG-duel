#include "render/BitmapFont.hpp"

namespace lg {
namespace {

using GlyphRows = std::array<std::uint8_t, 8>;

struct SupplementalGlyph {
  std::uint32_t codepoint = 0;
  GlyphRows rows = {};
};

constexpr std::array<SupplementalGlyph, 6> kSupplementalGlyphs = {{
  {
    0x00C5U, // U+00C5 Latin capital letter A with ring above
    {0x18, 0x24, 0x18, 0x24, 0x42, 0x7E, 0x42, 0x42},
  },
  {
    0x00C4U, // U+00C4 Latin capital letter A with diaeresis
    {0x42, 0x00, 0x18, 0x24, 0x42, 0x7E, 0x42, 0x42},
  },
  {
    0x00D6U, // U+00D6 Latin capital letter O with diaeresis
    {0x42, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C},
  },
  {
    0x00E5U, // U+00E5 Latin small letter a with ring above
    {0x18, 0x24, 0x18, 0x3C, 0x40, 0x7C, 0x42, 0x7C},
  },
  {
    0x00E4U, // U+00E4 Latin small letter a with diaeresis
    {0x42, 0x00, 0x00, 0x3C, 0x40, 0x7C, 0x42, 0x7C},
  },
  {
    0x00F6U, // U+00F6 Latin small letter o with diaeresis
    {0x42, 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C},
  },
}};

[[nodiscard]] BitmapGlyphLookup fallbackGlyph(std::size_t byteLength) {
  return {'?', byteLength, true, true};
}

} // namespace

BitmapGlyphLookup bitmapGlyphAt(std::string_view text, std::size_t offset) {
  if (offset >= text.size()) {
    return {};
  }

  const auto first = static_cast<unsigned char>(text[offset]);
  if (first == ' ') {
    return {' ', 1U, false, false};
  }
  if (first >= 32U && first < 127U) {
    return {first, 1U, true, false};
  }

  if (first == 0xC3U && offset + 1U < text.size()) {
    const auto second = static_cast<unsigned char>(text[offset + 1U]);
    switch (second) {
    case 0x85U:
      return {0x00C5U, 2U, true, false};
    case 0x84U:
      return {0x00C4U, 2U, true, false};
    case 0x96U:
      return {0x00D6U, 2U, true, false};
    case 0xA5U:
      return {0x00E5U, 2U, true, false};
    case 0xA4U:
      return {0x00E4U, 2U, true, false};
    case 0xB6U:
      return {0x00F6U, 2U, true, false};
    default:
      return fallbackGlyph(1U);
    }
  }

  return fallbackGlyph(1U);
}

std::optional<std::array<std::uint8_t, 8>>
supplementalBitmapGlyph(std::uint32_t codepoint) {
  for (const SupplementalGlyph& glyph : kSupplementalGlyphs) {
    if (glyph.codepoint == codepoint) {
      return glyph.rows;
    }
  }
  return std::nullopt;
}

} // namespace lg
