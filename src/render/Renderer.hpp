#pragma once

#include "render/GpuTimestampTiming.hpp"

#include "app/HudPresentation.hpp"
#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "render/ConsoleCat.hpp"
#include "render/DrawList2D.hpp"
#include "render/PlayerPresentation.hpp"
#include "render/ViewModelPresentation.hpp"
#include "render/WeaponSwitchPresentation.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/PlayerState.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

enum class PlayerOutlineStyle : int {
  Geometry = 0,
  ScreenSpace = 1,
};

enum class PlayerOutlineMode : int {
  Disabled = 0,
  Compatibility = 1,
  NativeScreenSpace = 2,
};

enum class NativeOutlineFallbackReason : std::uint8_t {
  None = 0,
  BackendUnavailable,
  NativeResourcesUnavailable,
  CompatibilityResourcesUnavailable,
};

struct PlayerOutlinePathPlan {
  PlayerOutlineMode mode = PlayerOutlineMode::Disabled;
  PlayerOutlineStyle style = PlayerOutlineStyle::Geometry;
  NativeOutlineFallbackReason fallbackReason =
    NativeOutlineFallbackReason::None;
};

[[nodiscard]] constexpr PlayerOutlinePathPlan buildPlayerOutlinePathPlan(
  PlayerOutlineMode requestedMode,
  PlayerOutlineStyle requestedStyle,
  bool nativeBackendAvailable,
  bool nativeResourcesAvailable,
  bool compatibilityResourcesAvailable
) {
  if (requestedMode != PlayerOutlineMode::NativeScreenSpace) {
    return {requestedMode, requestedStyle, NativeOutlineFallbackReason::None};
  }
  if (nativeBackendAvailable && nativeResourcesAvailable) {
    return {requestedMode, requestedStyle, NativeOutlineFallbackReason::None};
  }
  const NativeOutlineFallbackReason reason = nativeBackendAvailable
    ? NativeOutlineFallbackReason::NativeResourcesUnavailable
    : NativeOutlineFallbackReason::BackendUnavailable;
  if (compatibilityResourcesAvailable) {
    return {
      PlayerOutlineMode::Compatibility,
      PlayerOutlineStyle::Geometry,
      reason,
    };
  }
  return {
    PlayerOutlineMode::Disabled,
    PlayerOutlineStyle::Geometry,
    NativeOutlineFallbackReason::CompatibilityResourcesUnavailable,
  };
}

[[nodiscard]] constexpr const char* nativeOutlineFallbackReasonName(
  NativeOutlineFallbackReason reason
) {
  switch (reason) {
  case NativeOutlineFallbackReason::None:
    return "none";
  case NativeOutlineFallbackReason::BackendUnavailable:
    return "backend-unavailable";
  case NativeOutlineFallbackReason::NativeResourcesUnavailable:
    return "native-resources-unavailable";
  case NativeOutlineFallbackReason::CompatibilityResourcesUnavailable:
    return "compatibility-resources-unavailable";
  }
  return "unknown";
}

[[nodiscard]] inline bool usesGeometryPlayerOutlineFallback(
  PlayerOutlineStyle style
) {
  return style == PlayerOutlineStyle::Geometry;
}

[[nodiscard]] inline bool usesGeometryPlayerOutlineFallback(
  PlayerOutlineMode mode,
  PlayerOutlineStyle style
) {
  return mode == PlayerOutlineMode::Compatibility &&
    usesGeometryPlayerOutlineFallback(style);
}

[[nodiscard]] inline bool usesScreenSpacePlayerOutlines(
  PlayerOutlineMode mode,
  PlayerOutlineStyle style
) {
  return mode == PlayerOutlineMode::NativeScreenSpace ||
    (mode == PlayerOutlineMode::Compatibility &&
     style == PlayerOutlineStyle::ScreenSpace);
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
  float fadeAlpha = 1.0F;
  float pulse = 0.0F;
};

