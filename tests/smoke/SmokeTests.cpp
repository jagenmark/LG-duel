#include "app/GameApp.hpp"
#include "render/CaptureWriter.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

[[nodiscard]] std::uint64_t currentProcessId() {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(_getpid());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

[[nodiscard]] std::filesystem::path temporaryCapturePath() {
  const std::uint64_t timestamp = static_cast<std::uint64_t>(
    std::chrono::steady_clock::now().time_since_epoch().count()
  );
  return std::filesystem::temp_directory_path() /
    ("lg-duel-compressed-capture-test-" +
      std::to_string(currentProcessId()) + "-" +
      std::to_string(timestamp) + ".png");
}

} // namespace

int main() {
  const lg::GameApp app("127.0.0.1", 27960);
  int failures = 0;

  failures += expect(app.name() == "LG Duel Client", "app name should be stable");

#if LG_DUEL_HAS_SDL3
  const std::filesystem::path capturePath = temporaryCapturePath();
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
  const bool removed = std::filesystem::remove(capturePath, removeError);
  failures += expect(
    !removeError && removed && !std::filesystem::exists(capturePath),
    "capture writer should remove only its unique temporary PNG"
  );
#endif

  return failures == 0 ? 0 : 1;
}
