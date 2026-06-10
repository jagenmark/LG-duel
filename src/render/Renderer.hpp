#pragma once

#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/PlayerState.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lg {

struct RenderSettings {
  float fieldOfView = 90.0F;
  bool crosshairEnabled = true;
  int crosshairStyle = 0;
  float crosshairSize = 8.0F;
  float crosshairThickness = 2.0F;
  float crosshairGap = 3.0F;
  float crosshairAlpha = 1.0F;
  std::uint8_t crosshairRed = 255;
  std::uint8_t crosshairGreen = 255;
  std::uint8_t crosshairBlue = 255;
  float beamWidth = 2.0F;
  float beamAlpha = 1.0F;
  std::uint8_t beamRed = 74;
  std::uint8_t beamGreen = 166;
  std::uint8_t beamBlue = 255;
};

struct ConsoleRenderState {
  bool open = false;
  std::vector<std::string> lines;
  std::string input;
};

class Renderer {
public:
  Renderer() = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  ~Renderer();

  [[nodiscard]] bool initialize(void* window);
  void render(
    const Arena& arena,
    const PlayerState& player,
    const PlayerState& opponent,
    const LightningGunResult& lightningGun,
    const RenderSettings& settings,
    const ConsoleRenderState& console
  );
  [[nodiscard]] bool setVSync(bool enabled);
  void shutdown();

private:
  void* renderer_ = nullptr;
};

} // namespace lg