struct RenderSettings {
  float fieldOfView = 90.0F;
  ViewModelPresentationOutput viewModelPresentation = {};
  bool enemyLeanEnabled = true;
  float enemyLeanScale = 1.0F;
  bool teammateLeanEnabled = true;
  float teammateLeanScale = 1.0F;
  float healthTextScale = 2.0F;
  int healthStyle = 0;
  float healthGroupOffsetX = 0.0F;
  float healthGroupOffsetY = 0.0F;
  float speedTextScale = 1.5F;
  float weaponBarScale = 1.75F;
  float fpsTextScale = 1.6F;
  std::string uiFont = "bahnschrift.ttf";
  float playerSizePixels = 14.0F;
  bool crosshairEnabled = true;
  int crosshairStyle = 0;
  float crosshairSize = 8.0F;
  float crosshairThickness = 2.0F;
  float crosshairGap = 3.0F;
  bool crosshairDotEnabled = false;
  float crosshairDotThickness = 2.0F;
  bool crosshairOutlineEnabled = false;
  float crosshairOutlineWidth = 1.0F;
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
  float beamPhaseRadians = 0.0F;
  double presentationTimeSeconds = 0.0;
  float freezeGunFiringAmount = 0.0F;
  float freezeGunActivationFlashAmount = 0.0F;
  float freezeGunCoolantPulse = 0.0F;
  float freezeGunVibrationPhaseRadians = 0.0F;
  float plasmaGunContainmentAmount = 0.0F;
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
  bool damageNumbersDamageColor = false;
  float damageNumbersOffsetX = 0.0F;
  float damageNumbersOffsetY = -46.0F;
  std::uint8_t enemyRed = 224;
  std::uint8_t enemyGreen = 82;
  std::uint8_t enemyBlue = 92;
  float enemyAlpha = 1.0F;
  int playerModel = 1;
  bool enemyOutlineEnabled = true;
  PlayerOutlineMode playerOutlineMode = PlayerOutlineMode::NativeScreenSpace;
  PlayerOutlineStyle playerOutlineStyle = PlayerOutlineStyle::Geometry;
  float playerOutlineWidth = 1.5F;
  bool playerOutlineDebugMask = false;
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
  WeaponSwitchPresentationOutput weaponSwitchPresentation = {};
  float machineGunBarrelRotationRadians = 0.0F;
  float machineGunRecoilAmount = 0.0F;
  float machineGunVibrationAmount = 0.0F;
  float machineGunVibrationPhaseRadians = 0.0F;
  float rocketLauncherMechanicalAmount = 0.0F;
  float rocketLauncherRecoilAmount = 0.0F;
  float revolverRecoilAmount = 0.0F;
  float revolverCylinderRotationRadians = 0.0F;
  std::array<float, kDuelPlayerCount> revolverTracerAlpha = [] {
    std::array<float, kDuelPlayerCount> values = {};
    values.fill(1.0F);
    return values;
  }();
  // The app supplies the short-lived rail tracer envelope. It does not alter
  // the authoritative fire event or any renderer quality setting.
  std::array<float, kDuelPlayerCount> sniperSmokeTracerAlpha = [] {
    std::array<float, kDuelPlayerCount> values = {};
    values.fill(1.0F);
    return values;
  }();
  std::array<Vec3, kDuelPlayerCount> sniperSmokeTracerDirections = {};
  std::array<float, kDuelPlayerCount> sniperSmokeTracerTraceLengths = {};
  bool showOwnWeapons = true;
  // The hand meshes are an experimental preview. Keep direct RenderSettings
  // callers aligned with the default-off client cvar.
  bool viewModelHandsEnabled = false;
  int weaponPosition = 0;
  bool shotgunWeaponModelStart = false;
  int combatEffectsQuality = 2;
  float muzzleLightIntensity = 2.4F;
  float muzzleLightRadius = 3.2F;
  float muzzleLightDurationSeconds = 0.045F;
  bool bloomEnabled = true;
  float bloomIntensity = 0.18F;
  float bloomThreshold = 1.15F;
  float toneMapExposure = 1.0F;
  // Final display-only control. It must not change scene-referred lighting.
  float displayGamma = 1.0F;
  int atmosphereGradeQuality = 2;
  // GPU-only quality controls. SDL_Renderer keeps its current output.
  // AA: 0 = 1x, 1 = 2x, 2 = 4x. Sun shadows: 0 = off, 1/2 = 1024/2048.
  // Point lights: 0 = baked and combat only, 1 = 16 live, 2 = 32 live.
  // Point shadows: 0 = off, 1 = one 256 face set, 2 = two 512 face sets.
  int antiAliasingQuality = 0;
  int sunShadowQuality = 0;
  int pointLightQuality = 1;
  int pointShadowQuality = 0;
  int materialQuality = 2;
  int ambientGroundingQuality = 2;
  int ambientDebugMode = 0;
  int playerRimQuality = 2;
  bool casingsEnabled = true;
  float casingCountMultiplier = 1.0F;
  float casingLifetimeSeconds = 2.4F;
  std::uint32_t maximumCasings = 48;
  float particleMultiplier = 1.0F;
  std::uint32_t maximumImpactParticles = 192;
  std::uint32_t maximumBulletDecals = 128;
  float bulletDecalLifetimeSeconds = 24.0F;
  bool drawRemotePlayers = true;
  bool contactShadowsEnabled = true;
  bool drawRemoteWeapons = true;
  bool drawPlayerOutlines = true;
  bool frustumCullRemotePlayers = true;
  bool worldFrustumCull = false;
  // Prototype: GPU-tests static-world chunks and writes indirect commands for
  // the main-camera depth and color passes. CPU BVH culling remains the
  // fallback and control path.
  bool worldGpuIndirect = false;
  bool benchmarkTimingEnabled = false;
  std::optional<std::uint64_t> benchmarkGpuFrameIndex;
  // The client map revision changes when the authoritative arena is replaced.
  // A zero value keeps direct renderer tests on the content-hash fallback.
  std::uint32_t mapRevision = 0;
  // 0 off, 1 all, 2 visible solids, 3 playerclip, 4 weapclip, 5 triggers.
  // This is presentation-only and never changes authoritative trace masks.
  int showCollision = 0;
  int textureFilter = 2;
  int textureAnisotropy = 8;
  float textureLodBias = 0.5F;
  bool showRendererPerf = false;
  bool showRendererPerfDetail = false;
  std::uint8_t localPlayerIndex = 0;
  bool showLagCompensation = false;
  bool hasRemotePlayer = true;
};

struct ConsoleRenderState {
  bool open = false;
  bool showCat = true;
  std::vector<std::string> lines;
  std::string input;
  std::size_t cursorIndex = 0;
  bool hasSelection = false;
  std::size_t selectionAnchor = 0;
  std::size_t selectionFocus = 0;
  bool inputHasSelection = false;
  std::size_t inputSelectionAnchor = 0;
  std::size_t inputSelectionFocus = 0;
  std::size_t scrollRows = 0;
  ConsoleCatPose cat;
};

struct HudRenderState {
  struct FreeForAllScoreboardRow {
    std::size_t rank = 0;
    std::uint8_t playerIndex = 0;
    std::string name;
    PlayerScore score = 0;
    Weapon accuracyWeapon = Weapon::LightningGun;
    std::uint32_t accuracyPercent = 0;
    std::uint32_t totalDamage = 0;
    bool localPlayer = false;
  };

  struct FreeForAllStandingRow {
    std::size_t rank = 0;
    std::uint8_t playerIndex = 0;
    std::string name;
    PlayerScore score = 0;
    bool localPlayer = false;
  };

  struct SettingsMenuItem {
    std::string label;
    std::string value;
    bool active = false;
    bool changed = false;
    bool command = false;
  };

  struct NetGraphState {
    int mode = 0;
    float scale = 1.75F;
    NetworkTelemetry telemetry = {};
    float interpolationEffectiveDelayMilliseconds = 0.0F;
    double interpolationBufferLeadTicks = 0.0;
    double interpolationDesiredBufferLeadTicks = 0.0;
    double interpolationTimelineErrorTicks = 0.0;
    double interpolationPresentationTick = 0.0;
    double interpolationNewestSnapshotTick = 0.0;
    float interpolationPlaybackRate = 0.0F;
    std::size_t interpolationBufferedSnapshotCount = 0;
    std::uint32_t interpolationSampleTick = 0;
    std::uint32_t interpolationUnderrunCount = 0;
    std::uint32_t interpolationHardCorrectionCount = 0;
    bool interpolationPlaybackStarted = false;
    bool interpolationUnderrun = false;
    bool interpolationSampleEligible = false;
    std::size_t pendingCommands = 0;
    std::uint32_t correctionCount = 0;
    float lastCorrectionDistance = 0.0F;
    std::size_t snapshotQueueDepth = 0;
    std::uint32_t requestedRewindTicks = 0;
    std::uint32_t appliedRewindTicks = 0;
  };

