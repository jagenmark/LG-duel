#include "app/GameApp.hpp"
#include "render/CaptureWriter.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  const lg::GameApp app("127.0.0.1", 27960);
  int failures = 0;

  failures += expect(app.name() == "LG Duel Client", "app name should be stable");

#if LG_DUEL_HAS_SDL3
  const std::filesystem::path capturePath =
    std::filesystem::temp_directory_path() / "lg-duel-compressed-capture-test.png";
  constexpr std::uint32_t captureWidth = 1920U;
  constexpr std::uint32_t captureHeight = 1200U;
  std::vector<std::uint8_t> capturePixels(
    static_cast<std::size_t>(captureWidth) * captureHeight * 4U
  );
  for (std::size_t index = 0; index < capturePixels.size(); index += 4U) {
    capturePixels[index] = 24U;
    capturePixels[index + 1U] = 72U;
    capturePixels[index + 2U] = 120U;
    capturePixels[index + 3U] = 255U;
  }
  std::string captureError;
  failures += expect(
    lg::render::writeRgbaCapturePng(
      capturePath.string(),
      captureWidth,
      captureHeight,
      capturePixels,
      captureError
    ),
    "capture writer should save a full-size RGBA image"
  );
  std::error_code sizeError;
  const std::uintmax_t captureBytes =
    std::filesystem::file_size(capturePath, sizeError);
  failures += expect(
    !sizeError && captureBytes < 256U * 1024U,
    "capture PNG should compress repeated scene pixels"
  );
  std::error_code removeError;
  std::filesystem::remove(capturePath, removeError);
#endif

  return failures == 0 ? 0 : 1;
}
