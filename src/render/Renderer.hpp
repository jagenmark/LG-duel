#pragma once

#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "net/NetProtocol.hpp"
#include "sim/PlayerState.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

struct RenderSettings {
  int renderMode = 0;
  float fieldOfView = 90.0F;
  float cameraZoom = 1.0F;
  bool rotateView = false;
  bool enemyLeanEnabled = true;
  float enemyLeanScale = 1.0F;
  float healthTextScale = 2.0F;
  float playerSizePixels = 14.0F;
  bool crosshairUseScreenPosition = false;
  float crosshairScreenX = 0.0F;
  float crosshairScreenY = 0.0F;
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
  float beamPulse = 0.0F;
  float beamAlpha = 1.0F;
  std::uint8_t beamRed = 74;
  std::uint8_t beamGreen = 166;
  std::uint8_t beamBlue = 255;
  std::uint8_t beamHitRed = 255;
  std::uint8_t beamHitGreen = 255;
  std::uint8_t beamHitBlue = 255;
  float beamHitAmount = 0.0F;
  float enemyBeamWidth = 2.0F;
  float enemyBeamAlpha = 1.0F;
  std::uint8_t enemyBeamRed = 255;
  std::uint8_t enemyBeamGreen = 110;
  std::uint8_t enemyBeamBlue = 80;
  bool hitMarkerEnabled = true;
  float hitMarkerSize = 10.0F;
  float hitMarkerThickness = 2.0F;
  std::uint8_t hitMarkerRed = 255;
  std::uint8_t hitMarkerGreen = 255;
  std::uint8_t hitMarkerBlue = 255;
  float hitMarkerAmount = 0.0F;
  std::uint8_t enemyRed = 224;
  std::uint8_t enemyGreen = 82;
  std::uint8_t enemyBlue = 92;
  float enemyAlpha = 1.0F;
  std::uint8_t enemyHitRed = 255;
  std::uint8_t enemyHitGreen = 190;
  std::uint8_t enemyHitBlue = 198;
  float enemyHitAmount = 0.0F;
  bool showLagCompensation = false;
  bool hasRemotePlayer = true;
};

struct ConsoleRenderState {
  bool open = false;
  std::vector<std::string> lines;
  std::string input;
};

struct HudRenderState {
  std::vector<std::string> topLeftLines;
  std::vector<std::string> topRightLines;
  std::vector<std::string> centerLines;
  std::vector<std::string> bottomCenterLines;
  float centerOffsetY = 0.0F;
  std::string countdownText;
  float countdownPulse = 0.0F;
  std::vector<std::string> chatLines;
  std::string chatInput;
  bool chatInputOpen = false;
  bool scoreboardOpen = false;
  std::vector<std::string> scoreboardLines;
  bool showOpponentHealthBar = false;
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
    const LightningGunResult& localLightningGun,
    const LightningGunResult& opponentLightningGun,
    const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
    const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
    const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
    const RenderSettings& settings,
    const HudRenderState& hud,
    const ConsoleRenderState& console
  );
  [[nodiscard]] bool setVSync(bool enabled);
  [[nodiscard]] std::string_view backendName() const;
  void shutdown();

private:
  void* renderer_ = nullptr;
  void* gpuDevice_ = nullptr;
  void* gpuPipeline_ = nullptr;
  void* gpuPipeline3D_ = nullptr;
  void* gpuPipeline3DTranslucent_ = nullptr;
  void* gpuVertexBuffer_ = nullptr;
  void* gpuTransferBuffer_ = nullptr;
  void* gpuFontTexture_ = nullptr;
  void* gpuFontSampler_ = nullptr;
  void* gpuVertexScratch_ = nullptr;
  void* gpuDepthTexture_ = nullptr;
  std::uint32_t gpuDepthWidth_ = 0;
  std::uint32_t gpuDepthHeight_ = 0;
  void* window_ = nullptr;
  std::string backendName_ = "uninitialized";
  bool gpuBackend_ = false;
  bool gpuErrorReported_ = false;
};

} // namespace lg