  std::vector<std::string> topLeftLines;
  std::vector<std::string> topCenterLines;
  std::vector<std::string> topRightLines;
  std::vector<std::string> centerLines;
  std::vector<std::string> bottomCenterLines;
  struct KillcamOverlay {
    bool active = false;
    std::string killer;
    std::string weapon;
    std::string cause;
    std::string prompt = "SPACE/ESC: SKIP";
    float progress = 0.0F;
  } killcam;
  McGuffinNavigationTarget mcguffinNavigation;
  std::string fpsText;
  std::string speedText;
  Weapon selectedWeapon = Weapon::LightningGun;
  bool sniperScopeActive = false;
  float sniperScopeAmount = 0.0F;
  std::uint8_t sniperChargePercent = 0;
  std::array<std::string, kWeaponCount> weaponValues = {{"\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E", "\xE2\x88\x9E"}};
  struct KillFeedLine {
    std::string killerName;
    std::string killedName;
    Weapon weapon = Weapon::LightningGun;
    float alpha = 1.0F;
  };
  std::vector<KillFeedLine> killFeedLines;
  Weapon previousWeapon = Weapon::LightningGun;
  float weaponSwitchProgress = 1.0F;
  float centerOffsetY = 0.0F;
  std::string countdownText;
  float countdownPulse = 0.0F;
  float deathDesaturation = 0.0F;
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
  bool chatHistoryHasSelection = false;
  std::size_t chatHistorySelectionAnchor = 0;
  std::size_t chatHistorySelectionFocus = 0;
  bool chatInputOpen = false;
  bool chatHistoryExpanded = false;
  std::size_t chatScrollRows = 0;
  bool scoreboardOpen = false;
  bool freeForAllScoreboard = false;
  std::vector<FreeForAllScoreboardRow> freeForAllScoreboardRows;
  std::vector<FreeForAllStandingRow> freeForAllStandingRows;
  std::vector<std::string> scoreboardLines;
  std::vector<Team> scoreboardLineTeams;
  std::vector<Weapon> scoreboardLineAccuracyWeapons;
  std::vector<std::size_t> scoreboardLineAccuracyWeaponColumns;
  bool settingsOpen = false;
  std::vector<SettingsMenuItem> settingsItems;
  std::size_t settingsScrollRows = 0;
  int settingsHoveredRow = -1;
  int settingsPressedRow = -1;
  std::string settingsFooter;
  bool miscMenuOpen = false;
  std::vector<SettingsMenuItem> miscMenuItems;
  std::size_t miscMenuScrollRows = 0;
  int miscMenuHoveredRow = -1;
  int miscMenuPressedRow = -1;
  std::string miscMenuFooter;
  bool trainerMenuOpen = false;
  std::vector<SettingsMenuItem> trainerMenuItems;
  std::size_t trainerMenuScrollRows = 0;
  int trainerMenuHoveredRow = -1;
  int trainerMenuPressedRow = -1;
  std::string trainerMenuFooter;
  bool showOpponentHealthBar = false;
  std::int32_t healthAmount = 100;
  DamageNumberPresentation damageNumbers;
  DirectionalDamagePresentation directionalDamage;
  NetGraphState netGraph;
};

inline constexpr float kDeadBodyFadeDurationSeconds = 1.5F;
inline constexpr float kDeadBodyOutlineFadeDurationSeconds = 0.20F;

struct RemoteBodyFade {
  float modelAlpha = 1.0F;
  float outlineAlpha = 1.0F;
  bool visible = true;
};

[[nodiscard]] inline RemoteBodyFade remoteBodyFadeAtAge(float ageSeconds) {
  if (!std::isfinite(ageSeconds) || ageSeconds <= 0.0F) {
    return {};
  }
  if (ageSeconds >= kDeadBodyFadeDurationSeconds) {
    return {0.0F, 0.0F, false};
  }
  return {
    1.0F - ageSeconds / kDeadBodyFadeDurationSeconds,
    ageSeconds >= kDeadBodyOutlineFadeDurationSeconds
      ? 0.0F
      : 1.0F - ageSeconds / kDeadBodyOutlineFadeDurationSeconds,
    true,
  };
}

struct RemotePlayerView {
  PlayerState player = {};
  LightningGunResult lightningGun = {};
  Weapon selectedWeapon = Weapon::LightningGun;
  float enemyHitAmount = 0.0F;
  float enemyHealthAlpha = 1.0F;
  bool visible = false;
  bool teammate = false;
  std::string name;
  float animationTimeSeconds = 0.0F;
  float machineGunBarrelRotationRadians = 0.0F;
  float rocketLauncherMechanicalAmount = 0.0F;
  float freezeGunFiringAmount = 0.0F;
  float freezeGunActivationFlashAmount = 0.0F;
  float freezeGunCoolantPulse = 0.0F;
  float freezeGunVibrationPhaseRadians = 0.0F;
  float plasmaGunContainmentAmount = 0.0F;
  PlayerPresentationFrame presentation = {};
  bool hasPresentation = false;
  WeaponSwitchPresentationOutput weaponSwitchPresentation = {};
  RemoteBodyFade bodyFade = {};
};

enum class TracerStyle : std::uint8_t {
  MachineGun,
  MachineGunMuzzleFlash,
  RevolverMuzzleFlash,
  RocketLauncherMuzzleFlash,
  Shotgun,
};

struct TransientTracer {
  Vec3 start = {};
  Vec3 end = {};
  float ageSeconds = 0.0F;
  float lifetimeSeconds = 0.05F;
  float width = 0.012F;
  RenderColor color = {};
  std::uint32_t seed = 0;
  TracerStyle style = TracerStyle::MachineGun;
};

