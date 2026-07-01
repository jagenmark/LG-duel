#pragma once

#include "app/HudPresentation.hpp"
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
  return style == PlayerOutlineStyle::Geometry;
}

enum class OutlineGroup : std::uint8_t {
  None = 0,
  Enemy = 1,
  Teammate = 2,
};

enum class OutlineVisibility : std::uint8_t {
  None = 0,
  VisibleOnly = 1,
  OccludedOnly = 2,
  VisibleAndOccluded = 3,
};

struct OutlineState {
  OutlineGroup group = OutlineGroup::None;
  OutlineVisibility visibility = OutlineVisibility::None;
  float widthPixels = 0.0F;
  float alpha = 1.0F;
  float pulse = 0.0F;
};

struct RenderSettings {
  float fieldOfView = 90.0F;
  bool enemyLeanEnabled = true;
  float enemyLeanScale = 1.0F;
  bool teammateLeanEnabled = true;
  float teammateLeanScale = 1.0F;
  float healthTextScale = 2.0F;
  float playerSizePixels = 14.0F;
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
  float damageNumbersDuration = 0.65F;
  float damageNumbersSize = 1.6F;
  float damageNumbersAlpha = 1.0F;
  std::uint8_t damageNumbersRed = 255;
  std::uint8_t damageNumbersGreen = 236;
  std::uint8_t damageNumbersBlue = 128;
  float damageNumbersOffsetX = 0.0F;
  float damageNumbersOffsetY = -46.0F;
  std::uint8_t enemyRed = 224;
  std::uint8_t enemyGreen = 82;
  std::uint8_t enemyBlue = 92;
  float enemyAlpha = 1.0F;
  int playerModel = 1;
  bool enemyOutlineEnabled = true;
  PlayerOutlineStyle playerOutlineStyle = PlayerOutlineStyle::ScreenSpace;
  float enemyOutlineWidth = 3.0F;
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
  float teammateOutlineWidth = 3.0F;
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
  Weapon localSelectedWeapon = Weapon::LightningGun;
  bool shotgunWeaponModelStart = false;
  bool drawRemotePlayers = true;
  bool drawRemoteWeapons = true;
  bool drawPlayerOutlines = true;
  bool frustumCullRemotePlayers = true;
  bool showRendererPerf = false;
  bool showRendererPerfDetail = false;
  std::uint8_t localPlayerIndex = 0;
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
  struct SettingsMenuItem {
    std::string label;
    std::string value;
    bool active = false;
    bool changed = false;
    bool command = false;
  };

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
    std::string speakerName;
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
  bool settingsOpen = false;
  std::vector<SettingsMenuItem> settingsItems;
  std::string settingsFooter;
  bool showOpponentHealthBar = false;
  std::int32_t healthAmount = 100;
  DamageNumberPresentation damageNumbers;
};

struct RemotePlayerView {
  PlayerState player = {};
  LightningGunResult lightningGun = {};
  Weapon selectedWeapon = Weapon::LightningGun;
  float enemyHitAmount = 0.0F;
  float enemyHealthAlpha = 1.0F;
  bool visible = false;
  bool teammate = false;
  std::string name;
};

