#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace lg::render {

[[nodiscard]] bool writeRgbaCapturePng(
  const std::string& path,
  std::uint32_t width,
  std::uint32_t height,
  std::span<const std::uint8_t> rgbaPixels,
  std::string& error
);

} // namespace lg::render