enum class TransientEffectType : std::uint8_t {
  TrainerOrbTarget,
  TrainerWorkerTarget,
  RocketExplosionFlash,
  RocketExplosionCore,
  RocketExplosionHalo,
  PlasmaExplosionFlash,
  PlasmaExplosionCore,
  PlasmaExplosionHalo,
  GrenadeExplosionFlash,
  GrenadeExplosionCore,
  MachineGunMuzzleLight,
  RocketLauncherMuzzleLight,
  MachineGunMuzzleSmoke,
  RocketLauncherMuzzleSmoke,
  MachineGunMuzzleSpark,
  MachineGunCasing,
  BulletImpactFlash,
  BulletImpactSpark,
  BulletImpactDust,
  BulletDecal,
  RocketExplosionShard,
  RocketExplosionSmoke,
};

struct TransientEffect {
  TransientEffectType type = TransientEffectType::RocketExplosionCore;
  Vec3 position = {};
  float ageSeconds = 0.0F;
  float lifetimeSeconds = 0.0F;
  float initialScale = 1.0F;
  float finalScale = 1.0F;
  RenderColor color = {};
  std::uint32_t seed = 0;
  Vec3 velocity = {};
  Vec3 normal = {};
  Vec3 direction = {};
  float rotationRadians = 0.0F;
  float angularVelocityRadiansPerSecond = 0.0F;
  float intensity = 0.0F;
  float radius = 0.0F;
  std::uint8_t ownerIndex = 0;
};

struct PostProcessPlan {
  std::uint32_t sceneWidth = 0;
  std::uint32_t sceneHeight = 0;
  std::uint32_t bloomWidth = 0;
  std::uint32_t bloomHeight = 0;
  std::uint32_t bloomPasses = 0;
  std::uint32_t bloomDepthRebuildPasses = 0;
  bool bloomEnabled = false;
  bool bloomUsesWorldCamera = false;
  bool bloomMasksViewModel = false;
  std::uint32_t sceneCompositePasses = 0;
  std::uint32_t sceneCompositeOrder = 0;
  std::uint32_t outlineCompositeOrder = 0;
  std::uint32_t hudOrder = 0;
};

struct OutlineDepthPlan {
  bool reuseWorldDepth = false;
  bool rebuildDepth = true;
  std::uint32_t passCount = 6;
};

[[nodiscard]] constexpr OutlineDepthPlan buildOutlineDepthPlan(
  bool nativeOutline,
  bool singleSample
) {
  const bool reuseWorldDepth = nativeOutline && singleSample;
  return {
    reuseWorldDepth,
    !reuseWorldDepth,
    reuseWorldDepth ? 3U : 6U,
  };
}

enum class DirectPresentFallbackReason : std::uint8_t {
  None = 0,
  ColorGrade,
  Exposure,
  DisplayGamma,
  AntiAliasing,
  Bloom,
  SunShadow,
  QualityContract,
  LivePointLights,
  OutlineMode,
  ContactShadows,
  TranslucentVertices,
  TranslucentEffects,
  SimpleBatchPass,
  ActiveTextureAlpha,
  ActiveVertexAlpha,
  ActiveInstanceAlpha,
  PlayerAlpha,
  ViewModelAlpha,
  SwapchainFormat,
  Pipelines,
};

struct DirectPresentInputs {
  bool neutralGrade = false;
  bool unitExposure = false;
  bool neutralDisplayGamma = false;
  bool singleSample = false;
  bool bloomDisabled = false;
  bool sunShadowDisabled = false;
  bool competitiveQuality = false;
  bool livePointLightsEmpty = false;
  bool outlineModeSupported = false;
  bool contactShadowsEmpty = false;
  bool translucentVerticesEmpty = false;
  bool translucentEffectsEmpty = false;
  bool simpleBatchesOpaque = false;
  bool activeTexturesOpaque = false;
  bool activeVerticesOpaque = false;
  bool activeInstancesOpaque = false;
  bool playersOpaque = false;
  bool viewModelOpaque = false;
  bool swapchainFormatSupported = false;
  bool pipelinesReady = false;
};

struct DirectPresentPlan {
  bool eligible = false;
  DirectPresentFallbackReason fallback =
    DirectPresentFallbackReason::ColorGrade;
};

[[nodiscard]] constexpr DirectPresentPlan buildDirectPresentPlan(
  const DirectPresentInputs& inputs
) {
  if (!inputs.neutralGrade) {
    return {false, DirectPresentFallbackReason::ColorGrade};
  }
  if (!inputs.unitExposure) {
    return {false, DirectPresentFallbackReason::Exposure};
  }
  if (!inputs.neutralDisplayGamma) {
    return {false, DirectPresentFallbackReason::DisplayGamma};
  }
  if (!inputs.singleSample) {
    return {false, DirectPresentFallbackReason::AntiAliasing};
  }
  if (!inputs.bloomDisabled) {
    return {false, DirectPresentFallbackReason::Bloom};
  }
  if (!inputs.sunShadowDisabled) {
    return {false, DirectPresentFallbackReason::SunShadow};
  }
  if (!inputs.competitiveQuality) {
    return {false, DirectPresentFallbackReason::QualityContract};
  }
  if (!inputs.livePointLightsEmpty) {
    return {false, DirectPresentFallbackReason::LivePointLights};
  }
  if (!inputs.outlineModeSupported) {
    return {false, DirectPresentFallbackReason::OutlineMode};
  }
  if (!inputs.contactShadowsEmpty) {
    return {false, DirectPresentFallbackReason::ContactShadows};
  }
  if (!inputs.translucentVerticesEmpty) {
    return {false, DirectPresentFallbackReason::TranslucentVertices};
  }
  if (!inputs.translucentEffectsEmpty) {
    return {false, DirectPresentFallbackReason::TranslucentEffects};
  }
  if (!inputs.simpleBatchesOpaque) {
    return {false, DirectPresentFallbackReason::SimpleBatchPass};
  }
  if (!inputs.activeTexturesOpaque) {
    return {false, DirectPresentFallbackReason::ActiveTextureAlpha};
  }
  if (!inputs.activeVerticesOpaque) {
    return {false, DirectPresentFallbackReason::ActiveVertexAlpha};
  }
  if (!inputs.activeInstancesOpaque) {
    return {false, DirectPresentFallbackReason::ActiveInstanceAlpha};
  }
  if (!inputs.playersOpaque) {
    return {false, DirectPresentFallbackReason::PlayerAlpha};
  }
  if (!inputs.viewModelOpaque) {
    return {false, DirectPresentFallbackReason::ViewModelAlpha};
  }
  if (!inputs.swapchainFormatSupported) {
    return {false, DirectPresentFallbackReason::SwapchainFormat};
  }
  if (!inputs.pipelinesReady) {
    return {false, DirectPresentFallbackReason::Pipelines};
  }
  return {true, DirectPresentFallbackReason::None};
}

