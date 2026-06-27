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

enum class PlayerOutlineStyle : int {
  Geometry = 0,
  ScreenSpace = 1,
};

[[nodiscard]] inline bool usesGeometryPlayerOutlineFallback(
  PlayerOutlineStyle style
) {
  return style == PlayerOutlineStyle::Geometry ||
    style == PlayerOutlineStyle::ScreenSpace;
}

struct RenderSettings {
  int renderMode = 0;
  float fieldOfView = 90.0F;
  float cameraZoom = 1.0F;
  bool rotateView = false;
  bool enemyLeanEnabled = true;
  float enemyLeanScale = 1.0F;
  bool teammateLeanEnabled = true;
  float teammateLeanScale = 1.0F;
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
  std::uint8_t crosshairHitRed = 255;
  std::uint8_t crosshairHitGreen = 255;
  std::uint8_t crosshairHitBlue = 255;
  float crosshairHitAmount = 0.0F;
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
  float teammateBeamWidth = 2.0F;
  float teammateBeamAlpha = 1.0F;
  std::uint8_t teammateBeamRed = 80;
  std::uint8_t teammateBeamGreen = 220;
  std::uint8_t teammateBeamBlue = 150;
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
  bool enemyOutlineEnabled = true;
  PlayerOutlineStyle playerOutlineStyle = PlayerOutlineStyle::Geometry;
  float enemyOutlineWidth = 0.045F;
  float enemyOutlineAlpha = 1.0F;
  std::uint8_t enemyOutlineRed = 255;
  std::uint8_t enemyOutlineGreen = 220;
  std::uint8_t enemyOutlineBlue = 84;
  std::uint8_t enemyHitRed = 255;
  std::uint8_t enemyHitGreen = 190;
  std::uint8_t enemyHitBlue = 198;
  float enemyHitAmount = 0.0F;
  std::uint8_t teammateRed = 82;
  std::uint8_t teammateGreen = 190;
  std::uint8_t teammateBlue = 224;
  float teammateAlpha = 1.0F;
  bool teammateOutlineEnabled = true;
  float teammateOutlineWidth = 0.045F;
  float teammateOutlineAlpha = 1.0F;
  std::uint8_t teammateOutlineRed = 128;
  std::uint8_t teammateOutlineGreen = 240;
  std::uint8_t teammateOutlineBlue = 255;

  bool enemyHealthBarEnabled = true;
  bool enemyHealthBarDamageOnly = false;
  bool enemyHealthBarFade = true;
  float enemyHealthBarVisibleDuration = 5.0F;
  float enemyHealthBarMaxDistance = 0.0F;
  float enemyHealthBarWidth = 72.0F;
  float enemyHealthBarHeight = 7.0F;
  float enemyHealthBarWorldOffsetZ = 0.35F;
  float enemyHealthBarScreenOffsetX = 0.0F;
  float enemyHealthBarScreenOffsetY = -18.0F;
  float enemyHealthBarAlpha = 1.0F;
  std::uint8_t enemyHealthBarRed = 224;
  std::uint8_t enemyHealthBarGreen = 82;
  std::uint8_t enemyHealthBarBlue = 92;
  bool teammateHealthBarEnabled = true;
  bool teammateHealthBarDamageOnly = false;
  bool teammateHealthBarFade = true;
  float teammateHealthBarVisibleDuration = 5.0F;
  float teammateHealthBarMaxDistance = 0.0F;
  float teammateHealthBarWidth = 72.0F;
  float teammateHealthBarHeight = 7.0F;
  float teammateHealthBarWorldOffsetZ = 0.35F;
  float teammateHealthBarScreenOffsetX = 0.0F;
  float teammateHealthBarScreenOffsetY = -18.0F;
  float teammateHealthBarAlpha = 1.0F;
  std::uint8_t teammateHealthBarRed = 82;
  std::uint8_t teammateHealthBarGreen = 190;
  std::uint8_t teammateHealthBarBlue = 224;
  bool enemyNameTagEnabled = true;
  float enemyNameTagAlpha = 1.0F;
  float enemyNameTagScale = 1.5F;
  float enemyNameTagWorldOffsetZ = 0.75F;
  float enemyNameTagScreenOffsetX = 0.0F;
  float enemyNameTagScreenOffsetY = -34.0F;
  float enemyNameTagMaxDistance = 0.0F;
  std::uint8_t enemyNameTagRed = 255;
  std::uint8_t enemyNameTagGreen = 235;
  std::uint8_t enemyNameTagBlue = 235;
  bool teammateNameTagEnabled = true;
  float teammateNameTagAlpha = 1.0F;
  float teammateNameTagScale = 1.5F;
  float teammateNameTagWorldOffsetZ = 0.75F;
  float teammateNameTagScreenOffsetX = 0.0F;
  float teammateNameTagScreenOffsetY = -34.0F;
  float teammateNameTagMaxDistance = 0.0F;
  std::uint8_t teammateNameTagRed = 210;
  std::uint8_t teammateNameTagGreen = 245;
  std::uint8_t teammateNameTagBlue = 255;
  bool showLagCompensation = false;
  bool hasRemotePlayer = true;
};

struct ConsoleRenderState {
  bool open = false;
  std::vector<std::string> lines;
  std::string input;
  std::size_t cursorIndex = 0;
  bool hasSelection = false;
  std::size_t selectionAnchor = 0;
  std::size_t selectionFocus = 0;
};

struct HudRenderState {
  std::vector<std::string> topLeftLines;
  std::vector<std::string> topRightLines;
  std::vector<std::string> centerLines;
  std::vector<std::string> bottomCenterLines;
  Weapon selectedWeapon = Weapon::LightningGun;
  Weapon previousWeapon = Weapon::LightningGun;
  float weaponSwitchProgress = 1.0F;
  float centerOffsetY = 0.0F;
  std::string countdownText;
  float countdownPulse = 0.0F;
  struct ChatLine {
    std::uint8_t playerIndex = 0;
    std::string message;
  };
  std::vector<ChatLine> chatLines;
  std::string chatInput;
  std::size_t chatCursorIndex = 0;
  bool chatHasSelection = false;
  std::size_t chatSelectionAnchor = 0;
  std::size_t chatSelectionFocus = 0;
  bool chatInputOpen = false;
  bool scoreboardOpen = false;
  std::vector<std::string> scoreboardLines;
  std::vector<Team> scoreboardLineTeams;
  bool showOpponentHealthBar = false;
  std::int32_t healthAmount = 100;
};

struct RemotePlayerView {
  PlayerState player = {};
  LightningGunResult lightningGun = {};
  float enemyHitAmount = 0.0F;
  float enemyHealthAlpha = 1.0F;
  bool visible = false;
  bool teammate = false;
  std::string name;
};

struct RendererFrameDiagnostics {
  float swapchainAcquireMilliseconds = 0.0F;
  float renderBuildUploadMilliseconds = 0.0F;
  float submitMilliseconds = 0.0F;
  float totalRenderMilliseconds = 0.0F;
  std::string_view selectedPresentModeName = "n/a";
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
    const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
    const LightningGunResult& localLightningGun,
    const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
    const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
    const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
    const RenderSettings& settings,
    const HudRenderState& hud,
    const ConsoleRenderState& console
  );
  [[nodiscard]] bool setVSync(bool enabled);
  [[nodiscard]] std::string_view backendName() const;
  [[nodiscard]] const RendererFrameDiagnostics& lastFrameDiagnostics() const;
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
  RendererFrameDiagnostics lastFrameDiagnostics_ = {};
  bool gpuBackend_ = false;
  bool gpuErrorReported_ = false;
};

} // namespace lg