struct RendererFrameDiagnostics {
  float swapchainAcquireMilliseconds = 0.0F;
  float sceneBuildMilliseconds = 0.0F;
  float gpuVertexUploadMilliseconds = 0.0F;
  float worldDrawIssueMilliseconds = 0.0F;
  float renderBuildUploadMilliseconds = 0.0F;
  float submitMilliseconds = 0.0F;
  float totalRenderMilliseconds = 0.0F;
  std::uint32_t worldSourceTriangles = 0;
  std::uint32_t worldRenderedTriangles = 0;
  std::uint32_t worldVertexCount = 0;
  std::uint32_t worldDrawCalls = 0;
  std::uint32_t worldLoadedTextures = 0;
  std::uint32_t worldReferencedMaterials = 0;
  std::uint32_t dynamicOpaqueVertices = 0;
  std::uint32_t dynamicTranslucentVertices = 0;
  std::uint32_t totalUploadedVertices = 0;
  std::uint32_t dynamicTriangles = 0;
  std::uint32_t normalPlayerBodyDynamicVertices = 0;
  std::uint32_t geometryOutlineDynamicVertices = 0;
  std::uint32_t outlinedPlayers = 0;
  std::uint32_t outlineMaskWidth = 0;
  std::uint32_t outlineMaskHeight = 0;
  std::uint32_t outlinePasses = 0;
  bool outlineCompositeEnabled = false;
  bool geometryOutlineFallbackUsed = false;
  std::uint32_t visibleRemotePlayers = 0;
  std::uint32_t remoteBodyModelsBuilt = 0;
  std::uint32_t remoteWeaponModelsBuilt = 0;
  std::uint32_t playerOutlinesBuilt = 0;
  std::uint32_t remoteCandidates = 0;
  std::uint32_t remoteFrustumVisible = 0;
  std::uint32_t remoteFrustumCulled = 0;
  std::uint32_t remoteWeaponCandidates = 0;
  std::uint32_t remoteWeaponsFrustumCulled = 0;
  std::uint32_t remoteWeaponInstances = 0;
  std::uint32_t remoteWeaponInstanceUploadBytes = 0;
  std::uint32_t remoteWeaponBatches = 0;
  std::uint32_t remoteWeaponDrawCalls = 0;
  std::uint32_t legacyRemoteWeaponDynamicVertices = 0;
  std::uint32_t firstPersonViewModelDrawCalls = 0;
  std::uint32_t firstPersonViewModelDynamicVertices = 0;
  std::uint32_t projectilesActive = 0;
  std::uint32_t projectilesFrustumCulled = 0;
  std::uint32_t projectilesRendered = 0;
  std::uint32_t plasmaInstances = 0;
  std::uint32_t rocketInstances = 0;
  std::uint32_t grenadeInstances = 0;
  std::uint32_t projectileCoreInstances = 0;
  std::uint32_t projectileGlowInstances = 0;
  std::uint32_t opaqueProjectileBatches = 0;
  std::uint32_t additiveProjectileBatches = 0;
  std::uint32_t projectileInstanceUploadBytes = 0;
  std::uint32_t projectileMeshDrawCalls = 0;
  std::uint32_t projectileGlowDrawCalls = 0;
  std::uint32_t legacyProjectileDynamicVertices = 0;
  std::string_view selectedPresentModeName = "n/a";
};

enum class PresentMode : int {
  Fifo = 0,
  Mailbox = 1,
  Immediate = 2,
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
  [[nodiscard]] bool setPresentMode(PresentMode mode);
  [[nodiscard]] std::string_view backendName() const;
  [[nodiscard]] const RendererFrameDiagnostics& lastFrameDiagnostics() const;
  void shutdown();

private:
  void* renderer_ = nullptr;
  void* gpuDevice_ = nullptr;
  void* gpuPipeline_ = nullptr;
  void* gpuPipeline3D_ = nullptr;
  void* gpuPipeline3DTranslucent_ = nullptr;
  void* gpuPipelineInstancedMesh_ = nullptr;
  void* gpuPipelineStaticMesh_ = nullptr;
  void* gpuPipelineInstancedGlow_ = nullptr;
  void* gpuPipelineOutlineMask_ = nullptr;
  void* gpuPipelineOutlineComposite_ = nullptr;
  void* gpuVertexBuffer_ = nullptr;
  void* gpuTransferBuffer_ = nullptr;
  void* gpuSimpleResources_ = nullptr;
  void* gpuFontTexture_ = nullptr;
  void* gpuFontSampler_ = nullptr;
  void* gpuWorldTextureAtlas_ = nullptr;
  void* gpuStaticWorld_ = nullptr;
  void* gpuVertexScratch_ = nullptr;
  void* gpuDepthTexture_ = nullptr;
  void* gpuOutlineMaskTexture_ = nullptr;
  void* gpuOutlineMaskSampler_ = nullptr;
  std::uint32_t gpuDepthWidth_ = 0;
  std::uint32_t gpuDepthHeight_ = 0;
  std::uint32_t gpuOutlineMaskWidth_ = 0;
  std::uint32_t gpuOutlineMaskHeight_ = 0;
  void* window_ = nullptr;
  std::string backendName_ = "uninitialized";
  RendererFrameDiagnostics lastFrameDiagnostics_ = {};
  bool gpuBackend_ = false;
  bool gpuErrorReported_ = false;
};

} // namespace lg