inline constexpr float kNeutralDisplayGamma = 1.0F;
inline constexpr float kMinimumDisplayGamma = 0.50F;
inline constexpr float kMaximumDisplayGamma = 1.50F;

[[nodiscard]] inline float clampedDisplayGamma(float displayGamma) {
  return std::clamp(
    displayGamma,
    kMinimumDisplayGamma,
    kMaximumDisplayGamma
  );
}

[[nodiscard]] inline bool displayGammaIsNeutral(float displayGamma) {
  return std::fabs(
    clampedDisplayGamma(displayGamma) - kNeutralDisplayGamma
  ) <= 0.000001F;
}

struct SceneClearColor {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
};

inline constexpr SceneClearColor kDirectSdrClearColor = {
  0.047F,
  0.055F,
  0.071F,
};

inline constexpr SceneClearColor kNeutralHdrSceneClearColor = {
  0.00421044F,
  0.00553159F,
  0.00842750F,
};

[[nodiscard]] inline float directPresentDisplayChannel(float linear) {
  const float color = std::max(linear, 0.0F);
  const float mapped = std::clamp(
    (color * (2.51F * color + 0.03F)) /
      (color * (2.43F * color + 0.59F) + 0.14F),
    0.0F,
    1.0F
  );
  return std::pow(mapped, 1.0F / 2.2F);
}

struct FragmentResourceLayout {
  std::uint32_t samplers = 0;
  std::uint32_t uniformBuffers = 0;
};

[[nodiscard]] constexpr FragmentResourceLayout
instancedColorFragmentLayout() {
  return {1U, 1U};
}

[[nodiscard]] constexpr FragmentResourceLayout
untexturedSceneLightFragmentLayout() {
  return {0U, 1U};
}

[[nodiscard]] constexpr FragmentResourceLayout
sceneCompositeFragmentLayout() {
  return {3U, 1U};
}

[[nodiscard]] constexpr FragmentResourceLayout
sceneCompositeNoBloomFragmentLayout() {
  return {1U, 1U};
}

enum class SampledDepthFormatChoice : std::uint8_t {
  None = 0,
  D32,
  D24,
  D16,
};

struct SampledDepthFormatSupport {
  bool d32 = false;
  bool d24 = false;
  bool d16 = false;
};

[[nodiscard]] constexpr SampledDepthFormatChoice chooseSampledDepthFormat(
  SampledDepthFormatSupport support
) {
  return support.d32
    ? SampledDepthFormatChoice::D32
    : support.d24
      ? SampledDepthFormatChoice::D24
      : support.d16
        ? SampledDepthFormatChoice::D16
        : SampledDepthFormatChoice::None;
}

struct AuxiliaryDepthPlan {
  bool enabled = false;
  bool depthOnly = true;
  std::uint32_t sampleCount = 1;
  bool usesWorldCamera = true;
  bool includesWorld = true;
  bool includesStaticMeshes = true;
  bool includesMaterialMeshes = true;
  bool includesGltfPlayers = true;
  bool includesSimpleInstances = true;
};

struct SunShadowPassPlan {
  std::uint32_t textureSize = 1;
  bool renderShadowPass = false;
  bool useClearedFallback = true;
};

struct PointShadowPassPlan {
  std::uint32_t textureSize = 1;
  std::uint32_t lightCount = 0;
  std::uint32_t layerCount = 1;
  bool renderCache = false;
  bool useClearedFallback = true;
};

[[nodiscard]] constexpr PointShadowPassPlan buildPointShadowPassPlan(
  int quality,
  std::uint32_t eligibleLightCount,
  bool cacheMatches
) {
  const std::uint32_t budget =
    quality <= 0 ? 0U : quality == 1 ? 1U : 2U;
  const std::uint32_t lightCount =
    std::min(budget, eligibleLightCount);
  const std::uint32_t textureSize =
    quality <= 0 ? 1U : quality == 1 ? 256U : 512U;
  return {
    textureSize,
    lightCount,
    std::max(1U, lightCount * 6U),
    lightCount > 0U && !cacheMatches,
    lightCount == 0U,
  };
}

[[nodiscard]] constexpr SunShadowPassPlan buildSunShadowPassPlan(
  std::uint32_t mapSize
) {
  return {
    std::max(1U, mapSize),
    mapSize > 0U,
    mapSize == 0U,
  };
}

[[nodiscard]] constexpr AuxiliaryDepthPlan buildAuxiliaryDepthPlan(
  bool requested,
  bool pipelinesReady
) {
  AuxiliaryDepthPlan plan;
  plan.enabled = requested && pipelinesReady;
  return plan;
}

[[nodiscard]] constexpr PostProcessPlan buildPostProcessPlan(
  std::uint32_t outputWidth,
  std::uint32_t outputHeight,
  bool bloomEnabled
) {
  return {
    outputWidth,
    outputHeight,
    bloomEnabled ? std::max(1U, (outputWidth + 3U) / 4U) : 0U,
    bloomEnabled ? std::max(1U, (outputHeight + 3U) / 4U) : 0U,
    bloomEnabled ? 3U : 0U,
    bloomEnabled ? 1U : 0U,
    bloomEnabled,
    bloomEnabled,
    bloomEnabled,
    1U,
    bloomEnabled ? 6U : 3U,
    bloomEnabled ? 7U : 4U,
    bloomEnabled ? 8U : 5U,
  };
}

