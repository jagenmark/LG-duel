#include "render/CaptureWriter.hpp"

#include "dev/PngWriter.hpp"

#include <limits>

#ifndef LG_DUEL_HAS_SDL3
#define LG_DUEL_HAS_SDL3 0
#endif

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace lg::render {

bool writeRgbaCapturePng(
  const std::string& path,
  std::uint32_t width,
  std::uint32_t height,
  std::span<const std::uint8_t> rgbaPixels,
  std::string& error
) {
#if LG_DUEL_HAS_SDL3
  if (width == 0U || height == 0U) {
    error = "capture dimensions must be non-zero";
    return false;
  }
  const std::uint64_t pixelBytes =
    static_cast<std::uint64_t>(width) * height * 4U;
  if (
    pixelBytes != rgbaPixels.size() ||
    width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4) ||
    height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
  ) {
    error = "capture pixel buffer size does not match its dimensions";
    return false;
  }

  SDL_Surface* surface = SDL_CreateSurfaceFrom(
    static_cast<int>(width),
    static_cast<int>(height),
    SDL_PIXELFORMAT_RGBA32,
    const_cast<std::uint8_t*>(rgbaPixels.data()),
    static_cast<int>(width * 4U)
  );
  if (surface == nullptr) {
    error = std::string("could not create capture image: ") + SDL_GetError();
    return false;
  }
  const bool saved = SDL_SavePNG(surface, path.c_str());
  if (!saved) {
    error = std::string("could not write capture PNG: ") + SDL_GetError();
  }
  SDL_DestroySurface(surface);
  return saved;
#else
  return dev::writeRgbaPng(path, width, height, rgbaPixels, error);
#endif
}

} // namespace lg::render