struct RendererFrameDiagnostics {
  float swapchainAcquireMilliseconds = 0.0F;
  float lateMouseSampleMilliseconds = 0.0F;
  float mouseSampleToSubmitMilliseconds = 0.0F;
  float mouseSamplePhaseGainMilliseconds = 0.0F;
  bool lateMouseSampleEnabled = false;
  bool lateMouseSampleApplied = false;
  // Coarse CPU-side frame stages; GPU execution is not included.
  float renderInstanceConstructionMilliseconds = 0.0F;
  float worldVisibilityMilliseconds = 0.0F;
  float worldCommandEncodingMilliseconds = 0.0F;
  float dynamicCommandEncodingMilliseconds = 0.0F;
  float uiMilliseconds = 0.0F;
  float sceneBuildMilliseconds = 0.0F;
  float gpuVertexUploadMilliseconds = 0.0F;
  float worldDrawIssueMilliseconds = 0.0F;
  float renderBuildUploadMilliseconds = 0.0F;
  float submitMilliseconds = 0.0F;
  float totalRenderMilliseconds = 0.0F;
  std::uint32_t worldSourceTriangles = 0;
  std::uint32_t worldRenderedTriangles = 0;
  std::uint32_t worldSubmittedTriangles = 0;
  std::uint32_t worldDuplicateTrianglesCulled = 0;
  std::uint32_t worldVertexCount = 0;
  std::uint32_t worldDrawCalls = 0;
  std::uint32_t skyDrawCalls = 0;
  std::uint32_t skyLoadedTextures = 0;
  std::uint32_t worldSubmittedRanges = 0;
  std::uint32_t worldTotalChunks = 0;
  std::uint32_t worldVisibleChunks = 0;
  std::uint32_t worldCulledChunks = 0;
  std::uint32_t worldVisibilityTestedNodes = 0;
  float worldVisibilityQueryMilliseconds = 0.0F;
  float worldGpuIndirectCpuMilliseconds = 0.0F;
  bool worldGpuIndirect = false;
  std::uint32_t worldGpuIndirectCommands = 0;
  std::uint32_t worldGpuIndirectMaterialGroups = 0;
  int ambientGroundingQuality = 0;
  std::uint32_t ambientStaticRays = 0;
  std::uint32_t ambientStaticSamples = 0;
  std::uint32_t ambientStaticCacheHits = 0;
  std::uint8_t ambientStaticMinimum = 255;
  std::uint8_t ambientStaticMaximum = 255;
  std::uint32_t ambientProbeCount = 0;
  std::uint32_t ambientProbeRays = 0;
  std::uint32_t ambientProbeBytes = 0;
  std::uint64_t ambientProbeFingerprint = 0;
  float ambientProbeBuildMilliseconds = 0.0F;
  std::uint32_t ambientDynamicSamples = 0;
  std::uint32_t gpuDepthBits = 0;
  std::uint32_t worldLoadedTextures = 0;
  std::uint32_t worldMissingTextures = 0;
  std::uint32_t worldReferencedMaterials = 0;
  std::uint32_t worldMaxTextureMipLevels = 0;
  int worldTextureFilter = 2;
  int worldRequestedTextureAnisotropy = 8;
  int worldAppliedTextureAnisotropy = 8;
  float worldTextureLodBias = 0.0F;
  std::uint32_t dynamicOpaqueVertices = 0;
  std::uint32_t dynamicTranslucentVertices = 0;
  std::uint32_t totalUploadedVertices = 0;
  std::uint32_t dynamicTriangles = 0;
  std::uint32_t normalPlayerBodyDynamicVertices = 0;
  std::uint32_t geometryOutlineDynamicVertices = 0;
  std::uint32_t outlinedPlayers = 0;
  int outlineStyle = 0;
  std::uint32_t outlineMaskWidth = 0;
  std::uint32_t outlineMaskHeight = 0;
  std::uint32_t outlineWorkWidth = 0;
  std::uint32_t outlineWorkHeight = 0;
  float outlineWorkScale = 0.0F;
  std::int32_t outlineWorkRectX = 0;
  std::int32_t outlineWorkRectY = 0;
  std::int32_t outlineWorkRectWidth = 0;
  std::int32_t outlineWorkRectHeight = 0;
  float outlineWorkAreaPercent = 0.0F;
  std::uint32_t outlineMaskDrawCalls = 0;
  std::uint32_t outlineDilationDrawCalls = 0;
  std::uint32_t outlineCompositeDrawCalls = 0;
  std::uint32_t outlineUploadBytes = 0;
  bool outlineGpuTimingAvailable = false;
  float outlineGpuMilliseconds = 0.0F;
  std::uint32_t outlinePasses = 0;
  bool outlineCompositeEnabled = false;
  bool geometryOutlineFallbackUsed = false;
  NativeOutlineFallbackReason nativeOutlineFallbackReason =
    NativeOutlineFallbackReason::None;
  std::uint32_t sceneColorWidth = 0;
  std::uint32_t sceneColorHeight = 0;
  std::uint32_t sceneColorFormat = 0;
  std::uint32_t sceneCompositePasses = 0;
  std::uint32_t bloomWidth = 0;
  std::uint32_t bloomHeight = 0;
  std::uint32_t bloomPasses = 0;
  bool sceneCompositeEnabled = false;
  bool bloomEnabled = false;
  bool directPresentEligible = false;
  bool directPresentUsed = false;
  std::string directPresentFallbackReason;
  std::string directPresentFormat;
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
  std::uint32_t visibleProceduralBoxPlayers = 0;
  std::uint32_t culledProceduralBoxPlayers = 0;
  std::uint32_t playerBoxInstancesSubmitted = 0;
  std::uint32_t playerBoxInstanceUploadBytes = 0;
  std::uint32_t sharedCubeStaticGpuBytes = 0;
  std::uint32_t proceduralPlayerOpaqueBatches = 0;
  std::uint32_t proceduralPlayerOpaqueDrawCalls = 0;
  std::uint32_t proceduralPlayerOutlineMaskBatches = 0;
  std::uint32_t proceduralPlayerOutlineMaskDrawCalls = 0;
  std::uint32_t legacyCpuGeneratedPlayerVertices = 0;
  std::uint32_t legacyDynamicPlayerVertexUploadBytes = 0;
  std::uint32_t gltfPlayerModelInstances = 0;
  std::uint32_t gltfPlayerModelFrustumCulled = 0;
  std::uint32_t gltfStaticMeshGpuBytes = 0;
  std::uint32_t gltfStaticIndexGpuBytes = 0;
  std::uint64_t gltfMaterialTextureGpuBytes = 0;
  std::uint32_t gltfMaterialTextureMipLevels = 0;
  std::uint32_t gltfMaterialTextureBinds = 0;
  bool gltfAuthoredMaterialTexturesReady = false;
  bool gltfMaterialFallbackUsed = false;
  std::uint32_t gltfPoseUploadBytes = 0;
  std::uint32_t gltfBonePaletteEntriesUploaded = 0;
  std::uint32_t gltfRigidFallbackInstances = 0;
  std::uint32_t gltfGpuSkinnedInstances = 0;
  std::uint32_t gltfBodyBatches = 0;
  std::uint32_t gltfBodyDrawCalls = 0;
  std::uint32_t gltfShadowCasterInstances = 0;
  std::uint32_t gltfShadowCasterDrawCalls = 0;
  std::uint32_t gltfOutlineMaskBatches = 0;
  std::uint32_t gltfOutlineMaskDrawCalls = 0;
  std::uint32_t legacyCpuSkinnedGltfVertexUploadBytes = 0;
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
  std::uint32_t activeTransientEffects = 0;
  std::uint32_t activeMachineGunTracers = 0;
  std::uint32_t activeShotgunTracers = 0;
  std::uint32_t activeExplosionEffects = 0;
  std::uint32_t newExplosionEventsConsumed = 0;
  std::uint32_t tracerCandidates = 0;
  std::uint32_t tracerFrustumCulled = 0;
  std::uint32_t tracerInstancesSubmitted = 0;
  std::uint32_t tracerInstanceUploadBytes = 0;
  std::uint32_t tracerBatches = 0;
  std::uint32_t tracerDrawCalls = 0;
  std::uint32_t explosionCandidates = 0;
  std::uint32_t explosionFrustumCulled = 0;
  std::uint32_t explosionInstancesSubmitted = 0;
  std::uint32_t explosionInstanceUploadBytes = 0;
  std::uint32_t explosionOpaqueBatches = 0;
  std::uint32_t explosionAdditiveBatches = 0;
  std::uint32_t explosionDrawCalls = 0;
  std::uint32_t legacyWireframeExplosionDraws = 0;
  std::uint32_t legacyMachineGunShotgunVisualDraws = 0;
  std::uint32_t activeTemporaryLights = 0;
  std::uint32_t authoredPointLights = 0;
  std::uint32_t pointLightCandidates = 0;
  std::uint32_t selectedPointLights = 0;
  std::uint32_t droppedPointLights = 0;
  std::uint32_t flickeringPointLights = 0;
  std::uint32_t shadowedPointLights = 0;
  std::uint32_t activeCasings = 0;
  std::uint32_t activeImpactParticles = 0;
  std::uint32_t activeBulletDecals = 0;
  std::uint32_t transparentEffectsSubmitted = 0;
  std::string_view selectedPresentModeName = "n/a";
};

struct FrameCaptureRequest {
  std::string path;
  bool hideHud = true;
  bool hideOverlays = true;
};

struct FrameCaptureResult {
  bool requested = false;
  bool ok = false;
  std::string path;
  std::string error;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct LateViewSample {
  bool hasView = false;
  float yawRadians = 0.0F;
  float pitchRadians = 0.0F;
  std::uint64_t sampleCompletedNanoseconds = 0;
  float samplePhaseGainMilliseconds = 0.0F;
};

struct LateViewSampler {
  void* context = nullptr;
  LateViewSample (*sample)(void*) = nullptr;
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
    const IcePoolArray& icePools,
    const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
    std::span<const TransientTracer> transientTracers,
    std::span<const TransientEffect> transientEffects,
    std::uint32_t newExplosionEventsConsumed,
    const RenderSettings& settings,
    const HudRenderState& hud,
    const ConsoleRenderState& console,
    LateViewSampler lateViewSampler = {},
    const FrameCaptureRequest* captureRequest = nullptr,
    FrameCaptureResult* captureResult = nullptr
  );
  [[nodiscard]] bool setVSync(bool enabled);
  [[nodiscard]] bool setPresentMode(PresentMode mode);
  [[nodiscard]] std::string_view backendName() const;
  [[nodiscard]] std::string_view requestedBackendName() const;
  [[nodiscard]] std::string_view gpuName() const;
  [[nodiscard]] std::string_view graphicsDriverName() const;
  [[nodiscard]] std::string_view graphicsDriverVersion() const;
  [[nodiscard]] std::string_view graphicsDriverInfo() const;
  [[nodiscard]] std::string_view vulkanApiVersion() const;
  [[nodiscard]] std::string_view vulkanIcdPath() const;
  [[nodiscard]] std::string_view vulkanIcdSha256() const;
  [[nodiscard]] bool softwareRenderer() const;
  [[nodiscard]] const RendererFrameDiagnostics& lastFrameDiagnostics() const;
  void resetGpuTimingResults();
  [[nodiscard]] std::span<const GpuFrameTimingResult> takeGpuTimingResults();
  void drainGpuTimings();
  [[nodiscard]] bool hasPendingGpuTimings() const;
  [[nodiscard]] const GpuTimingAvailability& gpuTimingMetadata() const;
  void shutdown();

private:
  void* renderer_ = nullptr;
  void* gpuDevice_ = nullptr;
  void* gpuPipeline_ = nullptr;
  void* gpuPipelineWorldSurface_ = nullptr;
  void* gpuPipelineWorldIndirectCull_ = nullptr;
  void* gpuPipeline3D_ = nullptr;
  void* gpuPipeline3DTranslucent_ = nullptr;
  void* gpuPipelineInstancedMesh_ = nullptr;
  void* gpuPipelineStaticMesh_ = nullptr;
  void* gpuPipelineMaterialMesh_ = nullptr;
  void* gpuPipelineStaticMeshViewModel_ = nullptr;
  void* gpuPipelineMaterialMeshViewModel_ = nullptr;
  void* gpuPipelineGltfPlayerModel_ = nullptr;
  void* gpuPipelineGltfPlayerModelFlat_ = nullptr;
  void* gpuPipelineDepthWorld_ = nullptr;
  void* gpuPipelineDepthInstanced_ = nullptr;
  void* gpuPipelineDepthStatic_ = nullptr;
  void* gpuPipelineDepthMaterial_ = nullptr;
  void* gpuPipelineDepthGltf_ = nullptr;
  void* gpuPipelineInstancedGlow_ = nullptr;
  void* gpuPipelineOutlineClear_ = nullptr;
  void* gpuPipelineOutlineColorClear_ = nullptr;
  void* gpuPipelineOutlineMask_ = nullptr;
  void* gpuPipelineStaticMeshOutlineMask_ = nullptr;
  void* gpuPipelineMaterialMeshOutlineMask_ = nullptr;
  void* gpuPipelineGltfPlayerModelOutlineMask_ = nullptr;
  void* gpuPipelineOutlineDilation_ = nullptr;
  void* gpuPipelineOutlineComposite_ = nullptr;
  void* gpuPipelineOutlineNativeDilation_ = nullptr;
  void* gpuPipelineOutlineNativeComposite_ = nullptr;
  void* gpuPipelineSunShadowWorld_ = nullptr;
  void* gpuPipelineSunShadowStatic_ = nullptr;
  void* gpuPipelineSunShadowMaterial_ = nullptr;
  void* gpuPipelineSunShadowGltf_ = nullptr;
  void* gpuPipelinePointShadowWorld_ = nullptr;
  void* gpuPipelineBloomSource_ = nullptr;
  void* gpuPipelineBloomBlur_ = nullptr;
  void* gpuPipelineSceneComposite_ = nullptr;
  void* gpuPipelineSceneCompositeNoBloom_ = nullptr;
  void* gpuPipelineSky_ = nullptr;
  void* gpuPipelineDirectSky_ = nullptr;
  void* gpuPipelineDirectWorldSurface_ = nullptr;
  void* gpuPipelineDirectWorld_ = nullptr;
  void* gpuPipelineDirectInstancedMesh_ = nullptr;
  void* gpuPipelineDirectStaticMesh_ = nullptr;
  void* gpuPipelineDirectMaterialMesh_ = nullptr;
  void* gpuPipelineDirectGltfPlayer_ = nullptr;
  void* gpuVertexBuffer_ = nullptr;
  void* gpuTransferBuffer_ = nullptr;
  void* gpuSimpleResources_ = nullptr;
  void* gpuSkyResources_ = nullptr;
  void* gpuGltfPlayerResources_ = nullptr;
  void* gpuFontAtlas_ = nullptr;
  void* gpuHudImages_ = nullptr;
  void* gpuFontSampler_ = nullptr;
  void* gpuWorldTextureAtlas_ = nullptr;
  void* gpuStaticWorld_ = nullptr;
  void* gpuVertexScratch_ = nullptr;
  void* gpuDepthTexture_ = nullptr;
  void* gpuViewModelDepthTexture_ = nullptr;
  void* gpuMsaaColorTexture_ = nullptr;
  void* gpuSceneColorTexture_ = nullptr;
  void* gpuBloomTextureA_ = nullptr;
  void* gpuBloomTextureB_ = nullptr;
  void* gpuBloomDepthTexture_ = nullptr;
  void* gpuPostProcessSampler_ = nullptr;
  void* gpuOutlineMaskTexture_ = nullptr;
  void* gpuOutlineDilationTexture_ = nullptr;
  void* gpuOutlineDepthTexture_ = nullptr;
  void* gpuOutlineMaskSampler_ = nullptr;
  void* gpuSunShadowTexture_ = nullptr;
  void* gpuSunShadowFallbackTexture_ = nullptr;
  void* gpuSunShadowSampler_ = nullptr;
  void* gpuPointShadowTexture_ = nullptr;
  void* gpuPointShadowFallbackTexture_ = nullptr;
  void* gpuPointShadowSampler_ = nullptr;
  void* sdlHudImages_ = nullptr;
  std::uint32_t gpuDepthFormat_ = 0;
  std::uint32_t gpuDepthWidth_ = 0;
  std::uint32_t gpuDepthHeight_ = 0;
  std::uint32_t gpuViewModelDepthWidth_ = 0;
  std::uint32_t gpuViewModelDepthHeight_ = 0;
  std::uint32_t gpuMsaaColorWidth_ = 0;
  std::uint32_t gpuMsaaColorHeight_ = 0;
  std::uint32_t gpuSceneColorWidth_ = 0;
  std::uint32_t gpuSceneColorHeight_ = 0;
  std::uint32_t gpuBloomWidth_ = 0;
  std::uint32_t gpuBloomHeight_ = 0;
  std::uint32_t gpuBloomDepthWidth_ = 0;
  std::uint32_t gpuBloomDepthHeight_ = 0;
  std::uint32_t gpuSceneColorFormat_ = 0;
  std::uint32_t gpuSampleCount_ = 1;
  std::uint32_t gpuOutlineMaskWidth_ = 0;
  std::uint32_t gpuOutlineMaskHeight_ = 0;
  std::uint32_t gpuOutlineDilationWidth_ = 0;
  std::uint32_t gpuOutlineDilationHeight_ = 0;
  std::uint32_t gpuOutlineDepthWidth_ = 0;
  std::uint32_t gpuOutlineDepthHeight_ = 0;
  std::uint32_t gpuSunShadowSize_ = 0;
  std::uint64_t gpuSunShadowCacheKey_ = 0;
  std::uint32_t gpuPointShadowSize_ = 0;
  std::uint32_t gpuPointShadowLightCount_ = 0;
  std::uint64_t gpuPointShadowCacheKey_ = 0;
  void* window_ = nullptr;
  std::string backendName_ = "uninitialized";
  std::string requestedBackendName_ = "fallback";
  std::string gpuName_;
  std::string graphicsDriverName_;
  std::string graphicsDriverVersion_;
  std::string graphicsDriverInfo_;
  std::string vulkanApiVersion_;
  std::string vulkanIcdPath_;
  std::string vulkanIcdSha256_;
  GpuTimestampTiming gpuTimestampTiming_;
  RendererFrameDiagnostics lastFrameDiagnostics_ = {};
  std::chrono::steady_clock::time_point previousCameraStepUpdate_ = {};
  float previousCameraPlayerZ_ = 0.0F;
  float cameraStepOffset_ = 0.0F;
  bool gpuBackend_ = false;
  bool softwareRenderer_ = false;
  bool gpuErrorReported_ = false;
  bool hasPreviousCameraPlayerZ_ = false;
};

} // namespace lg
