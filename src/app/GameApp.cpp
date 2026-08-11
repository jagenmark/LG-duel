#include "app/GameApp.hpp"

#include "app/ClientAudio.hpp"
#include "app/ClientChat.hpp"
#include "app/ClientCvars.hpp"
#include "app/ConsoleInput.hpp"
#include "app/DeathCamera.hpp"
#include "app/GraphicsProfiles.hpp"
#include "app/HudPresentation.hpp"
#include "app/MiscMenu.hpp"
#include "app/PerfTelemetry.hpp"
#include "app/Scoreboard.hpp"
#include "app/TextInput.hpp"
#include "benchmark/Benchmark.hpp"
#include "benchmark/BenchmarkTiming.hpp"
#include "client/ClientSession.hpp"
#include "client/HitConfirmAudio.hpp"
#include "client/LocalHitFeedback.hpp"
#include "console/ConsoleConfig.hpp"
#include "console/ConsoleSystem.hpp"
#include "dev/DevControlServer.hpp"
#include "input/InputBindings.hpp"
#include "input/MouseAim.hpp"
#include "net/NetCodec.hpp"
#include "render/ChatLayout.hpp"
#include "render/CombatEffects.hpp"
#include "render/ConsoleLayout.hpp"
#include "render/GltfSkinnedModel.hpp"
#include "render/ImpactMaterials.hpp"
#include "render/OptionMenuLayout.hpp"
#include "render/Renderer.hpp"
#include "render/Scene3D.hpp"
#include "render/WeaponPresentation.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "shared/Math.hpp"
#include "shared/Sequence.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/GameplayCvars.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"
#include "sim/WeaponCatalog.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lg {
namespace {

constexpr float kHalfPi = 1.57079632679F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr int kMaxSimulationTicksPerFrame = 8;
constexpr std::uint64_t kMaximumReservedBenchmarkFramesPerSecond = 4096U;
constexpr float kDegreesToRadians = 0.01745329252F;
constexpr float kRadiansToDegrees = 57.2957795131F;
constexpr float kQ3RunRoll = 0.005F;
constexpr float kQuakeUnitsPerProjectUnit = 40.0F;
constexpr std::uint32_t kClientRailgunCooldownTicks = 188;
constexpr float kTwoPi = 6.28318530718F;
constexpr std::size_t kMaxTransientTracers = 128;
constexpr std::size_t kMaxTransientEffects = 192;
constexpr std::size_t kLocalTracerAimHistorySize = 128;
constexpr std::uint8_t kShotgunVisualPelletCount = 6;
constexpr Vec3 kRevolverGripSocket = {-0.23F, 0.0F, -0.24F};

[[nodiscard]] std::uint8_t selfDamagePercent(const ConsoleSystem& console) {
  return static_cast<std::uint8_t>(
    std::clamp(static_cast<int>(std::lround(console.getFloat("g_selfdamage"))), 0, 100)
  );
}

[[nodiscard]] bool nearlyEqualGameplayFloat(float lhs, float rhs) {
  return std::fabs(lhs - rhs) <= 0.0001F;
}

[[nodiscard]] bool nearlySameGameplayMovementTuning(
  const MovementTuning& lhs,
  const MovementTuning& rhs
) {
  return lhs.flightEnabled == rhs.flightEnabled &&
    lhs.airControlEnabled == rhs.airControlEnabled &&
    nearlyEqualGameplayFloat(lhs.groundAcceleration, rhs.groundAcceleration) &&
    nearlyEqualGameplayFloat(lhs.airAcceleration, rhs.airAcceleration) &&
    nearlyEqualGameplayFloat(lhs.groundFriction, rhs.groundFriction) &&
    nearlyEqualGameplayFloat(lhs.stopSpeed, rhs.stopSpeed) &&
    nearlyEqualGameplayFloat(lhs.maxGroundSpeed, rhs.maxGroundSpeed) &&
    nearlyEqualGameplayFloat(lhs.dashTargetSpeed, rhs.dashTargetSpeed) &&
    nearlyEqualGameplayFloat(lhs.dashMaxSpeed, rhs.dashMaxSpeed) &&
    nearlyEqualGameplayFloat(lhs.dashAcceleration, rhs.dashAcceleration) &&
    nearlyEqualGameplayFloat(lhs.dashDuration, rhs.dashDuration) &&
    nearlyEqualGameplayFloat(lhs.dashCooldown, rhs.dashCooldown) &&
    nearlyEqualGameplayFloat(lhs.dashGroundHopVelocity, rhs.dashGroundHopVelocity) &&
    nearlyEqualGameplayFloat(lhs.dashAirHopVelocity, rhs.dashAirHopVelocity) &&
    nearlyEqualGameplayFloat(lhs.flightAcceleration, rhs.flightAcceleration) &&
    nearlyEqualGameplayFloat(lhs.maxFlightSpeed, rhs.maxFlightSpeed) &&
    nearlyEqualGameplayFloat(lhs.flightDamping, rhs.flightDamping);
}

[[nodiscard]] const char* weaponSwitchingModeCvarValue(
  WeaponSwitchingMode mode
) {
  switch (mode) {
    case WeaponSwitchingMode::Ql:
      return "ql";
    case WeaponSwitchingMode::Cpma:
      return "cpma";
    case WeaponSwitchingMode::Crazy:
      return "crazy";
  }
  return "crazy";
}

[[nodiscard]] std::optional<BotAttackMode> parseBotAttackMode(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  if (value == "0" || value == "off" || value == "false") {
    return BotAttackMode::Off;
  }
  if (value == "easy") {
    return BotAttackMode::Easy;
  }
  if (value == "medium") {
    return BotAttackMode::Medium;
  }
  if (value == "hard") {
    return BotAttackMode::Hard;
  }
  return std::nullopt;
}

[[nodiscard]] const char* botAttackModeCvarValue(BotAttackMode mode) {
  switch (mode) {
    case BotAttackMode::Off:
      return "0";
    case BotAttackMode::Easy:
      return "easy";
    case BotAttackMode::Medium:
      return "medium";
    case BotAttackMode::Hard:
      return "hard";
  }
  return "0";
}

void syncGameplayCvarsFromSnapshot(
  ConsoleSystem& console,
  const ServerSnapshot& snapshot
) {
  (void)console.execute(
    std::string("set g_flight ") +
    (snapshot.movementTuning.flightEnabled ? "1" : "0")
  );
  (void)console.execute("set g_accel " + std::to_string(snapshot.movementTuning.groundAcceleration));
  (void)console.execute("set g_airaccel " + std::to_string(snapshot.movementTuning.airAcceleration));
  (void)console.execute(
    std::string("set g_aircontrol ") +
    (snapshot.movementTuning.airControlEnabled ? "1" : "0")
  );
  (void)console.execute("set g_friction " + std::to_string(snapshot.movementTuning.groundFriction));
  (void)console.execute("set g_stopspeed " + std::to_string(snapshot.movementTuning.stopSpeed));
  (void)console.execute("set g_maxspeed " + std::to_string(snapshot.movementTuning.maxGroundSpeed));
  (void)console.execute("set g_dash_targetspeed " + std::to_string(snapshot.movementTuning.dashTargetSpeed));
  (void)console.execute("set g_dash_maxspeed " + std::to_string(snapshot.movementTuning.dashMaxSpeed));
  (void)console.execute("set g_dash_accel " + std::to_string(snapshot.movementTuning.dashAcceleration));
  (void)console.execute("set g_dash_duration " + std::to_string(snapshot.movementTuning.dashDuration));
  (void)console.execute("set g_dash_cooldown " + std::to_string(snapshot.movementTuning.dashCooldown));
  (void)console.execute("set g_dash_groundhop " + std::to_string(snapshot.movementTuning.dashGroundHopVelocity));
  (void)console.execute("set g_dash_airhop " + std::to_string(snapshot.movementTuning.dashAirHopVelocity));
  (void)console.execute("set g_flightaccel " + std::to_string(snapshot.movementTuning.flightAcceleration));
  (void)console.execute("set g_flightmaxspeed " + std::to_string(snapshot.movementTuning.maxFlightSpeed));
  (void)console.execute("set g_flightdamping " + std::to_string(snapshot.movementTuning.flightDamping));
  (void)console.execute("set g_playersize_xy " + std::to_string(snapshot.playerSizeScaleXY));
  (void)console.execute("set g_playersize_z " + std::to_string(snapshot.playerSizeScaleZ));
  (void)console.execute("set g_lg_knockback " + std::to_string(snapshot.lightningKnockback));
  (void)console.execute("set g_lg_fire_hz " + std::to_string(snapshot.lightningFireHz));
  (void)console.execute("set g_rl_knockback " + std::to_string(snapshot.rocketKnockback));
  (void)console.execute("set g_knockback_time_ms " + std::to_string(snapshot.knockbackTimeMs));
  (void)console.execute("set g_sg_damage " + std::to_string(snapshot.weaponDamage.shotgunDamagePerPellet));
  (void)console.execute("set g_mg_damage " + std::to_string(snapshot.weaponDamage.machineGunDamage));
  (void)console.execute("set g_lg_damage " + std::to_string(snapshot.weaponDamage.lightningGunDamage));
  (void)console.execute("set g_fg_damage " + std::to_string(snapshot.weaponDamage.freezeGunDamage));
  (void)console.execute("set g_rg_damage " + std::to_string(snapshot.weaponDamage.railgunDamage));
  (void)console.execute("set g_rl_damage " + std::to_string(snapshot.weaponDamage.rocketLauncherDamage));
  (void)console.execute("set g_pg_damage " + std::to_string(snapshot.weaponDamage.plasmaGunDamage));
  (void)console.execute("set g_vampirism " + std::to_string(snapshot.vampirism));
  (void)console.execute("set g_selfdamage " + std::to_string(snapshot.selfDamagePercent));
  (void)console.execute("set g_healthamount " + std::to_string(snapshot.healthAmount));
  (void)console.execute(
    std::string("set g_infiniteammo ") +
    (snapshot.weaponAmmo.infiniteAmmo ? "1" : "0")
  );
  (void)console.execute(
    std::string("set g_weaponswitching ") +
    weaponSwitchingModeCvarValue(snapshot.weaponSwitchingMode)
  );
}

[[nodiscard]] ClientNetworkSimulationConfig networkSimulationConfigFromConsole(
  const ConsoleSystem& console
) {
  return ClientNetworkSimulationConfig{
    console.getInt("net_sim_latency_ms"),
    console.getInt("net_sim_jitter_ms"),
    console.getInt("net_sim_loss_percent"),
    console.getInt("net_sim_reorder_percent"),
    static_cast<std::uint32_t>(std::max(0, console.getInt("net_sim_seed"))),
  };
}

[[nodiscard]] dev::JsonValue controlPlayerStateJson(const PlayerState& player) {
  dev::JsonValue result = dev::JsonValue::objectValue();
  result.object["position"] = dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(player.position.x),
    dev::JsonValue::numberValue(player.position.y),
    dev::JsonValue::numberValue(player.position.z),
  });
  result.object["velocity"] = dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(player.velocity.x),
    dev::JsonValue::numberValue(player.velocity.y),
    dev::JsonValue::numberValue(player.velocity.z),
  });
  result.object["yaw_degrees"] =
    dev::JsonValue::numberValue(player.viewYawRadians * kRadiansToDegrees);
  result.object["pitch_degrees"] =
    dev::JsonValue::numberValue(player.viewPitchRadians * kRadiansToDegrees);
  result.object["health"] = dev::JsonValue::numberValue(player.health);
  result.object["on_ground"] = dev::JsonValue::booleanValue(player.onGround);
  result.object["crouched"] = dev::JsonValue::booleanValue(player.crouched);
  result.object["sneaking"] = dev::JsonValue::booleanValue(player.sneaking);
  return result;
}

[[nodiscard]] dev::JsonValue controlNetworkSimulationConfigJson(
  const ClientNetworkSimulationConfig& config
) {
  dev::JsonValue result = dev::JsonValue::objectValue();
  result.object["latency_ms"] = dev::JsonValue::numberValue(config.latencyMs);
  result.object["jitter_ms"] = dev::JsonValue::numberValue(config.jitterMs);
  result.object["packet_loss_percent"] =
    dev::JsonValue::numberValue(config.lossPercent);
  result.object["reorder_percent"] =
    dev::JsonValue::numberValue(config.reorderPercent);
  result.object["seed"] = dev::JsonValue::numberValue(config.seed);
  return result;
}

[[nodiscard]] std::string_view controlNetworkSimulationDirectionName(
  ClientNetworkSimDirection direction
) {
  return direction == ClientNetworkSimDirection::Outgoing ? "outgoing" : "incoming";
}

[[nodiscard]] std::string_view controlNetworkSimulationActionName(
  ClientNetworkSimAction action
) {
  switch (action) {
    case ClientNetworkSimAction::Immediate:
      return "immediate";
    case ClientNetworkSimAction::Queued:
      return "queued";
    case ClientNetworkSimAction::Dropped:
      return "dropped";
  }
  return "unknown";
}

#if LG_DUEL_HAS_SDL3
[[nodiscard]] bool isClipboardPasteKey(const SDL_KeyboardEvent& event) {
  return event.key == SDLK_V && (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

[[nodiscard]] bool isClipboardCopyKey(const SDL_KeyboardEvent& event) {
  return event.key == SDLK_C && (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

void pasteClipboardTextIntoConsole(std::string &input, std::size_t &cursorIndex,
                                   TextSelection &selection) {
  char* clipboardText = SDL_GetClipboardText();
  if (clipboardText == nullptr) {
    return;
  }
  replaceSelectionOrInsert(input, cursorIndex, selection, clipboardText,
                           TextInputFilter::Console);
  SDL_free(clipboardText);
}

void copyTextToClipboard(std::string_view text) {
  const std::string clipboardText{text};
  (void)SDL_SetClipboardText(clipboardText.c_str());
}
#endif

[[nodiscard]] DamageNumbersConfig damageNumbersConfig(
  const ConsoleSystem& console
) {
  return {
    static_cast<DamageNumbersMode>(console.getInt("r_damage_numbers_mode")),
    console.getFloat("r_damage_numbers_window"),
    console.getFloat("r_damage_numbers_duration"),
  };
}

[[nodiscard]] LocalDamageSource localDamageSourceForWeapon(Weapon weapon) {
  if (weapon == Weapon::LightningGun) {
    return LocalDamageSource::LightningGun;
  }
  if (weapon == Weapon::RocketLauncher || weapon == Weapon::GrenadeLauncher) {
    return LocalDamageSource::RocketExplosion;
  }
  return LocalDamageSource::WeaponFire;
}

[[nodiscard]] Vec3 cameraUp(float yawRadians, float pitchRadians) {
  return {
    -std::cos(yawRadians) * std::sin(pitchRadians),
    -std::sin(yawRadians) * std::sin(pitchRadians),
    std::cos(pitchRadians),
  };
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) {
  return {
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

[[nodiscard]] float firstPersonWeaponSideOffset(int weaponPosition) {
  if (weaponPosition == 1) {
    return 0.30F;
  }
  if (weaponPosition == 2) {
    return -0.30F;
  }
  return 0.0F;
}

[[nodiscard]] Vec3 hiddenWeaponVisualOrigin(const PlayerState& player) {
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 eyePosition =
    player.position + Vec3{0.0F, 0.0F, eyeHeight};
  return eyePosition +
    cameraForward(player.viewYawRadians, player.viewPitchRadians) * 0.24F -
    cameraUp(player.viewYawRadians, player.viewPitchRadians) * 0.54F;
}

struct WeaponPresentationFrame {
  Vec3 forward = {};
  Vec3 right = {};
  Vec3 up = {};
  Vec3 hand = {};
  float scale = 1.0F;
};

[[nodiscard]] float thirdPersonWeaponVisualScale(Weapon weapon) {
  switch (weapon) {
  case Weapon::LightningGun:
  case Weapon::FreezeGun:
    return 0.55F;
  case Weapon::RocketLauncher:
  case Weapon::GrenadeLauncher:
    return 0.68F;
  case Weapon::Revolver:
    return 0.45F;
  default:
    return 0.65F;
  }
}

[[nodiscard]] WeaponPresentationFrame firstPersonWeaponPresentationFrame(
  const PlayerState& player,
  int weaponPosition,
  Weapon weapon
) {
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 eyePosition =
    player.position + Vec3{0.0F, 0.0F, eyeHeight};
  WeaponPresentationFrame frame;
  frame.forward = cameraForward(player.viewYawRadians, player.viewPitchRadians);
  frame.right = yawRight(player.viewYawRadians);
  frame.up = cameraUp(player.viewYawRadians, player.viewPitchRadians);
  frame.hand =
    eyePosition +
    frame.forward * 0.32F -
    frame.up * 0.38F +
    frame.right * firstPersonWeaponSideOffset(weaponPosition);
  frame.scale = 0.50F;
  if (weapon == Weapon::MachineGun || weapon == Weapon::Shotgun) {
    frame.hand -= frame.forward * 0.10F;
  }
  return frame;
}

[[nodiscard]] WeaponPresentationFrame weaponPresentationFrame(
  const PlayerState& player,
  bool leanEnabled,
  float leanScale
) {
  constexpr CollisionBounds defaultBounds = {};
  const float radius = player.bounds.radius;
  const float halfHeight = player.bounds.halfHeight;
  const float bottom = player.position.z - halfHeight;
  const float height = halfHeight * 2.0F;
  const Vec3 forward = yawForward(player.viewYawRadians);
  const Vec3 baseRight = yawRight(player.viewYawRadians);
  const float lateralVelocity = dot(player.velocity, baseRight);
  const float rollRadians = (
    leanEnabled
      ? -lateralVelocity * kQuakeUnitsPerProjectUnit * kQ3RunRoll * leanScale
      : 0.0F
  ) * kDegreesToRadians;
  const float rollCos = std::cos(rollRadians);
  const float rollSin = std::sin(rollRadians);
  const Vec3 worldUp = {0.0F, 0.0F, 1.0F};
  WeaponPresentationFrame frame;
  frame.forward = forward;
  frame.right = normalize((baseRight * rollCos) + (worldUp * rollSin));
  frame.up = normalize((worldUp * rollCos) - (baseRight * rollSin));
  frame.scale = std::clamp(
    (
      radius / defaultBounds.radius +
      halfHeight / defaultBounds.halfHeight
    ) * 0.5F,
    0.65F,
    1.8F
  );
  const bool airborne =
    !player.onGround && player.movementMode == MovementMode::Airborne;
  const float handForwardOffset = airborne ? 0.22F : 0.18F;
  const float handHeightRatio = airborne ? 0.56F : 0.53F;
  frame.hand =
    player.position +
    frame.forward * (radius * handForwardOffset) +
    frame.right * (radius * 0.84F) +
    frame.up * ((bottom + height * handHeightRatio) - player.position.z);
  return frame;
}

[[nodiscard]] Vec3 weaponPresentationPoint(
  const WeaponPresentationFrame& frame,
  float forward,
  float right,
  float up
) {
  return frame.hand +
    frame.forward * (forward * frame.scale) +
    frame.right * (right * frame.scale) +
    frame.up * (up * frame.scale);
}

[[nodiscard]] Vec3 remoteWeaponPresentationPoint(
  Vec3 fallback,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  Weapon weapon,
  const RenderSettings& settings,
  float forward,
  float right,
  float up
) {
  if (playerIndex >= remotePlayers.size() || !remotePlayers[playerIndex].visible) {
    return fallback;
  }
  const RemotePlayerView& remote = remotePlayers[playerIndex];
  const bool leanEnabled = remote.teammate
    ? settings.teammateLeanEnabled
    : settings.enemyLeanEnabled;
  const float leanScale = remote.teammate
    ? settings.teammateLeanScale
    : settings.enemyLeanScale;
  WeaponPresentationFrame frame =
    weaponPresentationFrame(remote.player, leanEnabled, leanScale);
  frame.scale *= thirdPersonWeaponVisualScale(weapon);
  if (weapon == Weapon::Revolver) {
    forward -= kRevolverGripSocket.x;
    right -= kRevolverGripSocket.y;
    up -= kRevolverGripSocket.z;
  } else if (weapon == Weapon::RocketLauncher) {
    const Vec3 grip = rocketLauncherGripSocket();
    forward -= grip.x;
    right -= grip.y;
    up -= grip.z;
  }
  return weaponPresentationPoint(frame, forward, right, up);
}

[[nodiscard]] Vec3 machineGunTracerSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    if (!settings.showOwnWeapons) {
      return hiddenWeaponVisualOrigin(localPlayer);
    }
    return firstPersonMachineGunMuzzlePosition(localPlayer, settings);
  }
  const Vec3 muzzle = machineGunMuzzleSocket();
  return remoteWeaponPresentationPoint(
    fire.start,
    remotePlayers,
    playerIndex,
    Weapon::MachineGun,
    settings,
    muzzle.x,
    muzzle.y,
    muzzle.z
  );
}

[[nodiscard]] Vec3 machineGunCasingSource(
  Vec3 fallback,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return settings.showOwnWeapons
      ? firstPersonMachineGunCasingEjectPosition(localPlayer, settings)
      : hiddenWeaponVisualOrigin(localPlayer);
  }
  const Vec3 socket = machineGunCasingEjectSocket();
  return remoteWeaponPresentationPoint(
    fallback,
    remotePlayers,
    playerIndex,
    Weapon::MachineGun,
    settings,
    socket.x,
    socket.y,
    socket.z
  );
}

[[nodiscard]] Vec3 shotgunTracerSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    if (!settings.showOwnWeapons) {
      return hiddenWeaponVisualOrigin(localPlayer);
    }
    return weaponPresentationPoint(
      firstPersonWeaponPresentationFrame(
        localPlayer,
        settings.weaponPosition,
        Weapon::Shotgun
      ),
      0.46F,
      0.0F,
      0.12F
    );
  }
  return remoteWeaponPresentationPoint(
    fire.start,
    remotePlayers,
    playerIndex,
    Weapon::Shotgun,
    settings,
    0.62F,
    0.0F,
    0.115F
  );
}

[[nodiscard]] Vec3 freezeGunMuzzleSource(
  Vec3 fallback,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return settings.showOwnWeapons
      ? firstPersonFreezeGunMuzzlePosition(localPlayer, settings)
      : hiddenWeaponVisualOrigin(localPlayer);
  }
  return remoteWeaponPresentationPoint(
    fallback,
    remotePlayers,
    playerIndex,
    Weapon::FreezeGun,
    settings,
    1.0F,
    0.0F,
    0.105F
  );
}

[[nodiscard]] Vec3 rocketLauncherMuzzleSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
);

struct TransientTracerStore {
  std::array<TransientTracer, kMaxTransientTracers> tracers = {};
  std::array<bool, kMaxTransientTracers> active = {};
  std::array<bool, kMaxTransientTracers> followMuzzle = {};
  std::array<std::uint8_t, kMaxTransientTracers> followPlayerIndex = {};
  std::array<Weapon, kMaxTransientTracers> followWeapon = {};
  std::array<std::uint32_t, kMaxTransientTracers> followSeed = {};
  std::array<std::uint8_t, kMaxTransientTracers> expiryGraceState = {};
  std::array<TransientEffect, kMaxTransientEffects> effects = {};
  std::array<bool, kMaxTransientEffects> effectActive = {};
  std::array<std::uint8_t, kMaxTransientEffects> effectExpiryGraceState = {};
  CombatEffectEventHistory eventHistory = {};
  std::uint32_t explosionEventsConsumedThisFrame = 0;

  void update(float dt) {
    explosionEventsConsumedThisFrame = 0;
    const float elapsed = std::max(0.0F, dt);
    for (std::size_t index = 0; index < tracers.size(); ++index) {
      if (!active[index]) {
        continue;
      }
      if (expiryGraceState[index] == 2U) {
        active[index] = false;
        continue;
      }
      const float ageBeforeUpdate = tracers[index].ageSeconds;
      tracers[index].ageSeconds += elapsed;
      if (tracers[index].ageSeconds >= tracers[index].lifetimeSeconds) {
        if (
          expiryGraceState[index] == 1U &&
          ageBeforeUpdate <= 0.0001F
        ) {
          expiryGraceState[index] = 2U;
          tracers[index].ageSeconds =
            tracers[index].lifetimeSeconds * 0.35F;
          continue;
        }
        active[index] = false;
      } else if (expiryGraceState[index] == 1U) {
        expiryGraceState[index] = 0U;
      }
    }
    for (std::size_t index = 0; index < effects.size(); ++index) {
      if (!effectActive[index]) {
        continue;
      }
      if (effectExpiryGraceState[index] == 2U) {
        effectActive[index] = false;
        continue;
      }
      const float ageBeforeUpdate = effects[index].ageSeconds;
      effects[index].ageSeconds += elapsed;
      if (effects[index].ageSeconds >= effects[index].lifetimeSeconds) {
        if (
          effectExpiryGraceState[index] == 1U &&
          ageBeforeUpdate <= 0.0001F
        ) {
          effectExpiryGraceState[index] = 2U;
          effects[index].ageSeconds =
            effects[index].lifetimeSeconds * 0.35F;
          continue;
        }
        effectActive[index] = false;
      } else if (effectExpiryGraceState[index] == 1U) {
        effectExpiryGraceState[index] = 0U;
      }
    }
  }

  [[nodiscard]] bool acceptWeaponFire(
    std::uint8_t playerIndex,
    Weapon weapon,
    std::uint32_t visualSeed
  ) {
    return eventHistory.acceptWeaponFire(playerIndex, weapon, visualSeed);
  }

  [[nodiscard]] bool acceptExplosion(
    std::uint8_t ownerIndex,
    std::uint32_t sequence
  ) {
    if (!eventHistory.acceptExplosion(ownerIndex, sequence)) {
      return false;
    }
    ++explosionEventsConsumedThisFrame;
    return true;
  }

  void add(
    const TransientTracer& tracer,
    bool followMuzzle,
    Weapon weapon,
    std::uint32_t seed,
    std::uint8_t playerIndex
  ) {
    std::size_t slot = tracers.size();
    for (std::size_t index = 0; index < active.size(); ++index) {
      if (!active[index]) {
        slot = index;
        break;
      }
    }
    if (slot == tracers.size()) {
      slot = 0;
      for (std::size_t index = 1; index < active.size(); ++index) {
        if (tracers[index].ageSeconds > tracers[slot].ageSeconds) {
          slot = index;
        }
      }
    }
    tracers[slot] = tracer;
    active[slot] = true;
    this->followMuzzle[slot] = followMuzzle;
    followWeapon[slot] = weapon;
    followSeed[slot] = seed;
    followPlayerIndex[slot] = playerIndex;
    // Keep the compact Rocket flash for one extra submitted frame if a long
    // frame crosses its whole lifetime. This keeps the cue readable at low
    // frame rates without a time trail or an unbounded store.
    expiryGraceState[slot] =
      tracer.style == TracerStyle::RocketLauncherMuzzleFlash ? 1U : 0U;
  }

  void addEffect(const TransientEffect& effect) {
    std::size_t slot = effects.size();
    for (std::size_t index = 0; index < effectActive.size(); ++index) {
      if (!effectActive[index]) {
        slot = index;
        break;
      }
    }
    if (slot == effects.size()) {
      slot = 0;
      for (std::size_t index = 1; index < effectActive.size(); ++index) {
        if (effects[index].ageSeconds > effects[slot].ageSeconds) {
          slot = index;
        }
      }
    }
    effects[slot] = effect;
    effectActive[slot] = true;
    effectExpiryGraceState[slot] =
      effect.type == TransientEffectType::RocketExplosionFlash ||
        effect.type == TransientEffectType::RocketExplosionCore ||
        effect.type == TransientEffectType::RocketExplosionHalo
      ? 1U
      : 0U;
  }

  void fillActive(
    std::vector<TransientTracer>& result,
    const PlayerState& localPlayer,
    const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
    const RenderSettings& settings
  ) const {
    result.clear();
    result.reserve(tracers.size());
    for (std::size_t index = 0; index < tracers.size(); ++index) {
      if (active[index]) {
        TransientTracer tracer = tracers[index];
        if (
          settings.combatEffectsQuality <= 0 &&
          (
            tracer.style == TracerStyle::MachineGun ||
            tracer.style == TracerStyle::MachineGunMuzzleFlash
          )
        ) {
          continue;
        }
        // Only the short muzzle-flash cues stay attached to the live weapon.
        // Fired tracer endpoints are captured when the shot is accepted.
        if (
          followMuzzle[index] &&
          (
            tracer.style == TracerStyle::MachineGunMuzzleFlash ||
            tracer.style == TracerStyle::RevolverMuzzleFlash ||
            tracer.style == TracerStyle::RocketLauncherMuzzleFlash
          )
        ) {
          const Vec3 oldStart = tracer.start;
          const Vec3 oldDelta = tracer.end - oldStart;
          const std::size_t playerIndex = followPlayerIndex[index];
          const bool local =
            playerIndex == static_cast<std::size_t>(settings.localPlayerIndex);
          bool followsCurrentDirection = false;
          if (local && !settings.showOwnWeapons) {
            tracer.start = hiddenWeaponVisualOrigin(localPlayer);
          } else if (followWeapon[index] == Weapon::MachineGun) {
            if (local) {
              tracer.start = firstPersonMachineGunMuzzlePosition(
                localPlayer,
                settings
              );
            } else {
              WeaponFireResult attachmentFire;
              attachmentFire.start = oldStart;
              tracer.start = machineGunTracerSource(
                attachmentFire,
                localPlayer,
                remotePlayers,
                playerIndex,
                settings
              );
            }
          } else if (followWeapon[index] == Weapon::Shotgun) {
            tracer.start = weaponPresentationPoint(
              firstPersonWeaponPresentationFrame(
                localPlayer,
                settings.weaponPosition,
                Weapon::Shotgun
              ),
              0.46F,
              0.0F,
              0.12F
            );
          } else if (followWeapon[index] == Weapon::RocketLauncher) {
            WeaponFireResult attachmentFire;
            attachmentFire.start = oldStart;
            tracer.start = rocketLauncherMuzzleSource(
              attachmentFire,
              localPlayer,
              remotePlayers,
              playerIndex,
              settings
            );
            const PlayerState& sourcePlayer =
              local || playerIndex >= remotePlayers.size()
                ? localPlayer
                : remotePlayers[playerIndex].player;
            const Vec3 currentDirection = cameraForward(
              sourcePlayer.viewYawRadians,
              sourcePlayer.viewPitchRadians
            );
            tracer.end =
              tracer.start + currentDirection * length(oldDelta);
            followsCurrentDirection = true;
          } else if (followWeapon[index] == Weapon::Revolver) {
            tracer.start = firstPersonRevolverMuzzlePosition(
              localPlayer,
              settings
            );
          }
          if (!followsCurrentDirection) {
            tracer.end += tracer.start - oldStart;
          }
        }
        result.push_back(tracer);
      }
    }
  }

  void fillActiveEffects(std::vector<TransientEffect>& result) const {
    result.clear();
    result.reserve(effects.size());
    for (std::size_t index = 0; index < effects.size(); ++index) {
      if (effectActive[index]) {
        result.push_back(effects[index]);
      }
    }
  }

};

struct LocalTracerAim {
  std::uint32_t sequence = 0;
  float yawRadians = 0.0F;
  float pitchRadians = 0.0F;
  bool active = false;
};

struct LocalTracerAimHistory {
  std::array<LocalTracerAim, kLocalTracerAimHistorySize> entries = {};
  std::size_t next = 0;

  void remember(const UserCommand& command) {
    entries[next] = {
      command.sequence,
      command.viewYawRadians,
      command.viewPitchRadians,
      true,
    };
    next = (next + 1U) % entries.size();
  }

  [[nodiscard]] bool find(
    std::uint32_t sequence,
    float& yawRadians,
    float& pitchRadians
  ) const {
    for (const LocalTracerAim& entry : entries) {
      if (entry.active && entry.sequence == sequence) {
        yawRadians = entry.yawRadians;
        pitchRadians = entry.pitchRadians;
        return true;
      }
    }
    return false;
  }
};

[[nodiscard]] RenderColor tracerColor(Weapon weapon, std::uint32_t seed) {
  const std::uint8_t variation =
    static_cast<std::uint8_t>((seed * 17U + 31U) & 23U);
  if (weapon == Weapon::Shotgun) {
    return {
      static_cast<std::uint8_t>(218U + variation),
      static_cast<std::uint8_t>(166U + variation),
      92,
      150,
    };
  }
  return {
    255,
    static_cast<std::uint8_t>(210U + variation),
    118,
    185,
  };
}

[[nodiscard]] float localTracerVisualRange(const WeaponFireResult& fire) {
  const float fireDistance = length(fire.end - fire.start);
  if (std::isfinite(fireDistance) && fireDistance > 0.001F) {
    // Server cameraForward endpoints can rebuild a fraction short after client normalization.
    return fireDistance + 0.001F;
  }
  return fire.weapon == Weapon::Shotgun ? 18.0F : 100.0F;
}

[[nodiscard]] WeaponFireResult localPerspectiveTracerFire(
  const Arena& arena,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  const PlayerState& localPlayer,
  const LocalTracerAimHistory& localAimHistory
) {
  float yawRadians = localPlayer.viewYawRadians;
  float pitchRadians = localPlayer.viewPitchRadians;
  (void)localAimHistory.find(fire.visualSeed, yawRadians, pitchRadians);

  WeaponFireResult visualFire = fire;
  const Vec3 direction = cameraForward(yawRadians, pitchRadians);
  const WorldTrace trace =
    traceWorld(arena, fire.start, direction, localTracerVisualRange(fire));
  visualFire.start = visualStart;
  visualFire.end = trace.end;
  return visualFire;
}

void spawnMachineGunTracer(
  TransientTracerStore& store,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  std::uint8_t playerIndex
) {
  const Vec3 direction = normalize(fire.end - fire.start);
  if (length(direction) <= 0.0001F) {
    return;
  }
  const float width = 0.010F + static_cast<float>(fire.visualSeed & 3U) * 0.0015F;
  store.add({
    visualStart + direction * 0.22F,
    fire.end,
    0.0F,
    0.036F,
    width,
    tracerColor(Weapon::MachineGun, fire.visualSeed),
    fire.visualSeed,
    TracerStyle::MachineGun,
  }, false, Weapon::MachineGun, fire.visualSeed, playerIndex);
  // The flash shares the tracer's deduplicated fire event and muzzle-follow
  // metadata, so it cannot repeat when the same snapshot is rendered twice.
  store.add({
    visualStart,
    visualStart + direction * 0.16F,
    0.0F,
    kMachineGunMuzzleFlashDurationSeconds,
    0.045F,
    {255, 188, 76, 235},
    fire.visualSeed,
    TracerStyle::MachineGunMuzzleFlash,
  }, true, Weapon::MachineGun, fire.visualSeed, playerIndex);
}

[[nodiscard]] Vec3 rocketLauncherMuzzleSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return settings.showOwnWeapons
      ? firstPersonRocketLauncherMuzzlePosition(localPlayer, settings)
      : hiddenWeaponVisualOrigin(localPlayer);
  }
  return playerIndex < remotePlayers.size() &&
      remotePlayers[playerIndex].visible
    ? remoteRocketLauncherMuzzlePosition(remotePlayers[playerIndex], settings)
    : fire.start;
}

void spawnShotgunTracers(
  TransientTracerStore& store,
  const Arena& arena,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  std::uint8_t playerIndex
) {
  const Vec3 forward = normalize(fire.end - fire.start);
  if (length(forward) <= 0.0001F) {
    return;
  }
  Vec3 right = normalize(cross(forward, Vec3{0.0F, 0.0F, 1.0F}));
  if (length(right) <= 0.0001F) {
    right = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(right, forward));
  const std::uint8_t pelletCount =
    std::max<std::uint8_t>(1U, fire.pelletCount);
  const std::uint8_t visualCount =
    std::min<std::uint8_t>(kShotgunVisualPelletCount, pelletCount);
  constexpr float kVisualSpreadRadians = 0.0872665F;
  constexpr float kMaxShotgunTracerLength = 7.0F;
  for (std::uint8_t visualIndex = 0; visualIndex < visualCount; ++visualIndex) {
    const std::uint8_t pelletIndex = visualCount <= 1U
      ? 0U
      : static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(visualIndex) *
           static_cast<std::uint16_t>(pelletCount - 1U)) /
          static_cast<std::uint16_t>(visualCount - 1U)
        );
    const Vec3 direction = shotgunPelletDirection(
      forward,
      right,
      up,
      kVisualSpreadRadians,
      pelletIndex
    );
    if (length(direction) <= 0.0001F) {
      continue;
    }
    const WorldTrace trace =
      traceWorld(arena, visualStart, direction, kMaxShotgunTracerLength);
    const float visibleLength = std::min(trace.distance, kMaxShotgunTracerLength);
    store.add({
      visualStart + direction * 0.18F,
      visualStart + direction * visibleLength,
      0.0F,
      0.046F,
      visualIndex == 0 ? 0.010F : 0.007F,
      tracerColor(Weapon::Shotgun, fire.visualSeed + visualIndex),
      fire.visualSeed + visualIndex,
      TracerStyle::Shotgun,
    }, false, Weapon::Shotgun, fire.visualSeed, playerIndex);
  }
}

[[nodiscard]] bool spawnWorldSurfaceImpact(
  CombatEffects& combatEffects,
  const Arena& arena,
  const WeaponFireResult& fire,
  SurfaceImpactWeapon weapon,
  std::span<const ImpactSurfaceMaterial> impactSurfaceMaterials,
  const CombatEffectsTuning& effectsTuning
) {
  if (effectsTuning.quality <= 0) {
    return false;
  }
  const Vec3 direction = normalize(fire.end - fire.start);
  if (length(direction) <= 0.0001F) {
    return false;
  }
  const WorldTrace trace = traceWorld(
    arena,
    fire.start,
    direction,
    localTracerVisualRange(fire)
  );
  if (!trace.hit || fire.hit) {
    return false;
  }
  combatEffects.spawnSurfaceImpact(
    {
      trace.end,
      trace.normal,
      direction,
      impactSurfaceCategory(trace, impactSurfaceMaterials),
      weapon,
      fire.visualSeed,
    },
    effectsTuning
  );
  return true;
}

[[nodiscard]] bool spawnFreezeGunPulse(
  CombatEffects& combatEffects,
  const Arena& arena,
  const LightningGunResult& beam,
  Vec3 muzzlePosition,
  std::uint8_t ownerIndex,
  std::uint32_t visualSeed,
  std::span<const ImpactSurfaceMaterial> impactSurfaceMaterials,
  const CombatEffectsTuning& effectsTuning
) {
  if (effectsTuning.quality <= 0) {
    return false;
  }
  const Vec3 direction = normalize(beam.end - beam.start);
  if (length(direction) <= 0.0001F) {
    return false;
  }
  const float range = length(beam.end - beam.start);
  const WorldTrace impactTrace = traceWorld(
    arena,
    beam.start,
    direction,
    std::isfinite(range) && range > 0.001F ? range : 18.0F
  );
  combatEffects.spawnFreezeGunPulse(
    {
      muzzlePosition,
      direction,
      impactTrace.end,
      impactTrace.normal,
      direction,
      impactSurfaceCategory(impactTrace, impactSurfaceMaterials),
      visualSeed,
      ownerIndex,
      impactTrace.hit && !beam.hit,
    },
    effectsTuning
  );
  return impactTrace.hit && !beam.hit;
}

void spawnRocketLauncherMuzzleFlash(
  TransientTracerStore& store,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  bool followMuzzle,
  std::uint8_t playerIndex
) {
  const Vec3 direction = normalize(fire.end - fire.start);
  if (length(direction) <= 0.0001F) {
    return;
  }
  store.add({
    visualStart,
    visualStart + direction * 0.22F,
    0.0F,
    0.068F,
    0.070F,
    {246, 92, 42, 238},
    fire.visualSeed,
    TracerStyle::RocketLauncherMuzzleFlash,
  }, followMuzzle, Weapon::RocketLauncher, fire.visualSeed, playerIndex);
}

[[nodiscard]] Vec3 revolverMuzzleSource(
  const WeaponFireResult& fire,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  std::size_t playerIndex,
  const RenderSettings& settings
) {
  if (playerIndex == static_cast<std::size_t>(settings.localPlayerIndex)) {
    return settings.showOwnWeapons
      ? firstPersonRevolverMuzzlePosition(localPlayer, settings)
      : hiddenWeaponVisualOrigin(localPlayer);
  }
  const Vec3 muzzle = revolverMuzzleSocket();
  return remoteWeaponPresentationPoint(
    fire.start,
    remotePlayers,
    playerIndex,
    Weapon::Revolver,
    settings,
    muzzle.x,
    muzzle.y,
    muzzle.z
  );
}

void spawnRevolverMuzzleFlash(
  TransientTracerStore& store,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  bool followLocalMuzzle,
  std::uint8_t playerIndex
) {
  const Vec3 direction = normalize(fire.end - fire.start);
  if (length(direction) <= 0.0001F) {
    return;
  }
  store.add({
    visualStart,
    visualStart + direction * 0.22F,
    0.0F,
    0.052F,
    0.062F,
    {255, 212, 118, 245},
    fire.visualSeed,
    TracerStyle::RevolverMuzzleFlash,
  }, followLocalMuzzle, Weapon::Revolver, fire.visualSeed, playerIndex);
}

struct LocalSurfaceImpactFrame {
  bool active = false;
  Weapon weapon = Weapon::MachineGun;
};

[[nodiscard]] std::string_view surfaceImpactCaptureWeaponName(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun: return "machine_gun";
  case Weapon::Shotgun: return "shotgun";
  case Weapon::Railgun: return "railgun";
  case Weapon::Revolver: return "revolver";
  case Weapon::FreezeGun: return "freeze_gun";
  default: return "unknown";
  }
}

void consumeTracerWeaponFires(
  TransientTracerStore& store,
  CombatEffects& combatEffects,
  const Arena& arena,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const LocalTracerAimHistory& localAimHistory,
  const RenderSettings& settings,
  const CombatEffectsTuning& effectsTuning,
  std::span<const ImpactSurfaceMaterial> impactSurfaceMaterials,
  bool ownsPresentedSubject,
  LocalSurfaceImpactFrame& localSurfaceImpact
) {
  for (std::size_t playerIndex = 0; playerIndex < weaponFires.size(); ++playerIndex) {
    const WeaponFireResult& fire = weaponFires[playerIndex];
    if (
      !fire.fired ||
      (
        fire.weapon != Weapon::MachineGun &&
        fire.weapon != Weapon::Shotgun &&
        fire.weapon != Weapon::Railgun &&
        fire.weapon != Weapon::Revolver &&
        fire.weapon != Weapon::RocketLauncher
      )
    ) {
      continue;
    }
    const std::uint8_t eventPlayer = static_cast<std::uint8_t>(playerIndex);
    if (!store.acceptWeaponFire(eventPlayer, fire.weapon, fire.visualSeed)) {
      continue;
    }
    const bool localEvent =
      playerIndex == static_cast<std::size_t>(settings.localPlayerIndex);
    if (fire.weapon == Weapon::MachineGun) {
      const Vec3 visualStart = machineGunTracerSource(
        fire,
        localPlayer,
        remotePlayers,
        playerIndex,
        settings
      );
      const WeaponFireResult visualFire = localEvent && ownsPresentedSubject
        ? localPerspectiveTracerFire(
            arena,
            fire,
            visualStart,
            localPlayer,
            localAimHistory
          )
        : fire;
      if (effectsTuning.quality > 0) {
        spawnMachineGunTracer(
          store,
          visualFire,
          visualStart,
          eventPlayer
        );
        const Vec3 shotDirection = normalize(visualFire.end - visualFire.start);
        const WorldTrace impactTrace = traceWorld(
          arena,
          fire.start,
          normalize(fire.end - fire.start),
          localTracerVisualRange(fire)
        );
        const PlayerState& sourcePlayer =
          localEvent || playerIndex >= remotePlayers.size()
            ? localPlayer
            : remotePlayers[playerIndex].player;
        const Vec3 muzzleRight = yawRight(sourcePlayer.viewYawRadians);
        const Vec3 muzzleUp = cameraUp(
          sourcePlayer.viewYawRadians,
          sourcePlayer.viewPitchRadians
        );
        combatEffects.spawnMachineGunShot(
          {
            visualStart,
            shotDirection,
            muzzleRight,
            muzzleUp,
            machineGunCasingSource(
              visualStart,
              localPlayer,
              remotePlayers,
              playerIndex,
              settings
            ),
            sourcePlayer.velocity,
            impactTrace.end,
            impactTrace.normal,
            shotDirection,
            impactSurfaceCategory(impactTrace, impactSurfaceMaterials),
            fire.visualSeed,
            eventPlayer,
            impactTrace.hit && !fire.hit,
          },
          effectsTuning
        );
        if (localEvent && impactTrace.hit && !fire.hit) {
          localSurfaceImpact = {true, Weapon::MachineGun};
        }
      }
    } else if (fire.weapon == Weapon::Shotgun) {
      const Vec3 visualStart = shotgunTracerSource(
        fire,
        localPlayer,
        remotePlayers,
        playerIndex,
        settings
      );
      const WeaponFireResult visualFire = localEvent && ownsPresentedSubject
        ? localPerspectiveTracerFire(
            arena,
            fire,
            visualStart,
            localPlayer,
            localAimHistory
          )
        : fire;
      spawnShotgunTracers(
        store,
        arena,
        visualFire,
        visualStart,
        eventPlayer
      );
      const bool spawnedSurfaceImpact = spawnWorldSurfaceImpact(
        combatEffects,
        arena,
        fire,
        SurfaceImpactWeapon::Shotgun,
        impactSurfaceMaterials,
        effectsTuning
      );
      if (localEvent && spawnedSurfaceImpact) {
        localSurfaceImpact = {true, Weapon::Shotgun};
      }
    } else if (fire.weapon == Weapon::RocketLauncher) {
      const Vec3 visualStart = rocketLauncherMuzzleSource(
        fire,
        localPlayer,
        remotePlayers,
        playerIndex,
        settings
      );
      spawnRocketLauncherMuzzleFlash(
        store,
        fire,
        visualStart,
        true,
        eventPlayer
      );
      const PlayerState& sourcePlayer =
        localEvent || playerIndex >= remotePlayers.size()
          ? localPlayer
          : remotePlayers[playerIndex].player;
      combatEffects.spawnRocketLauncherShot(
        {
          visualStart,
          normalize(fire.end - fire.start),
          cameraUp(
            sourcePlayer.viewYawRadians,
            sourcePlayer.viewPitchRadians
          ),
          fire.visualSeed,
          eventPlayer,
        },
        effectsTuning
      );
    } else if (fire.weapon == Weapon::Revolver) {
      const Vec3 visualStart = revolverMuzzleSource(
        fire,
        localPlayer,
        remotePlayers,
        playerIndex,
        settings
      );
      spawnRevolverMuzzleFlash(
        store,
        fire,
        visualStart,
        localEvent,
        eventPlayer
      );
      const bool spawnedSurfaceImpact = spawnWorldSurfaceImpact(
        combatEffects,
        arena,
        fire,
        SurfaceImpactWeapon::Revolver,
        impactSurfaceMaterials,
        effectsTuning
      );
      if (localEvent && spawnedSurfaceImpact) {
        localSurfaceImpact = {true, Weapon::Revolver};
      }
    } else {
      const bool spawnedSurfaceImpact = spawnWorldSurfaceImpact(
        combatEffects,
        arena,
        fire,
        SurfaceImpactWeapon::Precision,
        impactSurfaceMaterials,
        effectsTuning
      );
      if (localEvent && spawnedSurfaceImpact) {
        localSurfaceImpact = {true, Weapon::Railgun};
      }
    }
  }
}

[[nodiscard]] float explosionPresentationRadius(float radius, Weapon weapon) {
  const float fallback = weapon == Weapon::PlasmaGun ? 0.45F : 3.0F;
  const float finiteRadius = std::isfinite(radius) && radius > 0.0F
    ? radius
    : fallback;
  if (weapon == Weapon::PlasmaGun) {
    return std::clamp(finiteRadius, 0.25F, 0.9F);
  }
  return std::clamp(finiteRadius, 0.75F, 3.6F);
}

void spawnExplosionEffects(
  TransientTracerStore& store,
  CombatEffects& combatEffects,
  const CombatEffectsTuning& effectsTuning,
  const RocketExplosionResult& explosion
) {
  if (
    !std::isfinite(explosion.position.x) ||
    !std::isfinite(explosion.position.y) ||
    !std::isfinite(explosion.position.z)
  ) {
    return;
  }
  const float radius = explosionPresentationRadius(explosion.radius, explosion.weapon);
  const std::uint32_t seed = explosion.sequence * 1103515245U + 12345U;
  if (explosion.weapon == Weapon::PlasmaGun) {
    store.addEffect({TransientEffectType::PlasmaExplosionFlash, explosion.position, 0.0F, 0.04F, radius * 0.35F, radius * 0.72F, {122, 255, 184, 210}, seed});
    store.addEffect({TransientEffectType::PlasmaExplosionCore, explosion.position, 0.0F, 0.14F, radius * 0.28F, radius * 0.95F, {76, 248, 210, 185}, seed + 1U});
    store.addEffect({TransientEffectType::PlasmaExplosionHalo, explosion.position, 0.0F, 0.10F, radius * 0.55F, radius * 1.25F, {64, 255, 168, 88}, seed + 2U});
    return;
  }
  if (explosion.weapon == Weapon::GrenadeLauncher) {
    store.addEffect({TransientEffectType::GrenadeExplosionFlash, explosion.position, 0.0F, 0.05F, radius * 0.22F, radius * 0.58F, {255, 224, 104, 220}, seed});
    store.addEffect({TransientEffectType::GrenadeExplosionCore, explosion.position, 0.0F, 0.20F, radius * 0.25F, radius * 1.05F, {255, 178, 66, 190}, seed + 1U});
    return;
  }
  store.addEffect({
    TransientEffectType::RocketExplosionFlash,
    explosion.position,
    0.0F,
    0.040F,
    radius * 0.18F,
    radius * 0.46F,
    {255, 239, 174, 238},
    seed,
  });
  store.addEffect({
    TransientEffectType::RocketExplosionCore,
    explosion.position,
    0.0F,
    0.135F,
    radius * 0.20F,
    radius * 0.82F,
    {246, 104, 62, 210},
    seed + 1U,
  });
  if (effectsTuning.quality > 0) {
    store.addEffect({
      TransientEffectType::RocketExplosionHalo,
      explosion.position,
      0.0F,
      0.080F,
      radius * 0.62F,
      radius * 1.12F,
      {255, 140, 76, 58},
      seed + 2U,
    });
  }
  combatEffects.spawnRocketExplosion(
    {explosion.position, radius, seed},
    effectsTuning
  );
}

void consumeExplosionEvents(
  TransientTracerStore& store,
  CombatEffects& combatEffects,
  const CombatEffectsTuning& effectsTuning,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& explosions
) {
  for (std::size_t owner = 0; owner < explosions.size(); ++owner) {
    const RocketExplosionResult& explosion = explosions[owner];
    if (!explosion.active) {
      continue;
    }
    const std::uint8_t eventOwner = static_cast<std::uint8_t>(owner);
    if (!store.acceptExplosion(eventOwner, explosion.sequence)) {
      continue;
    }
    spawnExplosionEffects(store, combatEffects, effectsTuning, explosion);
  }
}

struct LocalInputState {
  int forward = 0;
  int back = 0;
  int left = 0;
  int right = 0;
  int up = 0;
  int down = 0;
  int sneak = 0;
  int attack = 0;
  int dash = 0;

  float mouseDeltaX = 0.0F;
  float mouseDeltaY = 0.0F;
};

struct PresentationViewState {
  float yawRadians = 0.0F;
  float pitchRadians = 0.0F;
  bool initialized = false;
};

#if LG_DUEL_HAS_SDL3
[[nodiscard]] std::uint64_t steadyClockNanoseconds() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()
    ).count()
  );
}

struct LateMouseSampleContext {
  SDL_Window* window = nullptr;
  PresentationViewState* presentationView = nullptr;
  MouseAimSettings aimSettings = {};
  float earlyMouseDeltaX = 0.0F;
  float earlyMouseDeltaY = 0.0F;
  float frameSeconds = 0.0F;
  std::uint64_t earlySampleNanoseconds = 0;
  float viewPitchBeforeEarlySample = 0.0F;
  float* pendingViewModelMouseDeltaX = nullptr;
  float* pendingViewModelMouseDeltaY = nullptr;
  bool applyToView = false;
};

[[nodiscard]] LateViewSample sampleLateMouseView(void* rawContext) {
  auto* context = static_cast<LateMouseSampleContext*>(rawContext);
  if (context == nullptr || context->window == nullptr) {
    return {};
  }

  SDL_PumpEvents();
  float lateMouseDeltaX = 0.0F;
  float lateMouseDeltaY = 0.0F;
  (void)SDL_GetRelativeMouseState(
    &lateMouseDeltaX,
    &lateMouseDeltaY
  );
  // Keep pumped motion queued. The next gameplay event pass ignores its
  // relative counts, while menus and text fields still need its position.

  LateViewSample sample;
  const bool windowFocused =
    (SDL_GetWindowFlags(context->window) & SDL_WINDOW_INPUT_FOCUS) != 0;
  if (
    context->applyToView &&
    windowFocused &&
    context->presentationView != nullptr &&
    context->presentationView->initialized
  ) {
    const MouseAimDelta correction = quakeLiveLateMouseAimCorrection(
      context->earlyMouseDeltaX,
      context->earlyMouseDeltaY,
      lateMouseDeltaX,
      lateMouseDeltaY,
      context->frameSeconds,
      context->aimSettings,
      context->viewPitchBeforeEarlySample,
      -kMaxPitchRadians,
      kMaxPitchRadians
    );
    context->presentationView->yawRadians -= correction.yawRadians;
    context->presentationView->pitchRadians = clamp(
      context->presentationView->pitchRadians - correction.pitchRadians,
      -kMaxPitchRadians,
      kMaxPitchRadians
    );
    if (context->pendingViewModelMouseDeltaX != nullptr) {
      *context->pendingViewModelMouseDeltaX += lateMouseDeltaX;
    }
    if (context->pendingViewModelMouseDeltaY != nullptr) {
      *context->pendingViewModelMouseDeltaY += lateMouseDeltaY;
    }
    sample.hasView = true;
    sample.yawRadians = context->presentationView->yawRadians;
    sample.pitchRadians = context->presentationView->pitchRadians;
  }

  sample.sampleCompletedNanoseconds = steadyClockNanoseconds();
  if (
    context->earlySampleNanoseconds != 0 &&
    sample.sampleCompletedNanoseconds >= context->earlySampleNanoseconds
  ) {
    sample.samplePhaseGainMilliseconds = static_cast<float>(
      sample.sampleCompletedNanoseconds - context->earlySampleNanoseconds
    ) / 1'000'000.0F;
  }
  return sample;
}
#endif

struct FrameTimeSummary {
  float averageMilliseconds = 0.0F;
  float p50Milliseconds = 0.0F;
  float p95Milliseconds = 0.0F;
  float p99Milliseconds = 0.0F;
  float maxMilliseconds = 0.0F;
};

struct FrameTimeHistory {
  static constexpr std::size_t kSampleCount = 512;

  std::array<float, kSampleCount> samples = {};
  std::size_t nextSample = 0;
  std::size_t sampleCount = 0;
  std::array<float, kSampleCount> sortedSamples = {};

  void push(float milliseconds) {
    samples[nextSample] = milliseconds;
    nextSample = (nextSample + 1U) % samples.size();
    sampleCount = std::min(sampleCount + 1U, samples.size());
  }

  [[nodiscard]] FrameTimeSummary summarize() {
    if (sampleCount == 0) {
      return {};
    }

    float sum = 0.0F;
    for (std::size_t index = 0; index < sampleCount; ++index) {
      const float sample = samples[index];
      sortedSamples[index] = sample;
      sum += sample;
    }
    std::sort(sortedSamples.begin(), sortedSamples.begin() + sampleCount);
    const auto percentile =
      [&](float fraction) {
        const std::size_t index = std::min(
          sampleCount - 1U,
          static_cast<std::size_t>(
            std::round(fraction * static_cast<float>(sampleCount - 1U))
          )
        );
        return sortedSamples[index];
      };

    return {
      sum / static_cast<float>(sampleCount),
      percentile(0.50F),
      percentile(0.95F),
      percentile(0.99F),
      sortedSamples[sampleCount - 1U],
    };
  }
};

[[nodiscard]] PerfSample perfSampleFromFrame(
  float frameMilliseconds,
  const RendererFrameDiagnostics& renderDiagnostics,
  const SnapshotDiagnostics& snapshotDiagnostics
) {
  PerfSample sample;
  sample.frameMilliseconds = frameMilliseconds;
  sample.sceneBuildMilliseconds = renderDiagnostics.sceneBuildMilliseconds;
  sample.gpuVertexUploadMilliseconds =
    renderDiagnostics.gpuVertexUploadMilliseconds;
  sample.swapchainAcquireMilliseconds =
    renderDiagnostics.swapchainAcquireMilliseconds;
  sample.worldDrawIssueMilliseconds =
    renderDiagnostics.worldDrawIssueMilliseconds;
  sample.submitMilliseconds = renderDiagnostics.submitMilliseconds;
  sample.totalRenderMilliseconds =
    renderDiagnostics.totalRenderMilliseconds;
  sample.lateMouseSampleMilliseconds =
    renderDiagnostics.lateMouseSampleMilliseconds;
  sample.mouseSampleToSubmitMilliseconds =
    renderDiagnostics.mouseSampleToSubmitMilliseconds;
  sample.mouseSamplePhaseGainMilliseconds =
    renderDiagnostics.mouseSamplePhaseGainMilliseconds;
  sample.lateMouseSampleEnabled =
    renderDiagnostics.lateMouseSampleEnabled;
  sample.lateMouseSampleApplied =
    renderDiagnostics.lateMouseSampleApplied;
  sample.dynamicOpaqueVertices = renderDiagnostics.dynamicOpaqueVertices;
  sample.dynamicTranslucentVertices =
    renderDiagnostics.dynamicTranslucentVertices;
  sample.totalUploadedVertices = renderDiagnostics.totalUploadedVertices;
  sample.dynamicTriangles = renderDiagnostics.dynamicTriangles;
  sample.worldSourceTriangles = renderDiagnostics.worldSourceTriangles;
  sample.worldRenderedTriangles = renderDiagnostics.worldRenderedTriangles;
  sample.worldSubmittedTriangles = renderDiagnostics.worldSubmittedTriangles;
  sample.worldDuplicateTrianglesCulled =
    renderDiagnostics.worldDuplicateTrianglesCulled;
  sample.worldVertexCount = renderDiagnostics.worldVertexCount;
  sample.worldDrawCalls = renderDiagnostics.worldDrawCalls;
  sample.worldSubmittedRanges = renderDiagnostics.worldSubmittedRanges;
  sample.worldTotalChunks = renderDiagnostics.worldTotalChunks;
  sample.worldVisibleChunks = renderDiagnostics.worldVisibleChunks;
  sample.worldCulledChunks = renderDiagnostics.worldCulledChunks;
  sample.worldVisibilityTestedNodes =
    renderDiagnostics.worldVisibilityTestedNodes;
  sample.worldVisibilityQueryMilliseconds =
    renderDiagnostics.worldVisibilityQueryMilliseconds;
  sample.gpuDepthBits = renderDiagnostics.gpuDepthBits;
  sample.worldLoadedTextures = renderDiagnostics.worldLoadedTextures;
  sample.worldMissingTextures = renderDiagnostics.worldMissingTextures;
  sample.worldReferencedMaterials = renderDiagnostics.worldReferencedMaterials;
  sample.worldMaxTextureMipLevels = renderDiagnostics.worldMaxTextureMipLevels;
  sample.worldTextureFilter = renderDiagnostics.worldTextureFilter;
  sample.worldRequestedTextureAnisotropy =
    renderDiagnostics.worldRequestedTextureAnisotropy;
  sample.worldAppliedTextureAnisotropy =
    renderDiagnostics.worldAppliedTextureAnisotropy;
  sample.worldTextureLodBias = renderDiagnostics.worldTextureLodBias;
  sample.visibleRemotePlayers = renderDiagnostics.visibleRemotePlayers;
  sample.remoteBodyModelsBuilt = renderDiagnostics.remoteBodyModelsBuilt;
  sample.remoteWeaponModelsBuilt = renderDiagnostics.remoteWeaponModelsBuilt;
  sample.playerOutlinesBuilt = renderDiagnostics.playerOutlinesBuilt;
  sample.normalPlayerBodyDynamicVertices =
    renderDiagnostics.normalPlayerBodyDynamicVertices;
  sample.geometryOutlineDynamicVertices =
    renderDiagnostics.geometryOutlineDynamicVertices;
  sample.outlinedPlayers = renderDiagnostics.outlinedPlayers;
  sample.outlineStyle = renderDiagnostics.outlineStyle;
  sample.outlineMaskWidth = renderDiagnostics.outlineMaskWidth;
  sample.outlineMaskHeight = renderDiagnostics.outlineMaskHeight;
  sample.outlineWorkWidth = renderDiagnostics.outlineWorkWidth;
  sample.outlineWorkHeight = renderDiagnostics.outlineWorkHeight;
  sample.outlineWorkScale = renderDiagnostics.outlineWorkScale;
  sample.outlineWorkRectX = renderDiagnostics.outlineWorkRectX;
  sample.outlineWorkRectY = renderDiagnostics.outlineWorkRectY;
  sample.outlineWorkRectWidth = renderDiagnostics.outlineWorkRectWidth;
  sample.outlineWorkRectHeight = renderDiagnostics.outlineWorkRectHeight;
  sample.outlineWorkAreaPercent = renderDiagnostics.outlineWorkAreaPercent;
  sample.outlineMaskDrawCalls = renderDiagnostics.outlineMaskDrawCalls;
  sample.outlineDilationDrawCalls =
    renderDiagnostics.outlineDilationDrawCalls;
  sample.outlineCompositeDrawCalls =
    renderDiagnostics.outlineCompositeDrawCalls;
  sample.outlineUploadBytes = renderDiagnostics.outlineUploadBytes;
  sample.outlineGpuTimingAvailable =
    renderDiagnostics.outlineGpuTimingAvailable;
  sample.outlineGpuMilliseconds = renderDiagnostics.outlineGpuMilliseconds;
  sample.outlinePasses = renderDiagnostics.outlinePasses;
  sample.outlineCompositeEnabled = renderDiagnostics.outlineCompositeEnabled;
  sample.geometryOutlineFallbackUsed =
    renderDiagnostics.geometryOutlineFallbackUsed;
  sample.nativeOutlineFallbackReason = static_cast<std::uint8_t>(
    renderDiagnostics.nativeOutlineFallbackReason
  );
  sample.remoteWeaponCandidates = renderDiagnostics.remoteWeaponCandidates;
  sample.remoteWeaponsFrustumCulled =
    renderDiagnostics.remoteWeaponsFrustumCulled;
  sample.remoteWeaponInstances = renderDiagnostics.remoteWeaponInstances;
  sample.remoteWeaponInstanceUploadBytes =
    renderDiagnostics.remoteWeaponInstanceUploadBytes;
  sample.remoteWeaponBatches = renderDiagnostics.remoteWeaponBatches;
  sample.remoteWeaponDrawCalls = renderDiagnostics.remoteWeaponDrawCalls;
  sample.legacyRemoteWeaponDynamicVertices =
    renderDiagnostics.legacyRemoteWeaponDynamicVertices;
  sample.gltfPlayerModelInstances =
    renderDiagnostics.gltfPlayerModelInstances;
  sample.gltfPlayerModelFrustumCulled =
    renderDiagnostics.gltfPlayerModelFrustumCulled;
  sample.gltfStaticMeshGpuBytes =
    renderDiagnostics.gltfStaticMeshGpuBytes;
  sample.gltfStaticIndexGpuBytes =
    renderDiagnostics.gltfStaticIndexGpuBytes;
  sample.gltfMaterialTextureGpuBytes =
    renderDiagnostics.gltfMaterialTextureGpuBytes;
  sample.gltfMaterialTextureMipLevels =
    renderDiagnostics.gltfMaterialTextureMipLevels;
  sample.gltfMaterialTextureBinds =
    renderDiagnostics.gltfMaterialTextureBinds;
  sample.gltfAuthoredMaterialTexturesReady =
    renderDiagnostics.gltfAuthoredMaterialTexturesReady;
  sample.gltfMaterialFallbackUsed =
    renderDiagnostics.gltfMaterialFallbackUsed;
  sample.gltfPoseUploadBytes =
    renderDiagnostics.gltfPoseUploadBytes;
  sample.gltfBonePaletteEntriesUploaded =
    renderDiagnostics.gltfBonePaletteEntriesUploaded;
  sample.gltfRigidFallbackInstances =
    renderDiagnostics.gltfRigidFallbackInstances;
  sample.gltfGpuSkinnedInstances =
    renderDiagnostics.gltfGpuSkinnedInstances;
  sample.gltfBodyBatches = renderDiagnostics.gltfBodyBatches;
  sample.gltfBodyDrawCalls = renderDiagnostics.gltfBodyDrawCalls;
  sample.gltfOutlineMaskBatches =
    renderDiagnostics.gltfOutlineMaskBatches;
  sample.gltfOutlineMaskDrawCalls =
    renderDiagnostics.gltfOutlineMaskDrawCalls;
  sample.legacyCpuSkinnedGltfVertexUploadBytes =
    renderDiagnostics.legacyCpuSkinnedGltfVertexUploadBytes;
  sample.firstPersonViewModelDrawCalls =
    renderDiagnostics.firstPersonViewModelDrawCalls;
  sample.firstPersonViewModelDynamicVertices =
    renderDiagnostics.firstPersonViewModelDynamicVertices;
  sample.projectilesActive = renderDiagnostics.projectilesActive;
  sample.projectilesFrustumCulled = renderDiagnostics.projectilesFrustumCulled;
  sample.projectilesRendered = renderDiagnostics.projectilesRendered;
  sample.plasmaInstances = renderDiagnostics.plasmaInstances;
  sample.rocketInstances = renderDiagnostics.rocketInstances;
  sample.grenadeInstances = renderDiagnostics.grenadeInstances;
  sample.projectileCoreInstances = renderDiagnostics.projectileCoreInstances;
  sample.projectileGlowInstances = renderDiagnostics.projectileGlowInstances;
  sample.opaqueProjectileBatches = renderDiagnostics.opaqueProjectileBatches;
  sample.additiveProjectileBatches = renderDiagnostics.additiveProjectileBatches;
  sample.projectileInstanceUploadBytes =
    renderDiagnostics.projectileInstanceUploadBytes;
  sample.projectileMeshDrawCalls = renderDiagnostics.projectileMeshDrawCalls;
  sample.projectileGlowDrawCalls = renderDiagnostics.projectileGlowDrawCalls;
  sample.legacyProjectileDynamicVertices =
    renderDiagnostics.legacyProjectileDynamicVertices;
  sample.activeTransientEffects = renderDiagnostics.activeTransientEffects;
  sample.activeMachineGunTracers = renderDiagnostics.activeMachineGunTracers;
  sample.activeShotgunTracers = renderDiagnostics.activeShotgunTracers;
  sample.activeExplosionEffects = renderDiagnostics.activeExplosionEffects;
  sample.activeTemporaryLights = renderDiagnostics.activeTemporaryLights;
  sample.activeCasings = renderDiagnostics.activeCasings;
  sample.activeImpactParticles = renderDiagnostics.activeImpactParticles;
  sample.activeBulletDecals = renderDiagnostics.activeBulletDecals;
  sample.transparentEffectsSubmitted =
    renderDiagnostics.transparentEffectsSubmitted;
  sample.newExplosionEventsConsumed = renderDiagnostics.newExplosionEventsConsumed;
  sample.tracerCandidates = renderDiagnostics.tracerCandidates;
  sample.tracerFrustumCulled = renderDiagnostics.tracerFrustumCulled;
  sample.tracerInstancesSubmitted = renderDiagnostics.tracerInstancesSubmitted;
  sample.tracerInstanceUploadBytes = renderDiagnostics.tracerInstanceUploadBytes;
  sample.tracerBatches = renderDiagnostics.tracerBatches;
  sample.tracerDrawCalls = renderDiagnostics.tracerDrawCalls;
  sample.explosionCandidates = renderDiagnostics.explosionCandidates;
  sample.explosionFrustumCulled = renderDiagnostics.explosionFrustumCulled;
  sample.explosionInstancesSubmitted = renderDiagnostics.explosionInstancesSubmitted;
  sample.explosionInstanceUploadBytes =
    renderDiagnostics.explosionInstanceUploadBytes;
  sample.explosionOpaqueBatches = renderDiagnostics.explosionOpaqueBatches;
  sample.explosionAdditiveBatches = renderDiagnostics.explosionAdditiveBatches;
  sample.explosionDrawCalls = renderDiagnostics.explosionDrawCalls;
  sample.legacyWireframeExplosionDraws =
    renderDiagnostics.legacyWireframeExplosionDraws;
  sample.legacyMachineGunShotgunVisualDraws =
    renderDiagnostics.legacyMachineGunShotgunVisualDraws;
  sample.snapshot = snapshotDiagnostics;
  return sample;
}

[[nodiscard]] const char* textureFilterLabel(int filter) {
  switch (filter) {
  case 0:
    return "nearest";
  case 1:
    return "bilinear";
  default:
    return "trilinear";
  }
}

void appendPerfHudLines(
  HudRenderState& hud,
  const PerfWindowSummary& summary,
  bool detail,
  const ConsoleSystem& console
) {
  const PerfSample& latest = summary.latest;
  char text[192];
  std::snprintf(
    text,
    sizeof(text),
    "PERF frame %.2f ms | scene %.2f | upload %.2f | acquire %.2f | render %.2f",
    summary.frame.average,
    summary.sceneBuild.average,
    summary.gpuVertexUpload.average,
    summary.swapchainAcquire.average,
    summary.totalRender.average
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "native outline fallback %s",
    nativeOutlineFallbackReasonName(
      static_cast<NativeOutlineFallbackReason>(
        latest.nativeOutlineFallbackReason
      )
    )
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "combat effects: lights %u | casings %u | particles %u | decals %u | transparent %u",
    latest.activeTemporaryLights,
    latest.activeCasings,
    latest.activeImpactParticles,
    latest.activeBulletDecals,
    latest.transparentEffectsSubmitted
  );
  hud.topLeftLines.emplace_back(text);

  if (!detail) {
    return;
  }

  hud.topLeftLines.emplace_back("PERF WINDOW (recent samples)");
  std::snprintf(
    text,
    sizeof(text),
    "frame:   avg %.2f | p50 %.2f | p95 %.2f | p99 %.2f | max %.2f ms",
    summary.frame.average,
    summary.frame.p50,
    summary.frame.p95,
    summary.frame.p99,
    summary.frame.max
  );
  hud.topLeftLines.emplace_back(text);
  const auto appendMetric = [&](const char* label, const PerfMetricSummary& metric) {
    std::snprintf(
      text,
      sizeof(text),
      "%-8s avg %.2f | p95 %.2f ms",
      label,
      metric.average,
      metric.p95
    );
    hud.topLeftLines.emplace_back(text);
  };
  appendMetric("scene:", summary.sceneBuild);
  appendMetric("upload:", summary.gpuVertexUpload);
  appendMetric("acquire:", summary.swapchainAcquire);
  appendMetric("draw:", summary.worldDrawIssue);
  appendMetric("submit:", summary.submit);
  appendMetric("render:", summary.totalRender);
  std::snprintf(
    text,
    sizeof(text),
    "mouse late: %s/%s | callback avg/p95 %.3f/%.3f | to-submit %.2f/%.2f | phase %.2f/%.2f ms",
    latest.lateMouseSampleEnabled ? "on" : "off",
    latest.lateMouseSampleApplied ? "applied" : "not-applied",
    summary.lateMouseSample.average,
    summary.lateMouseSample.p95,
    summary.mouseSampleToSubmit.average,
    summary.mouseSampleToSubmit.p95,
    summary.mouseSamplePhaseGain.average,
    summary.mouseSamplePhaseGain.p95
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "dynamic: %u vertices | %u triangles",
    latest.dynamicOpaqueVertices + latest.dynamicTranslucentVertices,
    latest.dynamicTriangles
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "world: tris %u->%u | submitted %u | vertices %u | draws %u | dup culled %u",
    latest.worldSourceTriangles,
    latest.worldRenderedTriangles,
    latest.worldSubmittedTriangles,
    latest.worldVertexCount,
    latest.worldDrawCalls,
    latest.worldDuplicateTrianglesCulled
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "world BVH: chunks %u/%u | culled %u | nodes %u | ranges %u | query %.3f ms",
    latest.worldVisibleChunks,
    latest.worldTotalChunks,
    latest.worldCulledChunks,
    latest.worldVisibilityTestedNodes,
    latest.worldSubmittedRanges,
    latest.worldVisibilityQueryMilliseconds
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "textures: loaded %u/%u | missing %u | max mip %u | filter %s | aniso %d/%d | lod %+0.2f",
    latest.worldLoadedTextures,
    latest.worldReferencedMaterials,
    latest.worldMissingTextures,
    latest.worldMaxTextureMipLevels,
    textureFilterLabel(latest.worldTextureFilter),
    latest.worldAppliedTextureAnisotropy,
    latest.worldRequestedTextureAnisotropy,
    latest.worldTextureLodBias
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "projectiles: active %u | rendered %u | culled %u",
    latest.projectilesActive,
    latest.projectilesRendered,
    latest.projectilesFrustumCulled
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "projectile instances: plasma %u | rocket %u | grenade %u | glow %u",
    latest.plasmaInstances,
    latest.rocketInstances,
    latest.grenadeInstances,
    latest.projectileGlowInstances
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "projectile batches: opaque %u | additive %u | upload %.1f KB | draws %u",
    latest.opaqueProjectileBatches,
    latest.additiveProjectileBatches,
    static_cast<float>(latest.projectileInstanceUploadBytes) / 1024.0F,
    latest.projectileMeshDrawCalls + latest.projectileGlowDrawCalls
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "legacy projectile vertices %u",
    latest.legacyProjectileDynamicVertices
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "transient VFX: active %u | MG %u | SG %u | explosions %u | new exp %u",
    latest.activeTransientEffects,
    latest.activeMachineGunTracers,
    latest.activeShotgunTracers,
    latest.activeExplosionEffects,
    latest.newExplosionEventsConsumed
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "tracers: batches %u | draws %u | upload %.1f KB | legacy MG/SG draws %u",
    latest.tracerBatches,
    latest.tracerDrawCalls,
    static_cast<float>(latest.tracerInstanceUploadBytes) / 1024.0F,
    latest.legacyMachineGunShotgunVisualDraws
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "explosions: instances %u | culled %u | batches %u/%u | draws %u | upload %.1f KB",
    latest.explosionInstancesSubmitted,
    latest.explosionFrustumCulled,
    latest.explosionOpaqueBatches,
    latest.explosionAdditiveBatches,
    latest.explosionDrawCalls,
    static_cast<float>(latest.explosionInstanceUploadBytes) / 1024.0F
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "legacy explosion wireframes %u",
    latest.legacyWireframeExplosionDraws
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "remote:  visible %u | bodies %u | weapons %u | outlines %u",
    latest.visibleRemotePlayers,
    latest.remoteBodyModelsBuilt,
    latest.remoteWeaponModelsBuilt,
    latest.playerOutlinesBuilt
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "players: body verts %u | geom outline verts %u | legacy outline %d",
    latest.normalPlayerBodyDynamicVertices,
    latest.geometryOutlineDynamicVertices,
    latest.geometryOutlineFallbackUsed ? 1 : 0
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "outline: players %u | style %d | work %ux%u scale %.2f | mask %ux%u",
    latest.outlinedPlayers,
    latest.outlineStyle,
    latest.outlineWorkWidth,
    latest.outlineWorkHeight,
    latest.outlineWorkScale,
    latest.outlineMaskWidth,
    latest.outlineMaskHeight
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "outline rect: %d,%d %dx%d | %.1f%% fb | draws m/d/c %u/%u/%u | gpu timing %s",
    latest.outlineWorkRectX,
    latest.outlineWorkRectY,
    latest.outlineWorkRectWidth,
    latest.outlineWorkRectHeight,
    latest.outlineWorkAreaPercent,
    latest.outlineMaskDrawCalls,
    latest.outlineDilationDrawCalls,
    latest.outlineCompositeDrawCalls,
    latest.outlineGpuTimingAvailable ? "available" : "unavailable"
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "remote weapons: cand %u | inst %u | culled %u | batches %u | draws %u",
    latest.remoteWeaponCandidates,
    latest.remoteWeaponInstances,
    latest.remoteWeaponsFrustumCulled,
    latest.remoteWeaponBatches,
    latest.remoteWeaponDrawCalls
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "weapon uploads: remote inst %.1f KB | legacy remote verts %u | viewmodel draws %u dyn verts %u",
    static_cast<float>(latest.remoteWeaponInstanceUploadBytes) / 1024.0F,
    latest.legacyRemoteWeaponDynamicVertices,
    latest.firstPersonViewModelDrawCalls,
    latest.firstPersonViewModelDynamicVertices
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "snapshot: decode %.2f ms | apply %.2f ms | packets %u | queued %zu",
    summary.snapshotDecode.average,
    summary.snapshotApply.average,
    latest.snapshot.snapshotPacketsDecoded,
    latest.snapshot.snapshotQueueDepth
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "toggles:  remote_players %d | remote_weapons %d | outlines %d",
    console.getBool("r_draw_remote_players") ? 1 : 0,
    console.getBool("r_draw_remote_weapons") ? 1 : 0,
    console.getBool("r_draw_player_outlines") ? 1 : 0
  );
  hud.topLeftLines.emplace_back(text);
}

[[nodiscard]] const char* movementModeLabel(MovementMode mode) {
  switch (mode) {
  case MovementMode::Grounded:
    return "ground";
  case MovementMode::Airborne:
    return "air";
  case MovementMode::Flying:
    return "fly";
  }
  return "unknown";
}

void appendGroundDebugHudLines(
  HudRenderState& hud,
  const Arena& arena,
  const PlayerState& player,
  int detailLevel
) {
  if (detailLevel <= 0) {
    return;
  }

  constexpr float kGroundDebugProbeDistance = 0.08F;
  const CollisionResult groundProbe = slidePlayerArenaMove(
    arena,
    player,
    player.position,
    {0.0F, 0.0F, -kGroundDebugProbeDistance},
    1.0F
  );
  const Vec3 groundNormal =
    groundProbe.groundPlane ? groundProbe.groundNormal : Vec3{0.0F, 0.0F, 1.0F};
  const float slopeDegrees =
    std::acos(clamp(groundNormal.z, -1.0F, 1.0F)) * kRadiansToDegrees;
  const float horizontalSpeed =
    std::hypot(player.velocity.x, player.velocity.y) * kQuakeUnitsPerProjectUnit;

  char text[192];
  std::snprintf(
    text,
    sizeof(text),
    "GROUND state %s probe %s/%s slope %.1f spd %.0f",
    movementModeLabel(player.movementMode),
    groundProbe.onGround ? "walk" : "no-walk",
    groundProbe.groundPlane ? "plane" : "no-plane",
    slopeDegrees,
    horizontalSpeed
  );
  hud.topLeftLines.emplace_back(text);

  std::snprintf(
    text,
    sizeof(text),
    "GROUND pos %.3f %.3f %.3f view %.1f %.1f",
    player.position.x,
    player.position.y,
    player.position.z,
    player.viewYawRadians * kRadiansToDegrees,
    player.viewPitchRadians * kRadiansToDegrees
  );
  hud.topLeftLines.emplace_back(text);

  std::snprintf(
    text,
    sizeof(text),
    "GROUND normal %.3f %.3f %.3f",
    groundNormal.x,
    groundNormal.y,
    groundNormal.z
  );
  hud.topLeftLines.emplace_back(text);

  if (detailLevel < 2) {
    return;
  }

  const float normalVelocity = dot(player.velocity, groundNormal);
  std::snprintf(
    text,
    sizeof(text),
    "GROUND vel %.3f %.3f %.3f dotN %.3f onGround %d",
    player.velocity.x,
    player.velocity.y,
    player.velocity.z,
    normalVelocity,
    player.onGround ? 1 : 0
  );
  hud.topLeftLines.emplace_back(text);

  std::snprintf(
    text,
    sizeof(text),
    "GROUND dash active %u cooldown %u dir %.2f %.2f",
    static_cast<unsigned int>(player.dashActiveTicksRemaining),
    static_cast<unsigned int>(player.dashCooldownTicksRemaining),
    player.dashDirection.x,
    player.dashDirection.y
  );
  hud.topLeftLines.emplace_back(text);
}

#if LG_DUEL_HAS_SDL3
struct ClientConsoleState {
  bool open = false;
  std::string input;
  std::size_t cursorIndex = 0;
  std::deque<std::string> output;
  std::vector<std::string> history;
  std::size_t historyIndex = 0;
  bool hasSelection = false;
  bool selecting = false;
  std::size_t selectionAnchor = 0;
  std::size_t selectionFocus = 0;
  TextSelection inputSelection;
  bool selectingInput = false;
  std::size_t scrollRows = 0;
  ConsoleCatController cat;
};

struct ClientChatState {
  struct Message {
    std::uint8_t playerIndex = 0;
    std::string text;
    std::string speakerName;
  };

  bool inputOpen = false;
  std::string input;
  std::size_t cursorIndex = 0;
  TextSelection selection;
  bool selecting = false;
  bool hasHistorySelection = false;
  bool selectingHistory = false;
  std::size_t historySelectionAnchor = 0U;
  std::size_t historySelectionFocus = 0U;
  std::string pendingMessage;
  std::deque<Message> history;
  std::uint32_t lastSequence = 0;
  std::size_t scrollRows = 0;
  const ClientGame* sourceGame = nullptr;
  std::chrono::steady_clock::time_point visibleUntil = {};
};

struct VideoSettings {
  int fullscreenMode = 0;
  int width = 1280;
  int height = 720;
  int refreshHz = 0;
  int displayIndex = 0;
  PresentMode presentMode = PresentMode::Fifo;
};

struct VideoRuntimeState {
  VideoSettings applied = {};
  bool hasApplied = false;
  bool hasWindowedPosition = false;
  int windowedX = SDL_WINDOWPOS_CENTERED;
  int windowedY = SDL_WINDOWPOS_CENTERED;
};

struct ResolutionOption {
  int width = 0;
  int height = 0;
};

constexpr int kSettingsResetRow = 32;
constexpr int kSettingsApplyRow = 33;
constexpr int kSettingsCloseRow = 34;
constexpr int kSettingsRowCount = 35;

struct SettingsMenuState {
  bool open = false;
  int selectedRow = 0;
  std::size_t scrollRows = 0;
  int hoveredRow = -1;
  int pressedRow = -1;
  bool scrollbarDragging = false;
  float scrollbarGrabOffsetY = 0.0F;
  VideoSettings pendingVideo = {};
  int pendingMaxFps = 0;
  int pendingProfile = 1;
  float pendingRenderScale = 1.0F;
  int pendingTextureFilter = 2;
  int pendingAnisotropy = 8;
  float pendingLodBias = 0.5F;
  bool pendingFrustumCull = true;
  bool pendingWorldFrustumCull = false;
  bool pendingPlayerOutlines = true;
  int pendingOutlineMode = 2;
  int pendingOutlineStyle = 0;
  bool pendingShowConsoleCat = true;
  int pendingCombatEffects = 2;
  float pendingToneMapExposure = 1.0F;
  float pendingDisplayGamma = 1.0F;
  int pendingAtmosphereGrade = 2;
  bool pendingBloom = true;
  float pendingBloomIntensity = 0.18F;
  int pendingAntiAliasing = 1;
  int pendingSunShadows = 2;
  int pendingPointLights = 1;
  int pendingPointShadows = 1;
  bool pendingContactShadows = true;
  int pendingMaterialQuality = 1;
  int pendingPlayerRim = 1;
  bool pendingCasings = true;
  float pendingImpactParticles = 1.0F;
  int pendingDecalBudget = 128;
  VideoSettings originalVideo = {};
  int originalMaxFps = 0;
  float originalRenderScale = 1.0F;
  int originalTextureFilter = 2;
  int originalAnisotropy = 8;
  float originalLodBias = 0.5F;
  bool originalFrustumCull = true;
  bool originalWorldFrustumCull = false;
  bool originalPlayerOutlines = true;
  int originalOutlineMode = 2;
  int originalOutlineStyle = 0;
  bool originalShowConsoleCat = true;
  int originalCombatEffects = 2;
  float originalToneMapExposure = 1.0F;
  float originalDisplayGamma = 1.0F;
  int originalAtmosphereGrade = 2;
  bool originalBloom = true;
  float originalBloomIntensity = 0.18F;
  int originalAntiAliasing = 1;
  int originalSunShadows = 2;
  int originalPointLights = 1;
  int originalPointShadows = 1;
  bool originalContactShadows = true;
  int originalMaterialQuality = 1;
  int originalPlayerRim = 1;
  bool originalCasings = true;
  float originalImpactParticles = 1.0F;
  int originalDecalBudget = 128;
};

struct MiscMenuState {
  bool open = false;
  int selectedRow = 0;
  std::size_t scrollRows = 0U;
  int hoveredRow = -1;
  int pressedRow = -1;
  bool scrollbarDragging = false;
  float scrollbarGrabOffsetY = 0.0F;
};

struct LingeringWeaponFire {
  WeaponFireResult fire;
  WeaponFireResult sourceFire;
  bool active = false;
  std::chrono::steady_clock::time_point startedAt = {};
  std::chrono::steady_clock::time_point expiresAt = {};
};

struct KillFeedState {
  struct Entry {
    std::string killerName;
    std::string killedName;
    Weapon weapon = Weapon::LightningGun;
    float ageSeconds = 0.0F;
  };

  std::vector<Entry> entries;
  std::array<FragEvent, kDuelPlayerCount> previousFragEvents = {};
  std::array<bool, kDuelPlayerCount> previousFragActive = {};
  std::array<std::uint32_t, kDuelPlayerCount> previousSelfExplosionSequences = {};
};

void resetKillFeedState(KillFeedState& state) {
  state.entries.clear();
  state.previousFragEvents = {};
  state.previousFragActive = {};
  state.previousSelfExplosionSequences = {};
}

void updateKillFeedState(KillFeedState& state, float deltaSeconds) {
  constexpr float kKillFeedHoldSeconds = 4.5F;
  constexpr float kKillFeedFadeSeconds = 0.5F;
  const float maxAge = kKillFeedHoldSeconds + kKillFeedFadeSeconds;
  const float clampedDelta = std::max(0.0F, deltaSeconds);
  for (KillFeedState::Entry& entry : state.entries) {
    entry.ageSeconds += clampedDelta;
  }
  state.entries.erase(
    std::remove_if(
      state.entries.begin(),
      state.entries.end(),
      [maxAge](const KillFeedState::Entry& entry) {
        return entry.ageSeconds >= maxAge;
      }
    ),
    state.entries.end()
  );
}

void consumeKillFeedEvents(
  KillFeedState& state,
  const ServerSnapshot& snapshot
) {
  constexpr std::size_t kMaxKillFeedEntries = 8;
  for (
    std::size_t attackerIndex = 0;
    attackerIndex < kDuelPlayerCount;
    ++attackerIndex
  ) {
    const FragEvent& frag = snapshot.fragEvents[attackerIndex];
    if (!frag.active) {
      state.previousFragActive[attackerIndex] = false;
      state.previousFragEvents[attackerIndex] = {};
      continue;
    }

    const bool freshEvent =
      !state.previousFragActive[attackerIndex] ||
      !sameFragEvent(frag, state.previousFragEvents[attackerIndex]);
    state.previousFragActive[attackerIndex] = true;
    state.previousFragEvents[attackerIndex] = frag;

    if (!freshEvent || frag.targetPlayerIndex >= kDuelPlayerCount) {
      continue;
    }

    state.entries.push_back({
      snapshot.playerNames[attackerIndex],
      frag.targetPlayerIndex == attackerIndex
        ? std::string{}
        : snapshot.playerNames[frag.targetPlayerIndex],
      frag.weapon,
      0.0F,
    });
    if (state.entries.size() > kMaxKillFeedEntries) {
      state.entries.erase(state.entries.begin());
    }
  }

  for (
    std::size_t ownerIndex = 0;
    ownerIndex < kDuelPlayerCount;
    ++ownerIndex
  ) {
    const RocketExplosionResult& explosion = snapshot.rocketExplosions[ownerIndex];
    const FragEvent& frag = snapshot.fragEvents[ownerIndex];
    if (
      !explosion.active ||
      explosion.ownerDamageApplied <= 0 ||
      snapshot.players[ownerIndex].health > 0 ||
      explosion.sequence == state.previousSelfExplosionSequences[ownerIndex] ||
      (frag.active && frag.targetPlayerIndex == ownerIndex)
    ) {
      continue;
    }

    state.previousSelfExplosionSequences[ownerIndex] = explosion.sequence;
    state.entries.push_back({
      snapshot.playerNames[ownerIndex],
      std::string{},
      explosion.weapon,
      0.0F,
    });
    if (state.entries.size() > kMaxKillFeedEntries) {
      state.entries.erase(state.entries.begin());
    }
  }
}

std::vector<HudRenderState::KillFeedLine> killFeedPresentation(
  const KillFeedState& state
) {
  constexpr float kKillFeedHoldSeconds = 4.5F;
  constexpr float kKillFeedFadeSeconds = 0.5F;
  std::vector<HudRenderState::KillFeedLine> lines;
  lines.reserve(state.entries.size());
  for (const KillFeedState::Entry& entry : state.entries) {
    const float fadeAge = entry.ageSeconds - kKillFeedHoldSeconds;
    const float alpha = fadeAge <= 0.0F
      ? 1.0F
      : 1.0F - std::clamp(fadeAge / kKillFeedFadeSeconds, 0.0F, 1.0F);
    lines.push_back({
      entry.killerName,
      entry.killedName,
      entry.weapon,
      alpha,
    });
  }
  return lines;
}

void appendConsoleOutput(ClientConsoleState& state, std::string_view text) {
  state.scrollRows = 0U;
  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line)) {
    state.output.push_back(std::move(line));
  }
  while (state.output.size() > 128) {
    state.output.pop_front();
  }
}

[[nodiscard]] PresentMode presentModeFromInt(int value) {
  switch (value) {
  case 1:
    return PresentMode::Mailbox;
  case 2:
    return PresentMode::Immediate;
  default:
    return PresentMode::Fifo;
  }
}

[[nodiscard]] std::string_view presentModeName(PresentMode mode) {
  switch (mode) {
  case PresentMode::Fifo:
    return "FIFO/VSync";
  case PresentMode::Mailbox:
    return "Mailbox";
  case PresentMode::Immediate:
    return "Immediate";
  }
  return "Unknown";
}

[[nodiscard]] VideoSettings videoSettingsFromConsole(const ConsoleSystem& console) {
  return {
    console.getInt("vid_fullscreen"),
    console.getInt("vid_width"),
    console.getInt("vid_height"),
    console.getInt("vid_refresh_hz"),
    console.getInt("vid_display"),
    presentModeFromInt(console.getInt("r_present_mode")),
  };
}

[[nodiscard]] bool sameVideoSettings(
  const VideoSettings& lhs,
  const VideoSettings& rhs
) {
  return lhs.fullscreenMode == rhs.fullscreenMode &&
    lhs.width == rhs.width &&
    lhs.height == rhs.height &&
    lhs.refreshHz == rhs.refreshHz &&
    lhs.displayIndex == rhs.displayIndex &&
    lhs.presentMode == rhs.presentMode;
}

[[nodiscard]] SDL_DisplayID displayForIndex(int displayIndex, int& displayCount) {
  displayCount = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
  if (displays == nullptr || displayCount <= 0) {
    return 0;
  }
  const int clampedIndex = std::clamp(displayIndex, 0, displayCount - 1);
  const SDL_DisplayID display = displays[clampedIndex];
  SDL_free(displays);
  return display;
}

[[nodiscard]] int displayCount() {
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  if (displays != nullptr) {
    SDL_free(displays);
  }
  return std::max(0, count);
}

[[nodiscard]] std::string displayLabel(int displayIndex) {
  int count = 0;
  const SDL_DisplayID display = displayForIndex(displayIndex, count);
  std::string label =
    "Display " + std::to_string(std::max(0, std::min(displayIndex, count - 1)) + 1);
  if (display == 0) {
    return label;
  }
  const char* name = SDL_GetDisplayName(display);
  if (name != nullptr && std::string_view(name).size() > 0U) {
    label += ": ";
    label += name;
  }
  return label;
}

[[nodiscard]] float displayModeRefreshHz(const SDL_DisplayMode& mode) {
  if (mode.refresh_rate > 0.0F) {
    return mode.refresh_rate;
  }
  if (mode.refresh_rate_numerator > 0 && mode.refresh_rate_denominator > 0) {
    return static_cast<float>(mode.refresh_rate_numerator) /
      static_cast<float>(mode.refresh_rate_denominator);
  }
  return 0.0F;
}

[[nodiscard]] int roundedDisplayModeRefreshHz(const SDL_DisplayMode& mode) {
  return static_cast<int>(std::lround(displayModeRefreshHz(mode)));
}

[[nodiscard]] std::vector<ResolutionOption> resolutionOptions(
  int displayIndex,
  ResolutionOption requested
) {
  std::set<std::pair<int, int>> unique;
  int displayTotal = 0;
  const SDL_DisplayID display = displayForIndex(displayIndex, displayTotal);
  if (display != 0) {
    int modeCount = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
    for (int index = 0; modes != nullptr && index < modeCount; ++index) {
      if (modes[index] != nullptr && modes[index]->w > 0 && modes[index]->h > 0) {
        unique.emplace(modes[index]->w, modes[index]->h);
      }
    }
    if (modes != nullptr) {
      SDL_free(modes);
    }
    if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display)) {
      unique.emplace(desktop->w, desktop->h);
    }
  }
  for (const ResolutionOption preset : {
         ResolutionOption{1280, 720},
         ResolutionOption{1600, 900},
         ResolutionOption{1920, 1080},
         ResolutionOption{2560, 1440},
         ResolutionOption{3840, 2160},
       }) {
    unique.emplace(preset.width, preset.height);
  }
  unique.emplace(requested.width, requested.height);

  std::vector<ResolutionOption> options;
  options.reserve(unique.size());
  for (const auto& [width, height] : unique) {
    options.push_back({width, height});
  }
  std::sort(
    options.begin(),
    options.end(),
    [](ResolutionOption lhs, ResolutionOption rhs) {
      const int lhsArea = lhs.width * lhs.height;
      const int rhsArea = rhs.width * rhs.height;
      if (lhsArea != rhsArea) {
        return lhsArea < rhsArea;
      }
      if (lhs.width != rhs.width) {
        return lhs.width < rhs.width;
      }
      return lhs.height < rhs.height;
    }
  );
  return options;
}

[[nodiscard]] std::vector<int> refreshOptions(
  int displayIndex,
  ResolutionOption resolution,
  int requestedRefreshHz
) {
  std::set<int> unique{0};
  int displayTotal = 0;
  const SDL_DisplayID display = displayForIndex(displayIndex, displayTotal);
  if (display != 0) {
    int modeCount = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
    for (int index = 0; modes != nullptr && index < modeCount; ++index) {
      const SDL_DisplayMode* mode = modes[index];
      if (
        mode != nullptr &&
        mode->w == resolution.width &&
        mode->h == resolution.height
      ) {
        const int refreshHz = roundedDisplayModeRefreshHz(*mode);
        if (refreshHz > 0) {
          unique.insert(refreshHz);
        }
      }
    }
    if (modes != nullptr) {
      SDL_free(modes);
    }
    if (const SDL_DisplayMode* current = SDL_GetCurrentDisplayMode(display)) {
      const int refreshHz = roundedDisplayModeRefreshHz(*current);
      if (refreshHz > 0) {
        unique.insert(refreshHz);
      }
    }
  }
  if (requestedRefreshHz > 0) {
    unique.insert(requestedRefreshHz);
  }
  return {unique.begin(), unique.end()};
}

template <typename T, typename Equals>
[[nodiscard]] int optionIndex(
  const std::vector<T>& options,
  const T& current,
  Equals equals
) {
  const auto match = std::find_if(
    options.begin(),
    options.end(),
    [&](const T& option) { return equals(option, current); }
  );
  return match == options.end()
    ? 0
    : static_cast<int>(std::distance(options.begin(), match));
}

template <typename T>
[[nodiscard]] const T& wrappedOption(const std::vector<T>& options, int index) {
  const int count = static_cast<int>(options.size());
  const int wrapped = ((index % count) + count) % count;
  return options[static_cast<std::size_t>(wrapped)];
}

[[nodiscard]] const SDL_DisplayMode* chooseExclusiveDisplayMode(
  SDL_DisplayMode** modes,
  int modeCount,
  const VideoSettings& requested
) {
  const SDL_DisplayMode* best = nullptr;
  float bestScore = std::numeric_limits<float>::max();
  for (int index = 0; index < modeCount; ++index) {
    const SDL_DisplayMode* mode = modes[index];
    if (mode == nullptr || mode->w != requested.width || mode->h != requested.height) {
      continue;
    }
    const float refreshHz = displayModeRefreshHz(*mode);
    const float score = requested.refreshHz > 0
      ? std::abs(refreshHz - static_cast<float>(requested.refreshHz))
      : -refreshHz;
    if (best == nullptr || score < bestScore) {
      best = mode;
      bestScore = score;
    }
  }
  return best;
}

void appendWindowPixelSize(ClientConsoleState& consoleState, SDL_Window* window) {
  int pixelWidth = 0;
  int pixelHeight = 0;
  if (SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)) {
    appendConsoleOutput(
      consoleState,
      "video: drawable size " + std::to_string(pixelWidth) + "x" +
        std::to_string(pixelHeight)
    );
  }
}

bool applyWindowedVideoSettings(
  SDL_Window* window,
  const VideoSettings& requested,
  const VideoRuntimeState& state,
  ClientConsoleState& consoleState
) {
  bool ok = true;
  ok = SDL_SetWindowFullscreen(window, false) && ok;
  ok = SDL_SyncWindow(window) && ok;
  (void)SDL_SetWindowFullscreenMode(window, nullptr);
  (void)SDL_SetWindowBordered(window, true);
  (void)SDL_SetWindowResizable(window, true);
  ok = SDL_SetWindowSize(window, requested.width, requested.height) && ok;
  if (state.hasWindowedPosition) {
    (void)SDL_SetWindowPosition(window, state.windowedX, state.windowedY);
  }
  ok = SDL_SyncWindow(window) && ok;
  if (!ok) {
    appendConsoleOutput(consoleState, "video: failed to apply windowed mode");
  }
  appendWindowPixelSize(consoleState, window);
  return ok;
}

bool applyBorderlessVideoSettings(
  SDL_Window* window,
  const VideoSettings& requested,
  ClientConsoleState& consoleState
) {
  int displayCount = 0;
  const SDL_DisplayID display = displayForIndex(requested.displayIndex, displayCount);
  if (display == 0) {
    appendConsoleOutput(consoleState, "video: no displays found for borderless fullscreen");
    return false;
  }
  bool ok = SDL_SetWindowFullscreen(window, false);
  ok = SDL_SyncWindow(window) && ok;
  ok = SDL_SetWindowFullscreenMode(window, nullptr) && ok;
  SDL_SetWindowPosition(
    window,
    SDL_WINDOWPOS_CENTERED_DISPLAY(display),
    SDL_WINDOWPOS_CENTERED_DISPLAY(display)
  );
  ok = SDL_SetWindowFullscreen(window, true) && ok;
  ok = SDL_SyncWindow(window) && ok;
  if (!ok) {
    appendConsoleOutput(consoleState, "video: failed to apply borderless fullscreen");
  } else {
    appendConsoleOutput(
      consoleState,
      "video: borderless fullscreen on display " +
        std::to_string(std::clamp(requested.displayIndex, 0, displayCount - 1))
    );
  }
  appendWindowPixelSize(consoleState, window);
  return ok;
}

bool applyExclusiveVideoSettings(
  SDL_Window* window,
  ConsoleSystem& console,
  const VideoSettings& requested,
  ClientConsoleState& consoleState
) {
  int displayCount = 0;
  const SDL_DisplayID display = displayForIndex(requested.displayIndex, displayCount);
  if (display == 0) {
    appendConsoleOutput(consoleState, "video: no displays found for exclusive fullscreen");
    return false;
  }

  int modeCount = 0;
  SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &modeCount);
  const SDL_DisplayMode* chosenMode =
    modes != nullptr ? chooseExclusiveDisplayMode(modes, modeCount, requested) : nullptr;
  if (chosenMode == nullptr) {
    appendConsoleOutput(
      consoleState,
      "video: no exclusive mode for " + std::to_string(requested.width) + "x" +
        std::to_string(requested.height) +
        (requested.refreshHz > 0 ? "@" + std::to_string(requested.refreshHz) + "Hz" : "") +
        "; falling back to borderless fullscreen"
    );
    if (modes != nullptr) {
      SDL_free(modes);
    }
    (void)console.execute("set vid_fullscreen 1");
    VideoSettings fallback = requested;
    fallback.fullscreenMode = 1;
    return applyBorderlessVideoSettings(window, fallback, consoleState);
  }

  const float chosenRefreshHz = displayModeRefreshHz(*chosenMode);
  bool ok = SDL_SetWindowFullscreen(window, false);
  ok = SDL_SyncWindow(window) && ok;
  SDL_SetWindowPosition(
    window,
    SDL_WINDOWPOS_CENTERED_DISPLAY(display),
    SDL_WINDOWPOS_CENTERED_DISPLAY(display)
  );
  ok = SDL_SetWindowFullscreenMode(window, chosenMode) && ok;
  ok = SDL_SetWindowFullscreen(window, true) && ok;
  ok = SDL_SyncWindow(window) && ok;
  if (!ok) {
    appendConsoleOutput(
      consoleState,
      "video: failed to apply exclusive fullscreen; falling back to borderless fullscreen"
    );
    if (modes != nullptr) {
      SDL_free(modes);
    }
    (void)console.execute("set vid_fullscreen 1");
    VideoSettings fallback = requested;
    fallback.fullscreenMode = 1;
    return applyBorderlessVideoSettings(window, fallback, consoleState);
  }
  appendConsoleOutput(
    consoleState,
    "video: exclusive fullscreen " + std::to_string(chosenMode->w) + "x" +
      std::to_string(chosenMode->h) + "@" +
      std::to_string(static_cast<int>(std::lround(chosenRefreshHz))) + "Hz"
  );
  if (
    requested.refreshHz > 0 &&
    chosenRefreshHz > 0.0F &&
    std::abs(chosenRefreshHz - static_cast<float>(requested.refreshHz)) > 0.5F
  ) {
    appendConsoleOutput(
      consoleState,
      "video: requested refresh was unavailable; using closest matching resolution refresh"
    );
  }
  if (modes != nullptr) {
    SDL_free(modes);
  }
  appendWindowPixelSize(consoleState, window);
  return true;
}

bool applyVideoSettings(
  SDL_Window* window,
  Renderer& renderer,
  ConsoleSystem& console,
  VideoRuntimeState& state,
  const VideoSettings& requested,
  ClientConsoleState& consoleState
) {
  VideoSettings clamped = requested;
  clamped.fullscreenMode = std::clamp(clamped.fullscreenMode, 0, 2);
  clamped.width = std::max(320, clamped.width);
  clamped.height = std::max(200, clamped.height);
  clamped.refreshHz = std::max(0, clamped.refreshHz);
  clamped.displayIndex = std::max(0, clamped.displayIndex);

  appendConsoleOutput(
    consoleState,
    "video: applying fullscreen=" + std::to_string(clamped.fullscreenMode) +
      " size=" + std::to_string(clamped.width) + "x" +
      std::to_string(clamped.height) +
      " refresh=" + std::to_string(clamped.refreshHz) +
      " display=" + std::to_string(clamped.displayIndex) +
      " present=" + std::string(presentModeName(clamped.presentMode))
  );

  if (clamped.fullscreenMode != 0) {
    int x = 0;
    int y = 0;
    if (SDL_GetWindowPosition(window, &x, &y)) {
      state.windowedX = x;
      state.windowedY = y;
      state.hasWindowedPosition = true;
    }
  }

  bool ok = true;
  if (clamped.fullscreenMode == 0) {
    ok = applyWindowedVideoSettings(window, clamped, state, consoleState);
  } else if (clamped.fullscreenMode == 1) {
    ok = applyBorderlessVideoSettings(window, clamped, consoleState);
  } else {
    // Future GUI flow: risky exclusive resolution/refresh changes should get
    // an Apply/Revert countdown before being persisted.
    ok = applyExclusiveVideoSettings(window, console, clamped, consoleState);
    clamped = videoSettingsFromConsole(console);
  }

  if (!renderer.setPresentMode(clamped.presentMode)) {
    appendConsoleOutput(consoleState, "video: failed to change renderer present mode");
    ok = false;
  } else {
    appendConsoleOutput(
      consoleState,
      "video: requested present mode " + std::string(presentModeName(clamped.presentMode)) +
        ", active " + std::string(renderer.lastFrameDiagnostics().selectedPresentModeName)
    );
  }

  state.applied = clamped;
  state.hasApplied = true;
  return ok;
}

[[nodiscard]] std::string fullscreenModeLabel(int mode) {
  switch (mode) {
  case 1:
    return "Borderless Fullscreen";
  case 2:
    return "Exclusive Fullscreen";
  default:
    return "Windowed";
  }
}

[[nodiscard]] std::string resolutionLabel(const VideoSettings& settings) {
  return std::to_string(settings.width) + " x " + std::to_string(settings.height);
}

[[nodiscard]] std::string refreshLabel(int refreshHz) {
  return refreshHz <= 0 ? "Auto/Desktop" : std::to_string(refreshHz) + " Hz";
}

[[nodiscard]] std::string fpsLimitLabel(int maxFps) {
  return maxFps <= 0 ? "Unlimited" : std::to_string(maxFps);
}

[[nodiscard]] int presentModeInt(PresentMode mode) {
  return static_cast<int>(mode);
}

[[nodiscard]] std::string presentModeDisplayLabel(PresentMode mode) {
  switch (mode) {
  case PresentMode::Fifo:
    return "VSync / FIFO";
  case PresentMode::Mailbox:
    return "Mailbox";
  case PresentMode::Immediate:
    return "Immediate";
  }
  return "Unknown";
}

[[nodiscard]] bool settingsChanged(const SettingsMenuState& menu) {
  return !sameVideoSettings(menu.pendingVideo, menu.originalVideo) ||
    menu.pendingMaxFps != menu.originalMaxFps ||
    menu.pendingRenderScale != menu.originalRenderScale ||
    menu.pendingTextureFilter != menu.originalTextureFilter ||
    menu.pendingAnisotropy != menu.originalAnisotropy ||
    menu.pendingLodBias != menu.originalLodBias ||
    menu.pendingFrustumCull != menu.originalFrustumCull ||
    menu.pendingWorldFrustumCull != menu.originalWorldFrustumCull ||
    menu.pendingPlayerOutlines != menu.originalPlayerOutlines ||
    menu.pendingOutlineMode != menu.originalOutlineMode ||
    menu.pendingOutlineStyle != menu.originalOutlineStyle ||
    menu.pendingShowConsoleCat != menu.originalShowConsoleCat ||
    menu.pendingCombatEffects != menu.originalCombatEffects ||
    menu.pendingToneMapExposure != menu.originalToneMapExposure ||
    menu.pendingDisplayGamma != menu.originalDisplayGamma ||
    menu.pendingAtmosphereGrade != menu.originalAtmosphereGrade ||
    menu.pendingBloom != menu.originalBloom ||
    menu.pendingBloomIntensity != menu.originalBloomIntensity ||
    menu.pendingAntiAliasing != menu.originalAntiAliasing ||
    menu.pendingSunShadows != menu.originalSunShadows ||
    menu.pendingPointLights != menu.originalPointLights ||
    menu.pendingPointShadows != menu.originalPointShadows ||
    menu.pendingContactShadows != menu.originalContactShadows ||
    menu.pendingMaterialQuality != menu.originalMaterialQuality ||
    menu.pendingPlayerRim != menu.originalPlayerRim ||
    menu.pendingCasings != menu.originalCasings ||
    menu.pendingImpactParticles != menu.originalImpactParticles ||
    menu.pendingDecalBudget != menu.originalDecalBudget;
}

[[nodiscard]] int matchingGraphicsProfile(const SettingsMenuState& menu) {
  for (std::size_t index = 0; index < kGraphicsProfiles.size(); ++index) {
    const auto& values = kGraphicsProfiles[index].values;
    const auto value = [&](std::string_view name) {
      for (const GraphicsProfileValue& entry : values) if (entry.cvar == name) return entry.value;
      return std::string_view{};
    };
    if (std::abs(menu.pendingRenderScale - std::stof(std::string(value("r_render_scale")))) < 0.001F &&
        menu.pendingTextureFilter == std::stoi(std::string(value("r_texture_filter"))) &&
        menu.pendingAnisotropy == std::stoi(std::string(value("r_texture_anisotropy"))) &&
        std::abs(menu.pendingLodBias - std::stof(std::string(value("r_texture_lod_bias")))) < 0.001F &&
        (menu.pendingFrustumCull ? "1" : "0") == value("r_frustum_cull") &&
        (menu.pendingWorldFrustumCull ? "1" : "0") == value("r_world_frustum_cull") &&
        (menu.pendingPlayerOutlines ? "1" : "0") == value("r_draw_player_outlines") &&
        std::to_string(menu.pendingOutlineMode) == value("r_player_outline_mode") &&
        std::to_string(menu.pendingOutlineStyle) == value("r_player_outline_style") &&
        std::to_string(menu.pendingCombatEffects) == value("r_combat_effects") &&
        std::abs(
          menu.pendingToneMapExposure -
          std::stof(std::string(value("r_tonemap_exposure")))
        ) < 0.001F &&
        std::to_string(menu.pendingAtmosphereGrade) ==
          value("r_atmosphere_grade") &&
        (menu.pendingBloom ? "1" : "0") == value("r_bloom") &&
        std::abs(
          menu.pendingBloomIntensity -
          std::stof(std::string(value("r_bloom_intensity")))
        ) < 0.001F &&
        std::to_string(menu.pendingAntiAliasing) == value("r_antialiasing") &&
        std::to_string(menu.pendingSunShadows) == value("r_sun_shadows") &&
        std::to_string(menu.pendingPointLights) == value("r_point_lights") &&
        std::to_string(menu.pendingPointShadows) == value("r_point_shadows") &&
        (menu.pendingContactShadows ? "1" : "0") == value("r_contact_shadows") &&
        std::to_string(menu.pendingMaterialQuality) == value("r_material_quality") &&
        std::to_string(menu.pendingPlayerRim) == value("r_player_rim") &&
        (menu.pendingCasings ? "1" : "0") == value("r_casings") &&
        std::abs(
          menu.pendingImpactParticles -
          std::stof(std::string(value("r_impact_particles")))
        ) < 0.001F &&
        std::to_string(menu.pendingDecalBudget) == value("r_decals_max")) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void applyGraphicsProfile(SettingsMenuState& menu, int profile) {
  const auto& values = kGraphicsProfiles[static_cast<std::size_t>(profile)].values;
  const auto value = [&](std::string_view name) {
    for (const GraphicsProfileValue& entry : values) if (entry.cvar == name) return entry.value;
    return std::string_view{};
  };
  menu.pendingProfile = profile;
  menu.pendingRenderScale = std::stof(std::string(value("r_render_scale")));
  menu.pendingTextureFilter = std::stoi(std::string(value("r_texture_filter")));
  menu.pendingAnisotropy = std::stoi(std::string(value("r_texture_anisotropy")));
  menu.pendingLodBias = std::stof(std::string(value("r_texture_lod_bias")));
  menu.pendingFrustumCull = value("r_frustum_cull") == "1";
  menu.pendingWorldFrustumCull = value("r_world_frustum_cull") == "1";
  menu.pendingPlayerOutlines = value("r_draw_player_outlines") == "1";
  menu.pendingOutlineMode = std::stoi(std::string(value("r_player_outline_mode")));
  menu.pendingOutlineStyle = std::stoi(
    std::string(value("r_player_outline_style"))
  );
  menu.pendingCombatEffects = std::stoi(std::string(value("r_combat_effects")));
  menu.pendingToneMapExposure =
    std::stof(std::string(value("r_tonemap_exposure")));
  // Display gamma belongs to the player, not a graphics quality profile.
  menu.pendingAtmosphereGrade =
    std::stoi(std::string(value("r_atmosphere_grade")));
  menu.pendingBloom = value("r_bloom") == "1";
  menu.pendingBloomIntensity =
    std::stof(std::string(value("r_bloom_intensity")));
  menu.pendingAntiAliasing =
    std::stoi(std::string(value("r_antialiasing")));
  menu.pendingSunShadows =
    std::stoi(std::string(value("r_sun_shadows")));
  menu.pendingPointLights =
    std::stoi(std::string(value("r_point_lights")));
  menu.pendingPointShadows =
    std::stoi(std::string(value("r_point_shadows")));
  menu.pendingContactShadows = value("r_contact_shadows") == "1";
  menu.pendingMaterialQuality =
    std::stoi(std::string(value("r_material_quality")));
  menu.pendingPlayerRim =
    std::stoi(std::string(value("r_player_rim")));
  menu.pendingCasings = value("r_casings") == "1";
  menu.pendingImpactParticles =
    std::stof(std::string(value("r_impact_particles")));
  menu.pendingDecalBudget = std::stoi(std::string(value("r_decals_max")));
}

void syncSettingsMenuFromConsole(SettingsMenuState& menu, const ConsoleSystem& console) {
  menu.pendingVideo = videoSettingsFromConsole(console);
  menu.pendingMaxFps = console.getInt("r_maxfps");
  menu.pendingRenderScale = console.getFloat("r_render_scale");
  menu.pendingTextureFilter = console.getInt("r_texture_filter");
  menu.pendingAnisotropy = console.getInt("r_texture_anisotropy");
  menu.pendingLodBias = console.getFloat("r_texture_lod_bias");
  menu.pendingFrustumCull = console.getBool("r_frustum_cull");
  menu.pendingWorldFrustumCull = console.getBool("r_world_frustum_cull");
  menu.pendingPlayerOutlines = console.getBool("r_draw_player_outlines");
  menu.pendingOutlineMode = console.getInt("r_player_outline_mode");
  menu.pendingOutlineStyle = console.getInt("r_player_outline_style");
  menu.pendingShowConsoleCat = console.getBool("cl_show_console_cat");
  menu.pendingCombatEffects = console.getInt("r_combat_effects");
  menu.pendingToneMapExposure = console.getFloat("r_tonemap_exposure");
  menu.pendingDisplayGamma = console.getFloat("r_display_gamma");
  menu.pendingAtmosphereGrade = console.getInt("r_atmosphere_grade");
  menu.pendingBloom = console.getBool("r_bloom");
  menu.pendingBloomIntensity = console.getFloat("r_bloom_intensity");
  menu.pendingAntiAliasing = console.getInt("r_antialiasing");
  menu.pendingSunShadows = console.getInt("r_sun_shadows");
  menu.pendingPointLights = console.getInt("r_point_lights");
  menu.pendingPointShadows = console.getInt("r_point_shadows");
  menu.pendingContactShadows = console.getBool("r_contact_shadows");
  menu.pendingMaterialQuality = console.getInt("r_material_quality");
  menu.pendingPlayerRim = console.getInt("r_player_rim");
  menu.pendingCasings = console.getBool("r_casings");
  menu.pendingImpactParticles = console.getFloat("r_impact_particles");
  menu.pendingDecalBudget = console.getInt("r_decals_max");
  menu.originalVideo = menu.pendingVideo;
  menu.originalMaxFps = menu.pendingMaxFps;
  menu.originalRenderScale = menu.pendingRenderScale; menu.originalTextureFilter = menu.pendingTextureFilter;
  menu.originalAnisotropy = menu.pendingAnisotropy; menu.originalLodBias = menu.pendingLodBias;
  menu.originalFrustumCull = menu.pendingFrustumCull; menu.originalWorldFrustumCull = menu.pendingWorldFrustumCull;
  menu.originalPlayerOutlines = menu.pendingPlayerOutlines;
  menu.originalOutlineMode = menu.pendingOutlineMode;
  menu.originalOutlineStyle = menu.pendingOutlineStyle;
  menu.originalShowConsoleCat = menu.pendingShowConsoleCat;
  menu.originalCombatEffects = menu.pendingCombatEffects;
  menu.originalToneMapExposure = menu.pendingToneMapExposure;
  menu.originalDisplayGamma = menu.pendingDisplayGamma;
  menu.originalAtmosphereGrade = menu.pendingAtmosphereGrade;
  menu.originalBloom = menu.pendingBloom;
  menu.originalBloomIntensity = menu.pendingBloomIntensity;
  menu.originalAntiAliasing = menu.pendingAntiAliasing;
  menu.originalSunShadows = menu.pendingSunShadows;
  menu.originalPointLights = menu.pendingPointLights;
  menu.originalPointShadows = menu.pendingPointShadows;
  menu.originalContactShadows = menu.pendingContactShadows;
  menu.originalMaterialQuality = menu.pendingMaterialQuality;
  menu.originalPlayerRim = menu.pendingPlayerRim;
  menu.originalCasings = menu.pendingCasings;
  menu.originalImpactParticles = menu.pendingImpactParticles;
  menu.originalDecalBudget = menu.pendingDecalBudget;
  menu.pendingProfile = matchingGraphicsProfile(menu);
  menu.selectedRow = std::clamp(menu.selectedRow, 0, kSettingsRowCount - 1);
  menu.scrollRows = 0U;
}

void keepOptionMenuSelectionVisible(int selectedRow, std::size_t &scrollRows,
                                    std::size_t visibleRows,
                                    std::size_t itemCount) {
  visibleRows = std::max<std::size_t>(1U, visibleRows);
  const std::size_t maxScroll =
      itemCount > visibleRows ? itemCount - visibleRows : 0U;
  scrollRows = std::min(scrollRows, maxScroll);
  const std::size_t selected =
      static_cast<std::size_t>(std::max(0, selectedRow));
  if (selected < scrollRows) {
    scrollRows = selected;
  }
  if (selected >= scrollRows + visibleRows) {
    scrollRows = selected - visibleRows + 1U;
  }
  scrollRows = std::min(scrollRows, maxScroll);
}

void adjustSettingsMenuValue(SettingsMenuState& menu, int direction) {
  if (direction == 0) {
    return;
  }
  switch (menu.selectedRow) {
  case 0:
    menu.pendingVideo.fullscreenMode =
      (menu.pendingVideo.fullscreenMode + direction + 3) % 3;
    return;
  case 1: {
    const int count = std::max(1, displayCount());
    menu.pendingVideo.displayIndex =
      (menu.pendingVideo.displayIndex + direction + count) % count;
    return;
  }
  case 2: {
    const std::vector<ResolutionOption> options = resolutionOptions(
      menu.pendingVideo.displayIndex,
      {menu.pendingVideo.width, menu.pendingVideo.height}
    );
    const int index = optionIndex(
      options,
      ResolutionOption{menu.pendingVideo.width, menu.pendingVideo.height},
      [](ResolutionOption lhs, ResolutionOption rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
      }
    );
    const ResolutionOption next = wrappedOption(options, index + direction);
    menu.pendingVideo.width = next.width;
    menu.pendingVideo.height = next.height;
    menu.pendingVideo.refreshHz = 0;
    return;
  }
  case 3: {
    const std::vector<int> options = refreshOptions(
      menu.pendingVideo.displayIndex,
      {menu.pendingVideo.width, menu.pendingVideo.height},
      menu.pendingVideo.refreshHz
    );
    const int index = optionIndex(
      options,
      menu.pendingVideo.refreshHz,
      [](int lhs, int rhs) { return lhs == rhs; }
    );
    menu.pendingVideo.refreshHz = wrappedOption(options, index + direction);
    return;
  }
  case 4:
    menu.pendingVideo.presentMode =
      presentModeFromInt((presentModeInt(menu.pendingVideo.presentMode) + direction + 3) % 3);
    return;
  case 5: {
    const std::vector<int> options = {0, 60, 120, 144, 165, 240, 360, 500, 1000};
    const int index = optionIndex(
      options,
      menu.pendingMaxFps,
      [](int lhs, int rhs) { return lhs == rhs; }
    );
    menu.pendingMaxFps = wrappedOption(options, index + direction);
    return;
  }
  case 6: applyGraphicsProfile(menu, (std::max(0, menu.pendingProfile) + direction + 4) % 4); return;
  case 7: menu.pendingRenderScale = std::clamp(menu.pendingRenderScale + 0.1F * direction, 0.5F, 1.5F); return;
  case 8: menu.pendingTextureFilter = (menu.pendingTextureFilter + direction + 3) % 3; return;
  case 9: { const std::array<int, 5> values{1, 2, 4, 8, 16}; const int index = optionIndex(std::vector<int>(values.begin(), values.end()), menu.pendingAnisotropy, [](int a, int b) { return a == b; }); menu.pendingAnisotropy = values[static_cast<std::size_t>((index + direction + 5) % 5)]; return; }
  case 10: menu.pendingLodBias = std::clamp(menu.pendingLodBias + 0.25F * direction, -2.0F, 4.0F); return;
  case 11: menu.pendingFrustumCull = !menu.pendingFrustumCull; return;
  case 12: menu.pendingWorldFrustumCull = !menu.pendingWorldFrustumCull; return;
  case 13: menu.pendingPlayerOutlines = !menu.pendingPlayerOutlines; return;
  case 14: menu.pendingOutlineMode = (menu.pendingOutlineMode + direction + 3) % 3; return;
  case 15: menu.pendingShowConsoleCat = !menu.pendingShowConsoleCat; return;
  case 16:
    menu.pendingCombatEffects =
      (menu.pendingCombatEffects + direction + 3) % 3;
    return;
  case 17:
    menu.pendingToneMapExposure = std::clamp(
      menu.pendingToneMapExposure + 0.1F * static_cast<float>(direction),
      0.25F,
      4.0F
    );
    return;
  case 18:
    menu.pendingAtmosphereGrade =
      (menu.pendingAtmosphereGrade + direction + 4) % 4;
    return;
  case 19: menu.pendingBloom = !menu.pendingBloom; return;
  case 20:
    menu.pendingBloomIntensity = std::clamp(
      menu.pendingBloomIntensity + 0.05F * static_cast<float>(direction),
      0.0F,
      1.0F
    );
    return;
  case 21:
    menu.pendingAntiAliasing = (menu.pendingAntiAliasing + direction + 3) % 3;
    return;
  case 22:
    menu.pendingSunShadows = (menu.pendingSunShadows + direction + 3) % 3;
    return;
  case 23:
    menu.pendingPointLights = (menu.pendingPointLights + direction + 3) % 3;
    return;
  case 24:
    menu.pendingPointShadows = (menu.pendingPointShadows + direction + 3) % 3;
    return;
  case 25: menu.pendingContactShadows = !menu.pendingContactShadows; return;
  case 26:
    menu.pendingMaterialQuality = (menu.pendingMaterialQuality + direction + 3) % 3;
    return;
  case 27:
    menu.pendingPlayerRim = (menu.pendingPlayerRim + direction + 3) % 3;
    return;
  case 28: menu.pendingCasings = !menu.pendingCasings; return;
  case 29:
    menu.pendingImpactParticles = std::clamp(
      menu.pendingImpactParticles + 0.25F * static_cast<float>(direction),
      0.0F,
      2.0F
    );
    return;
  case 30: {
    const std::vector<int> values = {0, 32, 48, 64, 96, 128, 192, 256};
    const int index = optionIndex(
      values,
      menu.pendingDecalBudget,
      [](int lhs, int rhs) { return lhs == rhs; }
    );
    menu.pendingDecalBudget = wrappedOption(values, index + direction);
    return;
  }
  case 31:
    menu.pendingDisplayGamma = std::clamp(
      menu.pendingDisplayGamma + 0.05F * static_cast<float>(direction),
      0.50F,
      1.50F
    );
    return;
  default:
    return;
  }
}

void applySettingsMenu(ConsoleSystem& console, SettingsMenuState& menu) {
  (void)console.execute("set vid_fullscreen " + std::to_string(menu.pendingVideo.fullscreenMode));
  (void)console.execute("set vid_width " + std::to_string(menu.pendingVideo.width));
  (void)console.execute("set vid_height " + std::to_string(menu.pendingVideo.height));
  (void)console.execute("set vid_refresh_hz " + std::to_string(menu.pendingVideo.refreshHz));
  (void)console.execute("set vid_display " + std::to_string(menu.pendingVideo.displayIndex));
  (void)console.execute(
    "set r_present_mode " + std::to_string(presentModeInt(menu.pendingVideo.presentMode))
  );
  (void)console.execute("set r_maxfps " + std::to_string(menu.pendingMaxFps));
  (void)console.execute("set r_render_scale " + std::to_string(menu.pendingRenderScale));
  (void)console.execute("set r_texture_filter " + std::to_string(menu.pendingTextureFilter));
  (void)console.execute("set r_texture_anisotropy " + std::to_string(menu.pendingAnisotropy));
  (void)console.execute("set r_texture_lod_bias " + std::to_string(menu.pendingLodBias));
  (void)console.execute("set r_frustum_cull " + std::to_string(menu.pendingFrustumCull));
  (void)console.execute("set r_world_frustum_cull " + std::to_string(menu.pendingWorldFrustumCull));
  (void)console.execute("set r_draw_player_outlines " + std::to_string(menu.pendingPlayerOutlines));
  (void)console.execute("set r_player_outline_mode " + std::to_string(menu.pendingOutlineMode));
  (void)console.execute(
    "set r_player_outline_style " + std::to_string(menu.pendingOutlineStyle)
  );
  (void)console.execute(
    "set cl_show_console_cat " + std::to_string(menu.pendingShowConsoleCat ? 1 : 0)
  );
  (void)console.execute(
    "set r_combat_effects " + std::to_string(menu.pendingCombatEffects)
  );
  (void)console.execute(
    "set r_tonemap_exposure " + std::to_string(menu.pendingToneMapExposure)
  );
  (void)console.execute(
    "set r_display_gamma " + std::to_string(menu.pendingDisplayGamma)
  );
  (void)console.execute(
    "set r_atmosphere_grade " + std::to_string(menu.pendingAtmosphereGrade)
  );
  (void)console.execute("set r_bloom " + std::to_string(menu.pendingBloom ? 1 : 0));
  (void)console.execute(
    "set r_bloom_intensity " + std::to_string(menu.pendingBloomIntensity)
  );
  (void)console.execute(
    "set r_antialiasing " + std::to_string(menu.pendingAntiAliasing)
  );
  (void)console.execute(
    "set r_sun_shadows " + std::to_string(menu.pendingSunShadows)
  );
  (void)console.execute(
    "set r_point_lights " + std::to_string(menu.pendingPointLights)
  );
  (void)console.execute(
    "set r_point_shadows " + std::to_string(menu.pendingPointShadows)
  );
  (void)console.execute(
    "set r_contact_shadows " + std::to_string(menu.pendingContactShadows ? 1 : 0)
  );
  (void)console.execute(
    "set r_material_quality " + std::to_string(menu.pendingMaterialQuality)
  );
  (void)console.execute(
    "set r_player_rim " + std::to_string(menu.pendingPlayerRim)
  );
  (void)console.execute("set r_casings " + std::to_string(menu.pendingCasings ? 1 : 0));
  (void)console.execute(
    "set r_impact_particles " + std::to_string(menu.pendingImpactParticles)
  );
  (void)console.execute(
    "set r_decals_max " + std::to_string(menu.pendingDecalBudget)
  );
  menu.originalVideo = menu.pendingVideo;
  menu.originalMaxFps = menu.pendingMaxFps;
  menu.originalRenderScale = menu.pendingRenderScale; menu.originalTextureFilter = menu.pendingTextureFilter; menu.originalAnisotropy = menu.pendingAnisotropy; menu.originalLodBias = menu.pendingLodBias; menu.originalFrustumCull = menu.pendingFrustumCull; menu.originalWorldFrustumCull = menu.pendingWorldFrustumCull; menu.originalPlayerOutlines = menu.pendingPlayerOutlines; menu.originalOutlineMode = menu.pendingOutlineMode; menu.originalOutlineStyle = menu.pendingOutlineStyle;
  menu.originalShowConsoleCat = menu.pendingShowConsoleCat;
  menu.originalCombatEffects = menu.pendingCombatEffects;
  menu.originalToneMapExposure = menu.pendingToneMapExposure;
  menu.originalDisplayGamma = menu.pendingDisplayGamma;
  menu.originalAtmosphereGrade = menu.pendingAtmosphereGrade;
  menu.originalBloom = menu.pendingBloom;
  menu.originalBloomIntensity = menu.pendingBloomIntensity;
  menu.originalAntiAliasing = menu.pendingAntiAliasing;
  menu.originalSunShadows = menu.pendingSunShadows;
  menu.originalPointLights = menu.pendingPointLights;
  menu.originalPointShadows = menu.pendingPointShadows;
  menu.originalContactShadows = menu.pendingContactShadows;
  menu.originalMaterialQuality = menu.pendingMaterialQuality;
  menu.originalPlayerRim = menu.pendingPlayerRim;
  menu.originalCasings = menu.pendingCasings;
  menu.originalImpactParticles = menu.pendingImpactParticles;
  menu.originalDecalBudget = menu.pendingDecalBudget;
}

[[nodiscard]] HudRenderState::SettingsMenuItem settingsMenuItem(
  const SettingsMenuState& menu,
  int row,
  std::string label,
  std::string value,
  bool changed,
  bool command = false
) {
  return {
    std::move(label),
    std::move(value),
    menu.selectedRow == row,
    changed,
    command,
  };
}

void populateSettingsMenuRenderState(
  HudRenderState& hud,
  const SettingsMenuState& menu
) {
  if (!menu.open) {
    return;
  }
  hud.settingsOpen = true;
  hud.settingsScrollRows = menu.scrollRows;
  hud.settingsHoveredRow = menu.hoveredRow;
  hud.settingsPressedRow = menu.pressedRow;
  hud.settingsItems = {
    settingsMenuItem(
      menu,
      0,
      "Display mode",
      fullscreenModeLabel(menu.pendingVideo.fullscreenMode),
      menu.pendingVideo.fullscreenMode != menu.originalVideo.fullscreenMode
    ),
    settingsMenuItem(
      menu,
      1,
      "Display / Monitor",
      displayLabel(menu.pendingVideo.displayIndex),
      menu.pendingVideo.displayIndex != menu.originalVideo.displayIndex
    ),
    settingsMenuItem(
      menu,
      2,
      "Resolution",
      resolutionLabel(menu.pendingVideo),
      menu.pendingVideo.width != menu.originalVideo.width ||
        menu.pendingVideo.height != menu.originalVideo.height
    ),
    settingsMenuItem(
      menu,
      3,
      "Refresh rate",
      refreshLabel(menu.pendingVideo.refreshHz),
      menu.pendingVideo.refreshHz != menu.originalVideo.refreshHz
    ),
    settingsMenuItem(
      menu,
      4,
      "Presentation",
      presentModeDisplayLabel(menu.pendingVideo.presentMode),
      menu.pendingVideo.presentMode != menu.originalVideo.presentMode
    ),
    settingsMenuItem(
      menu,
      5,
      "FPS limit",
      fpsLimitLabel(menu.pendingMaxFps),
      menu.pendingMaxFps != menu.originalMaxFps
    ),
    settingsMenuItem(menu, 6, "Graphics profile", matchingGraphicsProfile(menu) >= 0 ? std::string(kGraphicsProfiles[static_cast<std::size_t>(matchingGraphicsProfile(menu))].name) : "Custom", false),
    settingsMenuItem(menu, 7, "Render scale", std::to_string(static_cast<int>(std::lround(menu.pendingRenderScale * 100.0F))) + (menu.pendingRenderScale > 1.0F ? "% Extreme / benchmark-only" : "%"), menu.pendingRenderScale != menu.originalRenderScale),
    settingsMenuItem(menu, 8, "Texture filter", menu.pendingTextureFilter == 0 ? "Nearest" : menu.pendingTextureFilter == 1 ? "Bilinear" : "Trilinear", menu.pendingTextureFilter != menu.originalTextureFilter),
    settingsMenuItem(menu, 9, "Texture anisotropy", std::to_string(menu.pendingAnisotropy) + "x", menu.pendingAnisotropy != menu.originalAnisotropy),
    settingsMenuItem(menu, 10, "Texture LOD bias", std::to_string(menu.pendingLodBias), menu.pendingLodBias != menu.originalLodBias),
    settingsMenuItem(menu, 11, "Player frustum cull", menu.pendingFrustumCull ? "On" : "Off", menu.pendingFrustumCull != menu.originalFrustumCull),
    settingsMenuItem(menu, 12, "World frustum cull", menu.pendingWorldFrustumCull ? "On" : "Off", menu.pendingWorldFrustumCull != menu.originalWorldFrustumCull),
    settingsMenuItem(menu, 13, "Player outlines", menu.pendingPlayerOutlines ? "On" : "Off", menu.pendingPlayerOutlines != menu.originalPlayerOutlines),
    settingsMenuItem(menu, 14, "Outline mode", menu.pendingOutlineMode == 0 ? "Off" : menu.pendingOutlineMode == 1 ? "Compatibility" : "Native", menu.pendingOutlineMode != menu.originalOutlineMode),
    settingsMenuItem(menu, 15, "Console cat", menu.pendingShowConsoleCat ? "Shown" : "Hidden", menu.pendingShowConsoleCat != menu.originalShowConsoleCat),
    settingsMenuItem(
      menu,
      16,
      "Combat effects / temp lights",
      menu.pendingCombatEffects == 0
        ? "Off"
        : menu.pendingCombatEffects == 1 ? "Reduced" : "Full",
      menu.pendingCombatEffects != menu.originalCombatEffects
    ),
    settingsMenuItem(
      menu,
      17,
      "Tone-map exposure",
      std::to_string(
        static_cast<int>(std::lround(menu.pendingToneMapExposure * 100.0F))
      ) + "%",
      menu.pendingToneMapExposure != menu.originalToneMapExposure
    ),
    settingsMenuItem(
      menu,
      18,
      "Atmosphere / grade",
      menu.pendingAtmosphereGrade == 0
        ? "Off"
        : menu.pendingAtmosphereGrade == 1
          ? "Low"
          : menu.pendingAtmosphereGrade == 2 ? "Default" : "High",
      menu.pendingAtmosphereGrade != menu.originalAtmosphereGrade
    ),
    settingsMenuItem(
      menu,
      19,
      "Bright-effect bloom",
      menu.pendingBloom ? "On" : "Off",
      menu.pendingBloom != menu.originalBloom
    ),
    settingsMenuItem(
      menu,
      20,
      "Bloom strength",
      std::to_string(
        static_cast<int>(std::lround(menu.pendingBloomIntensity * 100.0F))
      ) + "%",
      menu.pendingBloomIntensity != menu.originalBloomIntensity
    ),
    settingsMenuItem(
      menu,
      21,
      "Anti-aliasing",
      menu.pendingAntiAliasing == 0
        ? "Off"
        : menu.pendingAntiAliasing == 1 ? "2x MSAA" : "4x MSAA",
      menu.pendingAntiAliasing != menu.originalAntiAliasing
    ),
    settingsMenuItem(
      menu,
      22,
      "Sun shadows",
      menu.pendingSunShadows == 0
        ? "Off"
        : menu.pendingSunShadows == 1 ? "Low" : "High",
      menu.pendingSunShadows != menu.originalSunShadows
    ),
    settingsMenuItem(
      menu,
      23,
      "Live point lights",
      menu.pendingPointLights == 0
        ? "Combat only"
        : menu.pendingPointLights == 1 ? "16 lights" : "32 lights",
      menu.pendingPointLights != menu.originalPointLights
    ),
    settingsMenuItem(
      menu,
      24,
      "Cached point shadows",
      menu.pendingPointShadows == 0
        ? "Off"
        : menu.pendingPointShadows == 1 ? "1 light / 256" : "2 lights / 512",
      menu.pendingPointShadows != menu.originalPointShadows
    ),
    settingsMenuItem(
      menu,
      25,
      "Contact shadows",
      menu.pendingContactShadows ? "On" : "Off",
      menu.pendingContactShadows != menu.originalContactShadows
    ),
    settingsMenuItem(
      menu,
      26,
      "Material quality",
      menu.pendingMaterialQuality == 0
        ? "Basic"
        : menu.pendingMaterialQuality == 1 ? "Enhanced" : "High",
      menu.pendingMaterialQuality != menu.originalMaterialQuality
    ),
    settingsMenuItem(
      menu,
      27,
      "Player rim light",
      menu.pendingPlayerRim == 0
        ? "Off"
        : menu.pendingPlayerRim == 1 ? "Low" : "High",
      menu.pendingPlayerRim != menu.originalPlayerRim
    ),
    settingsMenuItem(
      menu,
      28,
      "Cartridge casings",
      menu.pendingCasings ? "On" : "Off",
      menu.pendingCasings != menu.originalCasings
    ),
    settingsMenuItem(
      menu,
      29,
      "Impact-particle density",
      std::to_string(
        static_cast<int>(std::lround(menu.pendingImpactParticles * 100.0F))
      ) + "%",
      menu.pendingImpactParticles != menu.originalImpactParticles
    ),
    settingsMenuItem(
      menu,
      30,
      "Bullet decal budget",
      std::to_string(menu.pendingDecalBudget),
      menu.pendingDecalBudget != menu.originalDecalBudget
    ),
    settingsMenuItem(
      menu,
      31,
      "Brightness / gamma",
      std::to_string(
        static_cast<int>(std::lround(menu.pendingDisplayGamma * 100.0F))
      ) + "%",
      menu.pendingDisplayGamma != menu.originalDisplayGamma
    ),
    settingsMenuItem(
      menu,
      kSettingsResetRow,
      "Reset graphics draft",
      "Default profile",
      false,
      true
    ),
    settingsMenuItem(
      menu,
      kSettingsApplyRow,
      "Apply changes",
      settingsChanged(menu) ? "Enter" : "No changes",
      settingsChanged(menu),
      true
    ),
    settingsMenuItem(
      menu,
      kSettingsCloseRow,
      "Close / Revert draft",
      "Esc",
      false,
      true
    ),
  };
  static constexpr std::array<std::string_view, kSettingsRowCount>
      settingHelp = {{
          "Chooses windowed, borderless fullscreen, or exclusive fullscreen.",
          "Chooses which monitor displays the game.",
          "Sets the render output resolution.",
          "Sets the fullscreen refresh rate or follows the desktop.",
          "Chooses how completed frames reach the display.",
          "Caps the frame rate or leaves it unlimited.",
          "Loads a full graphics preset as the current draft.",
          "Scales the 3D scene before it is shown at output resolution.",
          "Chooses nearest, bilinear, or trilinear texture filtering.",
          "Sharpens textures viewed at an angle.",
          "Offsets which texture detail level the renderer selects.",
          "Skips player models outside the camera view.",
          "Skips world chunks outside the camera view.",
          "Turns player outlines on or off.",
          "Chooses the compatibility or native outline path.",
          "Shows or hides the animated console cat.",
          "Sets combat particles and short-lived light detail.",
          "Sets the overall brightness before the final color pass.",
          "Sets the strength of the atmosphere and color grade.",
          "Adds glow around bright effects.",
          "Sets how strong the bright-effect glow appears.",
          "Smooths edges: Off, 2x MSAA, or 4x MSAA.",
          "Sets the quality of shadows cast by the sun.",
          "Sets the budget for live map point lights.",
          "Sets the quality of cached shadows from live map lights.",
          "Adds short grounding shadows to players and props.",
          "Sets world surface detail: Basic, Enhanced, or High.",
          "Sets the edge light used to make players stand out.",
          "Shows or hides spent weapon casings.",
          "Sets the amount of impact sparks and debris.",
          "Sets the maximum number of bullet marks kept in the world.",
          "Adjusts final display gamma after tone mapping and grade; 100% is neutral.",
          "Resets this draft to the default graphics profile.",
          "Applies every changed setting in this graphics draft.",
          "Closes the menu and restores the last applied settings.",
      }};
  const std::size_t selectedRow = static_cast<std::size_t>(
      std::clamp(menu.selectedRow, 0, kSettingsRowCount - 1));
  hud.settingsFooter = std::string(settingHelp[selectedRow]);
}

void populateMiscMenuRenderState(HudRenderState &hud, const MiscMenuState &menu,
                                 const ConsoleSystem &console) {
  if (!menu.open) {
    return;
  }
  hud.miscMenuOpen = true;
  hud.miscMenuScrollRows = menu.scrollRows;
  hud.miscMenuHoveredRow = menu.hoveredRow;
  hud.miscMenuPressedRow = menu.pressedRow;
  const std::vector<MiscMenuItem> items = miscMenuItems(console);
  hud.miscMenuItems.reserve(items.size());
  for (std::size_t index = 0U; index < items.size(); ++index) {
    hud.miscMenuItems.push_back(HudRenderState::SettingsMenuItem{
        items[index].label,
        items[index].value,
        menu.selectedRow == static_cast<int>(index),
        false,
        items[index].command,
    });
  }
  if (menu.selectedRow >= 0 &&
      static_cast<std::size_t>(menu.selectedRow) < items.size()) {
    hud.miscMenuFooter =
        items[static_cast<std::size_t>(menu.selectedRow)].description;
  }
}

std::string clientConfigPath() {
  char* preferencePath = SDL_GetPrefPath("LG Duel", "LG Duel");
  if (preferencePath == nullptr) {
    return "client.cfg";
  }
  std::string path = preferencePath;
  SDL_free(preferencePath);
  return path + "client.cfg";
}

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
  std::size_t position = 0;
  while ((position = text.find(from, position)) != std::string::npos) {
    text.replace(position, from.size(), to);
    position += to.size();
  }
}

std::string migrateLegacyClientCvarNames(std::string text) {
  // Preserve old user client.cfg values and binds while keeping the live cvar
  // registry on the shorter presentation toggle names.
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 16>
    kRenamedCvars{{
      {"crosshair_enable", "crosshair"},
      {"crosshair_dot_enable", "crosshair_dot"},
      {"crosshair_outline_enable", "crosshair_outline"},
      {"crosshair_hit_enable", "crosshair_hit"},
      {"crosshair_thickness", "crosshair_width"},
      {"crosshair_dot_thickness", "crosshair_dot_width"},
      {"r_beam_hit_enable", "r_beam_hit"},
      {"r_hitmarker_enable", "r_hitmarker"},
      {"r_hitmarker_thickness", "r_hitmarker_width"},
      {"r_enemy_outline_enable", "r_enemy_outline"},
      {"r_enemy_hit_enable", "r_enemy_hit"},
      {"r_enemy_health_enable", "r_enemy_health"},
      {"r_enemy_name_enable", "r_enemy_name"},
      {"r_teammate_outline_enable", "r_teammate_outline"},
      {"r_teammate_health_enable", "r_teammate_health"},
      {"r_teammate_name_enable", "r_teammate_name"},
    }};

  for (const auto& [oldName, newName] : kRenamedCvars) {
    replaceAll(text, oldName, newName);
  }
  return text;
}

void loadClientConfig(ConsoleSystem& console, const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return;
  }
  std::ostringstream text;
  text << file.rdbuf();
  const ConsoleConfigResult result = executeConsoleConfigText(
    console,
    migrateLegacyClientCvarNames(text.str())
  );
  for (const std::string& error : result.errors) {
    std::cerr << "Config warning: " << path << ": " << error << '\n';
  }
}

std::filesystem::path defaultClientConfigPath(
  const std::filesystem::path& assetBasePath
) {
  return assetBasePath / "config" / "default_client.cfg";
}

std::filesystem::path soundMixerConfigPath(
  const std::filesystem::path& assetBasePath
) {
  return assetBasePath / "config" / "sound_mixer.cfg";
}

void loadSoundMixerConfigs(
  ConsoleSystem& console,
  const std::filesystem::path& assetBasePath
) {
  loadClientConfig(console, soundMixerConfigPath(assetBasePath).string());

  std::filesystem::path directory = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    loadClientConfig(console, (directory / "config" / "sound_mixer.cfg").string());
    if (!directory.has_parent_path() || directory == directory.parent_path()) {
      break;
    }
    directory = directory.parent_path();
  }
}

bool saveClientConfig(
  const ConsoleSystem& console,
  const InputBindings& bindings,
  const std::string& path
) {
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  for (const std::string& line : console.archivedConfigLines()) {
    file << line << '\n';
  }
  file << "unbindall\n";
  for (const std::string& line : bindings.configLines()) {
    file << line << '\n';
  }
  return file.good();
}

CombatEffectsTuning combatEffectsTuning(const ConsoleSystem& console) {
  return {
    console.getInt("r_combat_effects"),
    console.getFloat("r_muzzle_light_intensity"),
    console.getFloat("r_muzzle_light_radius"),
    console.getFloat("r_muzzle_light_duration"),
    console.getBool("r_casings"),
    console.getFloat("r_casing_count"),
    console.getFloat("r_casing_lifetime"),
    static_cast<std::size_t>(console.getInt("r_casing_max")),
    console.getFloat("r_impact_particles"),
    static_cast<std::size_t>(console.getInt("r_impact_particle_max")),
    static_cast<std::size_t>(console.getInt("r_decals_max")),
    console.getFloat("r_decal_lifetime"),
  };
}

RenderSettings renderSettings(
  const ConsoleSystem& console,
  bool collisionDebugSupported
) {
  RenderSettings settings;
  settings.fieldOfView = console.getFloat("cl_fov");
  settings.healthTextScale = console.getFloat("cl_health_size");
  settings.healthStyle = console.getInt("cl_health_style");
  settings.speedTextScale = console.getFloat("cl_speed_size");
  settings.weaponBarScale = console.getFloat("cl_weapon_bar_size");
  settings.fpsTextScale = console.getFloat("cl_showfps_size");
  settings.uiFont = console.getString("r_ui_font");
  settings.frustumCullRemotePlayers = console.getBool("r_frustum_cull");
  settings.worldFrustumCull = console.getBool("r_world_frustum_cull");
  // SDL_Renderer does not consume Scene3D translucent geometry. Keep the
  // diagnostic fallback honest and avoid building an overlay it cannot show.
  settings.showCollision = collisionDebugSupported
    ? console.getInt("r_show_collision")
    : 0;
  settings.textureFilter = console.getInt("r_texture_filter");
  settings.textureAnisotropy = console.getInt("r_texture_anisotropy");
  settings.textureLodBias = console.getFloat("r_texture_lod_bias");
  settings.showRendererPerf = console.getBool("r_perf");
  settings.showRendererPerfDetail = console.getBool("r_perf_detail");
  settings.crosshairEnabled = console.getBool("crosshair");
  settings.crosshairStyle = console.getInt("crosshair_style");
  settings.crosshairSize = console.getFloat("crosshair_size");
  settings.crosshairThickness = console.getFloat("crosshair_width");
  settings.crosshairGap = console.getFloat("crosshair_gap");
  settings.crosshairDotEnabled = console.getBool("crosshair_dot");
  settings.crosshairDotThickness = console.getFloat("crosshair_dot_width");
  settings.crosshairOutlineEnabled = console.getBool("crosshair_outline");
  settings.crosshairOutlineWidth = console.getFloat("crosshair_outline_width");
  settings.crosshairAlpha = console.getFloat("crosshair_alpha");
  settings.crosshairRed = static_cast<std::uint8_t>(console.getInt("crosshair_r"));
  settings.crosshairGreen = static_cast<std::uint8_t>(console.getInt("crosshair_g"));
  settings.crosshairBlue = static_cast<std::uint8_t>(console.getInt("crosshair_b"));
  settings.crosshairHitRed = static_cast<std::uint8_t>(console.getInt("crosshair_hit_r"));
  settings.crosshairHitGreen = static_cast<std::uint8_t>(console.getInt("crosshair_hit_g"));
  settings.crosshairHitBlue = static_cast<std::uint8_t>(console.getInt("crosshair_hit_b"));
  settings.beamWidth = console.getFloat("r_beam_width");
  settings.beamAlpha = console.getFloat("r_beam_alpha");
  settings.beamRed = static_cast<std::uint8_t>(console.getInt("r_beam_r"));
  settings.beamGreen = static_cast<std::uint8_t>(console.getInt("r_beam_g"));
  settings.beamBlue = static_cast<std::uint8_t>(console.getInt("r_beam_b"));
  settings.beamHitRed =
    static_cast<std::uint8_t>(console.getInt("r_beam_hit_r"));
  settings.beamHitGreen =
    static_cast<std::uint8_t>(console.getInt("r_beam_hit_g"));
  settings.beamHitBlue =
    static_cast<std::uint8_t>(console.getInt("r_beam_hit_b"));
  settings.enemyBeamWidth = console.getFloat("r_enemy_beam_width");
  settings.enemyBeamAlpha = console.getFloat("r_enemy_beam_alpha");
  settings.enemyBeamRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_beam_r"));
  settings.enemyBeamGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_beam_g"));
  settings.enemyBeamBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_beam_b"));
  settings.hitMarkerEnabled = console.getBool("r_hitmarker");
  settings.hitMarkerSize = console.getFloat("r_hitmarker_size");
  settings.hitMarkerThickness = console.getFloat("r_hitmarker_width");
  settings.hitMarkerRed =
    static_cast<std::uint8_t>(console.getInt("r_hitmarker_r"));
  settings.hitMarkerGreen =
    static_cast<std::uint8_t>(console.getInt("r_hitmarker_g"));
  settings.hitMarkerBlue =
    static_cast<std::uint8_t>(console.getInt("r_hitmarker_b"));
  settings.shotgunWeaponModelStart =
    console.getBool("r_sg_weapon_model_start");
  settings.showOwnWeapons = console.getBool("r_show_weapons");
  settings.weaponPosition = console.getInt("r_weapon_pos");
  settings.combatEffectsQuality = console.getInt("r_combat_effects");
  settings.muzzleLightIntensity = console.getFloat("r_muzzle_light_intensity");
  settings.muzzleLightRadius = console.getFloat("r_muzzle_light_radius");
  settings.muzzleLightDurationSeconds =
    console.getFloat("r_muzzle_light_duration");
  settings.toneMapExposure = console.getFloat("r_tonemap_exposure");
  settings.displayGamma = console.getFloat("r_display_gamma");
  settings.atmosphereGradeQuality = console.getInt("r_atmosphere_grade");
  settings.bloomEnabled = console.getBool("r_bloom");
  settings.bloomIntensity = console.getFloat("r_bloom_intensity");
  settings.bloomThreshold = console.getFloat("r_bloom_threshold");
  settings.antiAliasingQuality = console.getInt("r_antialiasing");
  settings.sunShadowQuality = console.getInt("r_sun_shadows");
  settings.pointLightQuality = console.getInt("r_point_lights");
  settings.pointShadowQuality = console.getInt("r_point_shadows");
  settings.contactShadowsEnabled = console.getBool("r_contact_shadows");
  settings.materialQuality = console.getInt("r_material_quality");
  settings.ambientGroundingQuality = console.getInt("r_ambient_grounding");
  settings.ambientDebugMode = console.getInt("r_ambient_debug");
  settings.playerRimQuality = console.getInt("r_player_rim");
  settings.casingsEnabled = console.getBool("r_casings");
  settings.casingCountMultiplier = console.getFloat("r_casing_count");
  settings.casingLifetimeSeconds = console.getFloat("r_casing_lifetime");
  settings.maximumCasings =
    static_cast<std::uint32_t>(console.getInt("r_casing_max"));
  settings.particleMultiplier = console.getFloat("r_impact_particles");
  settings.maximumImpactParticles =
    static_cast<std::uint32_t>(console.getInt("r_impact_particle_max"));
  settings.maximumBulletDecals =
    static_cast<std::uint32_t>(console.getInt("r_decals_max"));
  settings.bulletDecalLifetimeSeconds =
    console.getFloat("r_decal_lifetime");
  settings.drawRemotePlayers = console.getBool("r_draw_remote_players");
  settings.drawRemoteWeapons = console.getBool("r_draw_remote_weapons");
  settings.drawPlayerOutlines = console.getBool("r_draw_player_outlines");
  settings.damageNumbersDuration = console.getFloat("r_damage_numbers_duration");
  settings.damageNumbersSize = console.getFloat("r_damage_numbers_size");
  settings.damageNumbersAlpha = console.getFloat("r_damage_numbers_alpha");
  settings.damageNumbersRed =
    static_cast<std::uint8_t>(console.getInt("r_damage_numbers_r"));
  settings.damageNumbersGreen =
    static_cast<std::uint8_t>(console.getInt("r_damage_numbers_g"));
  settings.damageNumbersBlue =
    static_cast<std::uint8_t>(console.getInt("r_damage_numbers_b"));
  settings.damageNumbersDamageColor =
    console.getBool("r_damage_numbers_damage_color");
  settings.damageNumbersOffsetX =
    console.getFloat("r_damage_numbers_offset_x");
  settings.damageNumbersOffsetY =
    console.getFloat("r_damage_numbers_offset_y");
  settings.enemyRed = static_cast<std::uint8_t>(console.getInt("r_enemy_r"));
  settings.enemyGreen = static_cast<std::uint8_t>(console.getInt("r_enemy_g"));
  settings.enemyBlue = static_cast<std::uint8_t>(console.getInt("r_enemy_b"));
  settings.enemyAlpha = console.getFloat("r_enemy_alpha");
  settings.playerModel = console.getInt("r_player_model");
  settings.enemyOutlineEnabled = console.getBool("r_enemy_outline");
  settings.playerOutlineMode = static_cast<PlayerOutlineMode>(
    console.getInt("r_player_outline_mode")
  );
  settings.playerOutlineStyle = static_cast<PlayerOutlineStyle>(
    console.getInt("r_player_outline_style")
  );
  settings.playerOutlineWidth = console.getFloat("r_player_outline_width");
  settings.playerOutlineDebugMask = console.getBool("r_player_outline_debug_mask");
  settings.enemyOutlineWidth = console.getFloat("r_enemy_outline_width");
  settings.enemyOutlineAlpha = console.getFloat("r_enemy_outline_alpha");
  settings.enemyOutlineRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_outline_r"));
  settings.enemyOutlineGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_outline_g"));
  settings.enemyOutlineBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_outline_b"));
  settings.enemyLeanEnabled = console.getBool("r_enemy_lean");
  settings.enemyLeanScale = console.getFloat("r_enemy_lean_scale");
  settings.enemyHitRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_hit_r"));
  settings.enemyHitGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_hit_g"));
  settings.enemyHitBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_hit_b"));
  settings.enemyHealthBarEnabled = console.getBool("r_enemy_health");
  settings.enemyHealthBarDamageOnly =
    console.getBool("r_enemy_health_damage_only");
  settings.enemyHealthBarFade = console.getBool("r_enemy_health_fade");
  settings.enemyHealthBarVisibleDuration =
    console.getFloat("r_enemy_health_duration");
  settings.enemyHealthBarMaxDistance =
    console.getFloat("r_enemy_health_max_distance");
  settings.enemyHealthBarWidth = console.getFloat("r_enemy_health_width");
  settings.enemyHealthBarHeight = console.getFloat("r_enemy_health_height");
  settings.enemyHealthBarWorldOffsetZ =
    console.getFloat("r_enemy_health_offset_z");
  settings.enemyHealthBarScreenOffsetX =
    console.getFloat("r_enemy_health_offset_x");
  settings.enemyHealthBarScreenOffsetY =
    console.getFloat("r_enemy_health_offset_y");
  settings.enemyHealthBarAlpha = console.getFloat("r_enemy_health_alpha");
  settings.enemyHealthBarRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_health_r"));
  settings.enemyHealthBarGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_health_g"));
  settings.enemyHealthBarBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_health_b"));
  settings.teammateBeamWidth = console.getFloat("r_teammate_beam_width");
  settings.teammateBeamAlpha = console.getFloat("r_teammate_beam_alpha");
  settings.teammateBeamRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_beam_r"));
  settings.teammateBeamGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_beam_g"));
  settings.teammateBeamBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_beam_b"));
  settings.teammateRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_r"));
  settings.teammateGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_g"));
  settings.teammateBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_b"));
  settings.teammateAlpha = console.getFloat("r_teammate_alpha");
  settings.teammateOutlineEnabled =
    console.getBool("r_teammate_outline");
  settings.teammateOutlineWidth =
    console.getFloat("r_teammate_outline_width");
  settings.teammateOutlineAlpha =
    console.getFloat("r_teammate_outline_alpha");
  settings.teammateOutlineRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_outline_r"));
  settings.teammateOutlineGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_outline_g"));
  settings.teammateOutlineBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_outline_b"));
  settings.teammateLeanEnabled = console.getBool("r_teammate_lean");
  settings.teammateLeanScale = console.getFloat("r_teammate_lean_scale");

  settings.teammateHealthBarEnabled =
    console.getBool("r_teammate_health");
  settings.teammateHealthBarDamageOnly =
    console.getBool("r_teammate_health_damage_only");
  settings.teammateHealthBarFade =
    console.getBool("r_teammate_health_fade");
  settings.teammateHealthBarVisibleDuration =
    console.getFloat("r_teammate_health_duration");
  settings.teammateHealthBarMaxDistance =
    console.getFloat("r_teammate_health_max_distance");
  settings.teammateHealthBarWidth =
    console.getFloat("r_teammate_health_width");
  settings.teammateHealthBarHeight =
    console.getFloat("r_teammate_health_height");
  settings.teammateHealthBarWorldOffsetZ =
    console.getFloat("r_teammate_health_offset_z");
  settings.teammateHealthBarScreenOffsetX =
    console.getFloat("r_teammate_health_offset_x");
  settings.teammateHealthBarScreenOffsetY =
    console.getFloat("r_teammate_health_offset_y");
  settings.teammateHealthBarAlpha =
    console.getFloat("r_teammate_health_alpha");
  settings.teammateHealthBarRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_health_r"));
  settings.teammateHealthBarGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_health_g"));
  settings.teammateHealthBarBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_health_b"));
  settings.enemyNameTagEnabled = console.getBool("r_enemy_name");
  settings.enemyNameTagAlpha = console.getFloat("r_enemy_name_alpha");
  settings.enemyNameTagScale = console.getFloat("r_enemy_name_font_size");
  settings.enemyNameTagWorldOffsetZ = console.getFloat("r_enemy_name_offset_z");
  settings.enemyNameTagScreenOffsetX = console.getFloat("r_enemy_name_offset_x");
  settings.enemyNameTagScreenOffsetY = console.getFloat("r_enemy_name_offset_y");
  settings.enemyNameTagMaxDistance = console.getFloat("r_enemy_name_max_distance");
  settings.enemyNameTagRed =
    static_cast<std::uint8_t>(console.getInt("r_enemy_name_r"));
  settings.enemyNameTagGreen =
    static_cast<std::uint8_t>(console.getInt("r_enemy_name_g"));
  settings.enemyNameTagBlue =
    static_cast<std::uint8_t>(console.getInt("r_enemy_name_b"));
  settings.teammateNameTagEnabled = console.getBool("r_teammate_name");
  settings.teammateNameTagAlpha = console.getFloat("r_teammate_name_alpha");
  settings.teammateNameTagScale = console.getFloat("r_teammate_name_font_size");
  settings.teammateNameTagWorldOffsetZ =
    console.getFloat("r_teammate_name_offset_z");
  settings.teammateNameTagScreenOffsetX =
    console.getFloat("r_teammate_name_offset_x");
  settings.teammateNameTagScreenOffsetY =
    console.getFloat("r_teammate_name_offset_y");
  settings.teammateNameTagMaxDistance =
    console.getFloat("r_teammate_name_max_distance");
  settings.teammateNameTagRed =
    static_cast<std::uint8_t>(console.getInt("r_teammate_name_r"));
  settings.teammateNameTagGreen =
    static_cast<std::uint8_t>(console.getInt("r_teammate_name_g"));
  settings.teammateNameTagBlue =
    static_cast<std::uint8_t>(console.getInt("r_teammate_name_b"));
  settings.showLagCompensation = console.getBool("cl_show_lagcomp");
  return settings;
}

MouseAimSettings mouseAimSettingsFromConsole(
  const ConsoleSystem& console,
  bool zoomHeld,
  bool sniperZoom,
  float zoomAmount = 1.0F
) {
  const float zoomSensitivity = zoomSensitivityMultiplier(
    console.getFloat("cl_fov"),
    console.getFloat(
      sniperZoom ? "cl_zoom_sniper_fov" : "cl_zoom_fov"
    ),
    console.getFloat("cl_zoom_sensitivity")
  );
  return {
    console.getFloat("sensitivity"),
    zoomHeld
      ? 1.0F + (zoomSensitivity - 1.0F) *
          std::clamp(zoomAmount, 0.0F, 1.0F)
      : 1.0F,
    console.getFloat("cl_mouseAccel"),
    console.getFloat("cl_mouseAccelPower"),
    console.getFloat("cl_mouseAccelOffset"),
    console.getFloat("cl_mouseSensCap"),
  };
}

bool sameRuntimeMovementTuning(
  const MovementTuning& lhs,
  const MovementTuning& rhs
) {
  return lhs.flightEnabled == rhs.flightEnabled &&
    lhs.groundAcceleration == rhs.groundAcceleration &&
    lhs.airAcceleration == rhs.airAcceleration &&
    lhs.airControlEnabled == rhs.airControlEnabled &&
    lhs.groundFriction == rhs.groundFriction &&
    lhs.stopSpeed == rhs.stopSpeed &&
    lhs.maxGroundSpeed == rhs.maxGroundSpeed &&
    lhs.flightAcceleration == rhs.flightAcceleration &&
    lhs.maxFlightSpeed == rhs.maxFlightSpeed &&
    lhs.flightDamping == rhs.flightDamping &&
    lhs.flightGravityCancel == rhs.flightGravityCancel;
}

ConsoleRenderState consoleRenderState(const ClientConsoleState& state) {
  ConsoleRenderState renderState;
  renderState.open = state.open;
  renderState.input = state.input;
  renderState.cursorIndex = state.cursorIndex;
  renderState.lines.assign(state.output.begin(), state.output.end());
  renderState.hasSelection = state.hasSelection;
  renderState.selectionAnchor = state.selectionAnchor;
  renderState.selectionFocus = state.selectionFocus;
  renderState.inputHasSelection = hasSelection(state.inputSelection);
  renderState.inputSelectionAnchor = state.inputSelection.anchor;
  renderState.inputSelectionFocus = state.inputSelection.focus;
  renderState.scrollRows = state.scrollRows;
  renderState.cat = state.cat.pose();
  return renderState;
}

void clearConsoleSelection(ClientConsoleState& state) {
  state.hasSelection = false;
  state.selecting = false;
  state.selectionAnchor = 0;
  state.selectionFocus = 0;
  clearSelection(state.inputSelection);
  state.selectingInput = false;
}

void clearConsoleOutputSelection(ClientConsoleState &state) {
  state.hasSelection = false;
  state.selecting = false;
  state.selectionAnchor = 0U;
  state.selectionFocus = 0U;
}

ConsoleTextLayout consoleLayoutForWindow(
  SDL_Window* window,
  const ClientConsoleState& state
) {
  int viewportWidth = 0;
  int viewportHeight = 0;
  SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
  return buildConsoleTextLayout(
    viewportWidth,
    viewportHeight,
    consoleRenderState(state)
  );
}

OptionMenuLayout optionMenuLayoutForWindow(SDL_Window *window,
                                           std::size_t itemCount,
                                           std::size_t scrollRows) {
  int viewportWidth = 0;
  int viewportHeight = 0;
  SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
  return buildOptionMenuLayout(viewportWidth, viewportHeight, itemCount,
                               scrollRows);
}

std::string consoleClipboardTextForWindow(SDL_Window *window,
                                          const ClientConsoleState &state) {
  if (hasSelection(state.inputSelection)) {
    return selectedText(state.input, state.inputSelection);
  }
  if (state.hasSelection && state.selectionAnchor != state.selectionFocus) {
    return consoleSelectedText(
      consoleLayoutForWindow(window, state),
      state.selectionAnchor,
      state.selectionFocus
    );
  }
  return consoleInputClipboardText(state.input);
}

HudRenderState chatHudRenderState(const ClientChatState& state) {
  HudRenderState hud;
  hud.chatLines.reserve(state.history.size());
  for (const ClientChatState::Message& message : state.history) {
    hud.chatLines.push_back(HudRenderState::ChatLine{
      message.playerIndex,
      message.text,
      message.speakerName,
    });
  }
  hud.chatInputOpen = state.inputOpen;
  hud.chatInput = state.input;
  hud.chatCursorIndex = state.cursorIndex;
  hud.chatHasSelection = hasSelection(state.selection);
  hud.chatSelectionAnchor = state.selection.anchor;
  hud.chatSelectionFocus = state.selection.focus;
  hud.chatHistoryHasSelection = state.hasHistorySelection;
  hud.chatHistorySelectionAnchor = state.historySelectionAnchor;
  hud.chatHistorySelectionFocus = state.historySelectionFocus;
  hud.chatScrollRows = state.scrollRows;
  return hud;
}

ChatTextLayout chatLayoutForWindow(SDL_Window* window, const ClientChatState& state) {
  int viewportWidth = 0;
  int viewportHeight = 0;
  SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
  return buildChatTextLayout(viewportWidth, viewportHeight, chatHudRenderState(state));
}

void clearChatSelection(ClientChatState& state) {
  clearSelection(state.selection);
  state.selecting = false;
}

void clearChatHistorySelection(ClientChatState &state) {
  state.hasHistorySelection = false;
  state.selectingHistory = false;
  state.historySelectionAnchor = 0U;
  state.historySelectionFocus = 0U;
}

void clearChatSelections(ClientChatState &state) {
  clearChatSelection(state);
  clearChatHistorySelection(state);
}

std::string chatClipboardText(SDL_Window *window,
                              const ClientChatState &state) {
  if (state.hasHistorySelection &&
      state.historySelectionAnchor != state.historySelectionFocus) {
    return chatHistorySelectedText(chatLayoutForWindow(window, state),
                                   state.historySelectionAnchor,
                                   state.historySelectionFocus);
  }
  if (hasSelection(state.selection)) {
    return selectedText(state.input, state.selection);
  }
  return state.input;
}

void pasteClipboardTextIntoChat(ClientChatState &state) {
  char* clipboardText = SDL_GetClipboardText();
  if (clipboardText == nullptr) {
    return;
  }
  clearChatHistorySelection(state);
  replaceSelectionOrInsert(
    state.input,
    state.cursorIndex,
    state.selection,
    clipboardText,
    TextInputFilter::Chat,
    kMaxChatMessageBytes
  );
  SDL_free(clipboardText);
}

void beginChatSelection(
  SDL_Window* window,
  ClientChatState& state,
  float x,
  float y
) {
  const ChatTextLayout layout = chatLayoutForWindow(window, state);
  if (!layout.inputRows.empty() && y >= layout.inputRows.front().y &&
      y < layout.inputRows.back().y + layout.input.lineHeight) {
    clearChatHistorySelection(state);
    const std::size_t offset = chatInputOffsetAt(layout, state.input, x, y);
    state.cursorIndex = offset;
    state.selection.active = true;
    state.selection.anchor = offset;
    state.selection.focus = offset;
    state.selecting = true;
    return;
  }
  if (!layout.rows.empty() && y >= layout.rows.front().y &&
      y < layout.rows.back().y + layout.lineHeight) {
    clearChatSelection(state);
    const std::size_t offset = chatHistoryTextOffsetAt(layout, x, y);
    state.hasHistorySelection = true;
    state.selectingHistory = true;
    state.historySelectionAnchor = offset;
    state.historySelectionFocus = offset;
    return;
  }
  clearChatSelections(state);
}

void updateChatSelection(
  SDL_Window* window,
  ClientChatState& state,
  float x,
  float y
) {
  const ChatTextLayout layout = chatLayoutForWindow(window, state);
  if (state.selecting) {
    state.selection.focus = chatInputOffsetAt(layout, state.input, x, y);
    state.cursorIndex = state.selection.focus;
  } else if (state.selectingHistory) {
    state.historySelectionFocus = chatHistoryTextOffsetAt(layout, x, y);
  }
}

void beginConsoleSelection(
  SDL_Window* window,
  ClientConsoleState& state,
  float x,
  float y
) {
  const ConsoleTextLayout layout = consoleLayoutForWindow(window, state);
  bool inPrompt = false;
  for (const ConsoleLayoutLine &line : layout.lines) {
    inPrompt = inPrompt ||
               (line.prompt && y >= line.y && y < line.y + layout.lineHeight);
  }
  if (inPrompt) {
    clearConsoleSelection(state);
    const std::size_t offset = consoleInputOffsetAt(layout, state.input, x, y);
    state.cursorIndex = offset;
    state.inputSelection.active = true;
    state.inputSelection.anchor = offset;
    state.inputSelection.focus = offset;
    state.selectingInput = true;
    return;
  }
  clearSelection(state.inputSelection);
  state.selectingInput = false;
  const std::size_t offset = consoleTextOffsetAt(layout, x, y);
  state.hasSelection = true;
  state.selecting = true;
  state.selectionAnchor = offset;
  state.selectionFocus = offset;
}

void updateConsoleSelection(
  SDL_Window* window,
  ClientConsoleState& state,
  float x,
  float y
) {
  const ConsoleTextLayout layout = consoleLayoutForWindow(window, state);
  if (state.selectingInput) {
    state.inputSelection.focus =
        consoleInputOffsetAt(layout, state.input, x, y);
    state.cursorIndex = state.inputSelection.focus;
  } else if (state.selecting) {
    state.selectionFocus = consoleTextOffsetAt(layout, x, y);
  }
}

std::string keyName(SDL_Scancode scancode) {
  switch (scancode) {
  case SDL_SCANCODE_GRAVE:
    return "section";
  case SDL_SCANCODE_LEFT:
    return "left";
  case SDL_SCANCODE_RIGHT:
    return "right";
  case SDL_SCANCODE_UP:
    return "up";
  case SDL_SCANCODE_DOWN:
    return "down";
  case SDL_SCANCODE_LCTRL:
    return "leftctrl";
  case SDL_SCANCODE_RCTRL:
    return "rightctrl";
  case SDL_SCANCODE_LSHIFT:
    return "leftshift";
  case SDL_SCANCODE_RSHIFT:
    return "rightshift";
  default:
    return InputBindings::normalizeKey(SDL_GetScancodeName(scancode));
  }
}

std::string mouseButtonName(Uint8 button) {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return "mouse1";
  case SDL_BUTTON_RIGHT:
    return "mouse2";
  case SDL_BUTTON_MIDDLE:
    return "mouse3";
  default:
    return "mouse" + std::to_string(static_cast<unsigned int>(button));
  }
}

bool isConsoleToggleText(std::string_view text) {
  return text == "\xC2\xA7" || text == "`" || text == "~";
}

void installDefaultBindings(InputBindings& bindings) {
  (void)bindings.bind("section", "toggleconsole");
  (void)bindings.bind("w", "+forward");
  (void)bindings.bind("s", "+back");
  (void)bindings.bind("a", "+moveleft");
  (void)bindings.bind("d", "+moveright");
  (void)bindings.bind("space", "+moveup");
  (void)bindings.bind("leftctrl", "+movedown");
  (void)bindings.bind("rightctrl", "+movedown");
  (void)bindings.bind("leftshift", "+speed");
  (void)bindings.bind("rightshift", "+speed");
  (void)bindings.bind("mouse1", "+attack");
  (void)bindings.bind("mouse2", "+zoom");
  (void)bindings.bind("mouse3", "+dash");
  (void)bindings.bind("g", "mcguffin_throw");
  (void)bindings.bind("2", "weapon mg");
  (void)bindings.bind("3", "weapon sg");
  (void)bindings.bind("5", "weapon gl");
  (void)bindings.bind("q", "weapon rl");
  (void)bindings.bind("e", "weapon lg");
  (void)bindings.bind("r", "weapon rg");
  (void)bindings.bind("4", "weapon pg");
  (void)bindings.bind("8", "weapon fg");
  (void)bindings.bind("9", "weapon re");
  (void)bindings.bind("q", "weapon rl");
  (void)bindings.bind("e", "weapon lg");
  (void)bindings.bind("r", "weapon rg");
  (void)bindings.bind("f5", "resetmatch");
  (void)bindings.bind("f3", "ready");
  (void)bindings.bind("t", "messagemode");
  (void)bindings.bind("z", "+showchat");
  (void)bindings.bind("tab", "+scores");
  (void)bindings.bind("f10", "settings");
  (void)bindings.bind("f11", "misc");
}

std::string gameModeName(GameMode gameMode) {
  switch (gameMode) {
  case GameMode::Duel:
    return "DUEL";
  case GameMode::ClanArena:
    return "CLAN ARENA";
  case GameMode::McGuffin:
    return "MCGUFFIN";
  }
  return "UNKNOWN";
}

std::string teamName(Team team) {
  switch (team) {
  case Team::None:
    return "NONE";
  case Team::Red:
    return "RED";
  case Team::Blue:
    return "BLUE";
  }
  return "UNKNOWN";
}

std::string aliveCountLine(const ServerSnapshot& snapshot) {
  std::uint32_t redAlive = 0;
  std::uint32_t blueAlive = 0;
  for (std::size_t index = 0; index < snapshot.players.size(); ++index) {
    if (
      (!snapshot.connectedPlayers[index] && !snapshot.botPlayers[index]) ||
      snapshot.players[index].health <= 0
    ) {
      continue;
    }
    if (snapshot.teams[index] == Team::Red) {
      ++redAlive;
    } else if (snapshot.teams[index] == Team::Blue) {
      ++blueAlive;
    }
  }
  return "ALIVE " + std::to_string(redAlive) + 'v' + std::to_string(blueAlive);
}

std::string matchPhaseName(MatchPhase phase) {
  switch (phase) {
  case MatchPhase::WaitingForPlayers:
    return "WAITING FOR PLAYERS";
  case MatchPhase::WaitingForReady:
    return "WAITING FOR READY";
  case MatchPhase::Countdown:
    return "ROUND START";
  case MatchPhase::Live:
    return "LIVE";
  case MatchPhase::RoundEnd:
    return "ROUND OVER";
  case MatchPhase::MatchEnd:
    return "MATCH OVER";
  }
  return "UNKNOWN";
}

HudRenderState buildHud(
  const ClientSession& session,
  bool showAliveCounts,
  std::optional<std::size_t> subjectPlayerIndex
) {
  HudRenderState hud;
  hud.centerLines.push_back(session.statusMessage());
  if (!session.readyForPlay()) {
    return hud;
  }

  const ClientGame& client = *session.game();
  const ServerSnapshot& snapshot = client.snapshot();
  if (subjectPlayerIndex.has_value() &&
      *subjectPlayerIndex >= kDuelPlayerCount) {
    subjectPlayerIndex.reset();
  }
  std::size_t occupiedCount = 0;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (snapshot.connectedPlayers[index] || snapshot.botPlayers[index]) {
      ++occupiedCount;
    }
  }

  if (!subjectPlayerIndex.has_value()) {
    // A dedicated observer without a living target has no body whose health,
    // ammo, team, score, or readiness can truthfully be presented as local.
    hud.centerLines.clear();
    hud.topLeftLines.push_back(
      "PLAYERS " + std::to_string(occupiedCount) + '/' +
      std::to_string(kDuelPlayerCount)
    );
    if (snapshot.spectatorCount > 0) {
      hud.topLeftLines.push_back(
        "SPECTATORS " + std::to_string(snapshot.spectatorCount)
      );
    }
    hud.topLeftLines.push_back("MODE " + gameModeName(snapshot.gameMode));
    if (showAliveCounts && snapshot.gameMode == GameMode::ClanArena) {
      hud.topRightLines.push_back(aliveCountLine(snapshot));
    }
    hud.centerLines.push_back(matchPhaseName(snapshot.matchPhase));
    hud.centerOffsetY = matchPhaseMessageOffsetY(snapshot.matchPhase);
    return hud;
  }

  const std::size_t localPlayerIndex = *subjectPlayerIndex;
  const std::size_t remotePlayerIndex =
    opponentPlayerIndex(snapshot, localPlayerIndex);

  hud.healthAmount = snapshot.healthAmount;
  const WeaponAmmoArray& localAmmo = snapshot.playerAmmo[localPlayerIndex];
  const auto ammoText = [&snapshot, &localAmmo](Weapon weapon) {
    return snapshot.weaponAmmo.infiniteAmmo
      ? std::string("\xE2\x88\x9E")
      : std::to_string(localAmmo[weaponIndex(weapon)]);
  };
  hud.weaponValues = {{
    ammoText(Weapon::MachineGun),
    ammoText(Weapon::Shotgun),
    ammoText(Weapon::GrenadeLauncher),
    ammoText(Weapon::RocketLauncher),
    ammoText(Weapon::LightningGun),
    ammoText(Weapon::Railgun),
    ammoText(Weapon::PlasmaGun),
    ammoText(Weapon::FreezeGun),
    ammoText(Weapon::Revolver),
  }};
  hud.sniperChargePercent = snapshot.sniperChargePercent[localPlayerIndex];
  hud.centerLines.clear();
  hud.bottomCenterLines.push_back(
    "HEALTH " + std::to_string(snapshot.players[localPlayerIndex].health)
  );
  hud.topLeftLines.push_back(
    "PLAYERS " + std::to_string(occupiedCount) + '/' +
    std::to_string(kDuelPlayerCount)
  );
  if (snapshot.spectatorCount > 0) {
    hud.topLeftLines.push_back(
      "SPECTATORS " + std::to_string(snapshot.spectatorCount)
    );
  }
  if (snapshot.matchPhase != MatchPhase::Live) {
    hud.topLeftLines.push_back("MODE " + gameModeName(snapshot.gameMode));
    if (snapshot.gameMode != GameMode::Duel) {
      hud.topLeftLines.push_back(
        "TEAM " + teamName(snapshot.teams[localPlayerIndex])
      );
    }
  }
  if (showAliveCounts && snapshot.gameMode == GameMode::ClanArena) {
    hud.topRightLines.push_back(aliveCountLine(snapshot));
  }
  hud.topCenterLines.push_back(hudScoreLine(snapshot, localPlayerIndex));
  if (snapshot.gameMode == GameMode::McGuffin) {
    hud.mcguffinNavigation = selectMcGuffinNavigationTarget(
      snapshot,
      client.arena(),
      localPlayerIndex
    );
    const char* state = "AT CENTER";
    if (snapshot.mcguffin.state == McGuffinState::Carried) state = "CARRIED";
    else if (snapshot.mcguffin.state == McGuffinState::Dropped) state = "DROPPED";
    else if (snapshot.mcguffin.state == McGuffinState::InstalledRed) state = "RED CONTROL";
    else if (snapshot.mcguffin.state == McGuffinState::InstalledBlue) state = "BLUE CONTROL";
    hud.topCenterLines.emplace_back(std::string("MCGUFFIN ") + state);
    if (snapshot.mcguffin.state == McGuffinState::NeutralSpawn &&
        snapshot.mcguffin.stateTicks < snapshot.mcguffinConfig.initialSpawnTicks) {
      const std::uint32_t ticks = snapshot.mcguffinConfig.initialSpawnTicks -
        snapshot.mcguffin.stateTicks;
      hud.topCenterLines.push_back("SPAWNS IN " + std::to_string((ticks + 124U) / 125U));
    }
    if (snapshot.mcguffin.state == McGuffinState::Dropped &&
        snapshot.mcguffinConfig.returnTicks > 0 &&
        snapshot.mcguffin.stateTicks < snapshot.mcguffinConfig.returnTicks) {
      const std::uint32_t ticks = snapshot.mcguffinConfig.returnTicks -
        snapshot.mcguffin.stateTicks;
      hud.topCenterLines.push_back("RETURNS IN " + std::to_string((ticks + 124U) / 125U));
    }
    if (snapshot.mcguffin.carrierIndex < kDuelPlayerCount) {
      hud.topCenterLines.push_back(
        snapshot.mcguffin.carrierIndex == localPlayerIndex
          ? "YOU HAVE THE MCGUFFIN"
          : snapshot.playerNames[snapshot.mcguffin.carrierIndex] + " HAS THE MCGUFFIN"
      );
    }
    switch (snapshot.mcguffin.lastEvent) {
    case McGuffinEventType::Pickup:
      hud.topCenterLines.emplace_back("OBJECTIVE PICKED UP");
      break;
    case McGuffinEventType::Drop:
      hud.topCenterLines.emplace_back("OBJECTIVE DROPPED");
      break;
    case McGuffinEventType::Install:
      hud.topCenterLines.emplace_back("OBJECTIVE INSTALLED");
      break;
    case McGuffinEventType::Steal:
      hud.topCenterLines.emplace_back("OBJECTIVE STOLEN");
      break;
    case McGuffinEventType::Return:
      hud.topCenterLines.emplace_back("OBJECTIVE RETURNED");
      break;
    case McGuffinEventType::RoundWin:
      hud.topCenterLines.emplace_back("SCORE LIMIT REACHED");
      break;
    case McGuffinEventType::Throw:
      hud.topCenterLines.emplace_back("OBJECTIVE THROWN");
      break;
    case McGuffinEventType::None:
      break;
    }
  }
  const std::string timeLine = matchTimeLine(snapshot);
  if (!timeLine.empty()) {
    hud.topRightLines.push_back(timeLine);
  }
  if (snapshot.matchRules.showOpponentHealth && remotePlayerIndex != localPlayerIndex) {
    hud.showOpponentHealthBar = true;
  }

  hud.centerLines.push_back(matchPhaseName(snapshot.matchPhase));
  hud.centerOffsetY = matchPhaseMessageOffsetY(snapshot.matchPhase);
  switch (snapshot.matchPhase) {
  case MatchPhase::WaitingForPlayers:
    hud.centerLines.push_back(
      std::to_string(occupiedCount) + '/' +
      std::to_string(kDuelPlayerCount) + " PLAYER SLOTS OCCUPIED"
    );
    break;
  case MatchPhase::WaitingForReady:
    hud.centerLines.push_back(
      snapshot.readyPlayers[localPlayerIndex]
        ? "WAITING FOR OTHER PLAYERS TO READY UP"
        : "PRESS F3 TO READY UP"
    );
    break;
  case MatchPhase::Countdown: {
    const std::uint32_t seconds =
      (snapshot.phaseTicksRemaining + 124U) / 125U;
    hud.countdownText = std::to_string(seconds);
    hud.countdownPulse =
      1.0F - (
        static_cast<float>((snapshot.phaseTicksRemaining - 1U) % 125U) /
        124.0F
      );
    hud.centerLines.push_back("MOVE ENABLED - WEAPONS LOCKED");
    break;
  }
  case MatchPhase::RoundEnd:
    hud.centerLines.push_back(
      localPlayerWonResult(snapshot, localPlayerIndex, false)
        ? "ROUND WON"
        : "ROUND LOST"
    );
    hud.centerLines.push_back(
      roundStatsLine("YOU", snapshot.roundCombatStats[localPlayerIndex])
    );
    if (remotePlayerIndex != localPlayerIndex) {
      hud.centerLines.push_back(
        playerRoundStatsLine(snapshot, remotePlayerIndex)
      );
    }
    break;
  case MatchPhase::MatchEnd:
    hud.centerLines.push_back(
      localPlayerWonResult(snapshot, localPlayerIndex, true)
        ? "MATCH WON"
        : "MATCH LOST"
    );
    hud.centerLines.push_back(
      roundStatsLine("YOU", snapshot.roundCombatStats[localPlayerIndex])
    );
    if (remotePlayerIndex != localPlayerIndex) {
      hud.centerLines.push_back(
        playerRoundStatsLine(snapshot, remotePlayerIndex)
      );
    }
    break;
  case MatchPhase::Live:
    hud.centerLines.clear();
    break;
  }
  return hud;
}

[[nodiscard]] UserCommand buildCommand(
  const LocalInputState& input,
  const PlayerState& player,
  std::uint32_t sequence,
  std::uint32_t clientTick,
  const MouseAimSettings& mouseAimSettings,
  float mouseFrameSeconds,
  Weapon weapon,
  bool zoomed
) {
  UserCommand command;
  command.sequence = sequence;
  command.clientTick = clientTick;
  const MouseAimDelta mouseAimDelta = quakeLiveMouseAimDelta(
    input.mouseDeltaX,
    input.mouseDeltaY,
    mouseFrameSeconds,
    mouseAimSettings
  );
  command.viewYawRadians = player.viewYawRadians - mouseAimDelta.yawRadians;
  command.viewPitchRadians = clamp(
    player.viewPitchRadians - mouseAimDelta.pitchRadians,
    -kMaxPitchRadians,
    kMaxPitchRadians
  );
  command.planarAim = false;

  command.forwardMove = (input.forward > 0 ? 1.0F : 0.0F) - (input.back > 0 ? 1.0F : 0.0F);
  command.rightMove = (input.right > 0 ? 1.0F : 0.0F) - (input.left > 0 ? 1.0F : 0.0F);
  command.upMove = (input.up > 0 ? 1.0F : 0.0F) - (input.down > 0 ? 1.0F : 0.0F);
  command.jump = input.up > 0;
  command.dash = input.dash > 0;
  command.crouch = input.down > 0;
  command.sneak = input.sneak > 0;
  command.attack = input.attack > 0;
  command.zoomed = zoomed;
  command.weapon = weapon;
  return command;
}

[[nodiscard]] UserCommand buildCommandWithViewAngles(
  const LocalInputState& input,
  std::uint32_t sequence,
  std::uint32_t clientTick,
  float yawRadians,
  float pitchRadians,
  Weapon weapon,
  bool zoomed
) {
  UserCommand command;
  command.sequence = sequence;
  command.clientTick = clientTick;
  command.viewYawRadians = yawRadians;
  command.viewPitchRadians = pitchRadians;
  command.planarAim = false;
  command.forwardMove = (input.forward > 0 ? 1.0F : 0.0F) - (input.back > 0 ? 1.0F : 0.0F);
  command.rightMove = (input.right > 0 ? 1.0F : 0.0F) - (input.left > 0 ? 1.0F : 0.0F);
  command.upMove = (input.up > 0 ? 1.0F : 0.0F) - (input.down > 0 ? 1.0F : 0.0F);
  command.jump = input.up > 0;
  command.dash = input.dash > 0;
  command.crouch = input.down > 0;
  command.sneak = input.sneak > 0;
  command.attack = input.attack > 0;
  command.zoomed = zoomed;
  command.weapon = weapon;
  return command;
}
#endif

} // namespace

GameApp::GameApp(
  std::string serverHost,
  std::uint16_t serverPort,
  DeveloperControlOptions developerControl,
  BenchmarkOptions benchmark
)
  : serverHost_(std::move(serverHost)),
    serverPort_(serverPort),
    developerControl_(developerControl),
    benchmark_(benchmark) {}

int GameApp::run() const {
#if LG_DUEL_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 1;
  }
  const bool audioSubsystemAvailable = SDL_InitSubSystem(SDL_INIT_AUDIO);

  SDL_Window* window = SDL_CreateWindow(name().data(), 1280, 720, SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }

  if (!SDL_SetWindowRelativeMouseMode(window, true)) {
    std::cerr << "Relative mouse mode failed: " << SDL_GetError() << '\n';
  }

  Renderer renderer;
  if (!renderer.initialize(window)) {
    std::cerr << "Renderer initialization failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "Renderer backend: " << renderer.backendName() << '\n';
  const char* executableBasePath = SDL_GetBasePath();
  const std::filesystem::path assetBasePath =
    executableBasePath != nullptr ? executableBasePath : std::filesystem::current_path();
  const std::vector<ImpactSurfaceMaterial> impactSurfaceMaterials =
    loadImpactSurfaceMaterials(assetBasePath / "textures");
  const std::filesystem::path runtimeDirectory =
    std::filesystem::weakly_canonical(assetBasePath);
  dev::DevControlServer developerControl;
  const std::filesystem::path repositoryRoot =
    runtimeDirectory.parent_path().parent_path();
  const std::filesystem::path captureDirectory =
    runtimeDirectory.parent_path() / "captures";
  std::string lastControlError;
  if (developerControl_.enabled) {
    std::error_code directoryError;
    std::filesystem::create_directories(captureDirectory, directoryError);
    if (directoryError) {
      std::cerr << "Could not create developer capture directory: "
                << directoryError.message() << '\n';
      renderer.shutdown();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
    std::string controlError;
    if (!developerControl.start(developerControl_.port, controlError)) {
      std::cerr << "Developer control startup failed: " << controlError << '\n';
      renderer.shutdown();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
    std::cout << "Developer control enabled on 127.0.0.1:"
              << developerControl.port() << '\n';
    std::cout << "Capture output: " << captureDirectory.string() << '\n';
  }
  ClientAudio audio;
  const bool audioAvailable =
    audioSubsystemAvailable && audio.initialize(assetBasePath);

  ConsoleSystem console;
  registerClientCvars(console);
  InputBindings bindings;
  const std::string configPath = clientConfigPath();
  LocalInputState input;
  bool running = true;
  bool resetRequested = false;
  bool readyRequested = false;
  bool mcguffinThrowRequested = false;
  bool quitRequested = false;
  bool clearRequested = false;
  bool writeConfigRequested = false;
  bool toggleConsoleRequested = false;
  bool settingsMenuRequested = false;
  bool miscMenuRequested = false;
  bool openChatRequested = false;
  bool showChatRequested = false;
  bool requestGameModePending = false;
  bool requestTeamPending = false;
  bool requestSpectatorPending = false;
  GameMode requestedGameMode = GameMode::Duel;
  Team requestedTeam = Team::None;
  int scoreboardPressCount = 0;
  int chatHistoryPressCount = 0;
  int zoomPressCount = 0;
  float sniperAdsAmount = 0.0F;
  int pendingSpectateCycle = 0;
  Weapon selectedWeapon = Weapon::LightningGun;
  Weapon viewWeapon = Weapon::LightningGun;
  Weapon previousViewWeapon = Weapon::LightningGun;
  float weaponSwitchSeconds = 1.0F;
  bool botDodgeEnabled = false;
  std::int32_t botDodgeMinIntervalMs = 250;
  std::int32_t botDodgeMaxIntervalMs = 750;
  bool botStareEnabled = true;
  bool botStandstillEnabled = false;
  BotAttackMode botAttackMode = BotAttackMode::Off;
  Weapon botWeapon = Weapon::MachineGun;
  // The snapshot carries the selected fallback weapon but no mode bit. Keep
  // this client-side request state so querying after `bot_weapon auto` says
  // what the user selected without extending the game protocol.
  bool botWeaponAuto = true;
  struct PendingBotCommand {
    BotCommandType type = BotCommandType::None;
    std::int32_t value = 0;
    std::int32_t minIntervalMs = 250;
    std::int32_t maxIntervalMs = 750;
  };
  std::string pendingPlayerName;
  std::string lastSentPlayerName;
  std::string pendingMapName;
  std::deque<PendingBotCommand> pendingBotCommands;
  ClientChatState chatState;
  ClientSession session;
  std::optional<ClientNetworkSimulationConfig> developerNetworkSimulation;
  dev::CameraTransform developmentCamera;
  bool developmentCameraEnabled = false;
  std::uint64_t renderedFrameSerial = 0;
  struct ActiveControlOperation {
    enum class Stage {
      Start,
      WaitingForMap,
      WaitingForCameraFrame,
      WaitingForFrames,
      WaitingForClientTick,
      WaitingForSnapshotTick,
      WaitingForCommandAck,
      WaitingForPhaseCapture,
      PlayerInput,
      WaitingForInputAck,
      CaptureReady,
      BenchmarkWarmup,
      BenchmarkMeasure,
      BenchmarkGpuDrain,
      BenchmarkWaitingForCameraFrame,
      BenchmarkCaptureReady,
      BenchmarkFinalize,
    };
    dev::QueuedControlRequest queued;
    Stage stage = Stage::Start;
    std::string targetMap;
    std::uint32_t previousMapRevision = 0;
    std::uint64_t requiredRenderedFrame = 0;
    std::uint32_t inputTicksRemaining = 0;
    std::uint32_t inputReleaseSequence = 0;
    bool inputReleaseSent = false;
    std::size_t viewpointIndex = 0;
    std::vector<dev::JsonValue> viewResults;
    std::string captureStem;
    std::filesystem::path pendingCapturePath;
    std::chrono::steady_clock::time_point deadline = {};
    std::chrono::steady_clock::time_point benchmarkPhaseStart = {};
    std::uint64_t benchmarkPhaseFrames = 0;
    std::vector<benchmark::FrameSample> benchmarkSamples;
    std::vector<benchmark::SimulationTickSample> benchmarkTickSamples;
    benchmark::TimingValues lastBenchmarkFrameTiming;
    std::map<std::string, std::string, std::less<>> restoredCvars;
    std::size_t benchmarkScreenshotIndex = 0;
    std::vector<std::string> benchmarkScreenshotPaths;
    bool previousDevelopmentCameraEnabled = false;
    dev::CameraTransform previousDevelopmentCamera;
    Weapon previousSelectedWeapon = Weapon::LightningGun;
    int previousAttack = 0;
    BotAttackMode previousBotAttackMode = BotAttackMode::Off;
    Weapon previousBotWeapon = Weapon::MachineGun;
    bool previousBotWeaponAuto = true;
    bool previousBotStare = true;
    bool previousBotStandstill = false;
    bool previousBotDodge = false;
    std::int32_t previousBotDodgeMinIntervalMs = 250;
    std::int32_t previousBotDodgeMaxIntervalMs = 750;
    int previousBotCount = 0;
    bool benchmarkBotsConfigured = false;
  };
  std::optional<ActiveControlOperation> activeControlOperation;
  struct ArmedPhaseCapture {
    std::string name;
    std::string phase;
    bool hideHud = true;
    bool hideOverlays = true;
    std::filesystem::path path;
    std::optional<dev::JsonValue> result;
    std::string error;
    std::chrono::steady_clock::time_point deadline = {};
  };
  std::optional<ArmedPhaseCapture> armedPhaseCapture;
  std::array<std::uint64_t, kNetworkTelemetryHistorySamples>
    netGraphCorrectionSerials = {};
  std::array<float, kNetworkTelemetryHistorySamples>
    netGraphCorrectionDistances = {};
  std::array<std::uint64_t, kNetworkTelemetryHistorySamples>
    netGraphUnderrunSerials = {};
  std::array<std::uint64_t, kNetworkTelemetryHistorySamples>
    netGraphHardCorrectionSerials = {};
  std::uint32_t lastNetGraphCorrectionCount = 0;
  std::uint32_t lastNetGraphUnderrunCount = 0;
  std::uint32_t lastNetGraphHardCorrectionCount = 0;

  const auto registerButtonCommand =
    [&console](std::string name, int& pressCount) {
      console.registerCommand(
        '+' + name,
        "Begin " + name + '.',
        [&pressCount](const std::vector<std::string>&) {
          ++pressCount;
          return std::string{};
        }
      );
      console.registerCommand(
        '-' + name,
        "End " + name + '.',
        [&pressCount](const std::vector<std::string>&) {
          pressCount = std::max(0, pressCount - 1);
          return std::string{};
        }
      );
    };
  registerButtonCommand("forward", input.forward);
  registerButtonCommand("back", input.back);
  registerButtonCommand("moveleft", input.left);
  registerButtonCommand("moveright", input.right);
  registerButtonCommand("moveup", input.up);
  registerButtonCommand("movedown", input.down);
  registerButtonCommand("duck", input.down);
  registerButtonCommand("crouch", input.down);
  registerButtonCommand("speed", input.sneak);
  registerButtonCommand("sneak", input.sneak);
  registerButtonCommand("attack", input.attack);
  registerButtonCommand("dash", input.dash);
  registerButtonCommand("scores", scoreboardPressCount);
  registerButtonCommand("showchat", chatHistoryPressCount);
  registerButtonCommand("zoom", zoomPressCount);

  console.registerCommand(
    "weapon",
    "Select weapon: weapon <mg|sg|gl|rl|lg|sr|pg|fg|re|1..9>.",
    [&selectedWeapon](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: weapon <mg|sg|gl|rl|lg|sr|pg|fg|re|1..9>");
      }
      const std::optional<Weapon> parsed = parseWeaponToken(arguments[1]);
      if (parsed.has_value()) {
        selectedWeapon = *parsed;
        return std::string("weapon = ") + std::string(weaponShortName(*parsed));
      }
      return std::string("usage: weapon <mg|sg|gl|rl|lg|rg|pg|fg|1..8>");
    }
  );
  console.registerCommand(
    "bot_dodge",
    "Toggle BOT random left/right movement: bot_dodge [0|1] [min_ms max_ms].",
    [&botDodgeEnabled,
     &botDodgeMinIntervalMs,
     &botDodgeMaxIntervalMs,
     &pendingBotCommands,
     &session](
      const std::vector<std::string>& arguments
    ) {
      auto parseInt = [](const std::string& text, int& value) {
        const auto result =
          std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} &&
          result.ptr == text.data() + text.size();
      };

      bool enabled = !botDodgeEnabled;
      std::size_t intervalArgument = 1;
      if (arguments.size() >= 2) {
        if (
          arguments[1] == "1" ||
          arguments[1] == "on" ||
          arguments[1] == "true"
        ) {
          enabled = true;
          intervalArgument = 2;
        } else if (
          arguments[1] == "0" ||
          arguments[1] == "off" ||
          arguments[1] == "false"
        ) {
          enabled = false;
          intervalArgument = 2;
        }
      }

      int minMs = botDodgeMinIntervalMs;
      int maxMs = botDodgeMaxIntervalMs;
      if (arguments.size() > intervalArgument) {
        if (arguments.size() != intervalArgument + 2) {
          return std::string("usage: bot_dodge [0|1] [min_ms max_ms]");
        }
        if (
          !parseInt(arguments[intervalArgument], minMs) ||
          !parseInt(arguments[intervalArgument + 1], maxMs)
        ) {
          return std::string("usage: bot_dodge [0|1] [min_ms max_ms]");
        }
      }
      minMs = std::clamp(minMs, 1, 10000);
      maxMs = std::clamp(maxMs, 1, 10000);
      if (minMs > maxMs) {
        std::swap(minMs, maxMs);
      }
      if (!session.connected()) {
        return std::string("not connected");
      }
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Dodge,
        enabled ? 1 : 0,
        minMs,
        maxMs,
      });
      return std::string("bot_dodge requested");
    }
  );
  console.registerCommand(
    "bot_dodge_min_ms",
    "Set minimum bot dodge interval: bot_dodge_min_ms <milliseconds>.",
    [&botDodgeEnabled,
     &botDodgeMinIntervalMs,
     &botDodgeMaxIntervalMs,
     &pendingBotCommands,
     &session](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return "bot_dodge_min_ms = " + std::to_string(botDodgeMinIntervalMs);
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_dodge_min_ms <milliseconds>");
      }
      int minMs = 0;
      const auto result =
        std::from_chars(arguments[1].data(), arguments[1].data() + arguments[1].size(), minMs);
      if (result.ec != std::errc{} || result.ptr != arguments[1].data() + arguments[1].size()) {
        return std::string("usage: bot_dodge_min_ms <milliseconds>");
      }
      minMs = std::clamp(minMs, 1, 10000);
      int maxMs = std::max(botDodgeMaxIntervalMs, minMs);
      if (!session.connected()) {
        return std::string("not connected");
      }
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Dodge,
        botDodgeEnabled ? 1 : 0,
        minMs,
        maxMs,
      });
      return std::string("bot_dodge_min_ms requested");
    }
  );
  console.registerCommand(
    "bot_dodge_max_ms",
    "Set maximum bot dodge interval: bot_dodge_max_ms <milliseconds>.",
    [&botDodgeEnabled,
     &botDodgeMinIntervalMs,
     &botDodgeMaxIntervalMs,
     &pendingBotCommands,
     &session](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return "bot_dodge_max_ms = " + std::to_string(botDodgeMaxIntervalMs);
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_dodge_max_ms <milliseconds>");
      }
      int maxMs = 0;
      const auto result =
        std::from_chars(arguments[1].data(), arguments[1].data() + arguments[1].size(), maxMs);
      if (result.ec != std::errc{} || result.ptr != arguments[1].data() + arguments[1].size()) {
        return std::string("usage: bot_dodge_max_ms <milliseconds>");
      }
      maxMs = std::clamp(maxMs, 1, 10000);
      int minMs = std::min(botDodgeMinIntervalMs, maxMs);
      if (!session.connected()) {
        return std::string("not connected");
      }
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Dodge,
        botDodgeEnabled ? 1 : 0,
        minMs,
        maxMs,
      });
      return std::string("bot_dodge_max_ms requested");
    }
  );
  const auto parseBotBool = [](const std::string& text, bool& value) {
    if (text == "1" || text == "on" || text == "true") {
      value = true;
      return true;
    }
    if (text == "0" || text == "off" || text == "false") {
      value = false;
      return true;
    }
    return false;
  };
  const auto parseBotInt = [](const std::string& text, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
  };
  const auto queueBotCommand =
    [&pendingBotCommands, &session](PendingBotCommand command) {
      if (!session.connected()) {
        return std::string("not connected");
      }
      pendingBotCommands.push_back(command);
      return std::string("bot command requested");
    };
  console.registerCommand(
    "bot_add",
    "Request server training bots: bot_add [count].",
    [&queueBotCommand, &parseBotInt](const std::vector<std::string>& arguments) {
      if (arguments.size() > 2) {
        return std::string("usage: bot_add [count]");
      }
      int count = -1;
      if (arguments.size() == 2 && (!parseBotInt(arguments[1], count) || count < 0)) {
        return std::string("usage: bot_add [count]");
      }
      return queueBotCommand(PendingBotCommand{BotCommandType::Add, count});
    }
  );
  console.registerCommand(
    "bot_kick",
    "Request removal of server training bots: bot_kick all|<slot>.",
    [&queueBotCommand, &parseBotInt](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: bot_kick all|<slot>");
      }
      if (arguments[1] == "all") {
        return queueBotCommand(PendingBotCommand{BotCommandType::KickAll, 0});
      }
      int slot = 0;
      if (!parseBotInt(arguments[1], slot) || slot < 1 || slot > static_cast<int>(kDuelPlayerCount)) {
        return std::string("usage: bot_kick all|<slot>");
      }
      return queueBotCommand(PendingBotCommand{BotCommandType::KickSlot, slot});
    }
  );
  console.registerCommand(
    "bot_attack",
    "Request server training bot combat mode: bot_attack [0|off|easy|medium|hard].",
    [&botAttackMode, &queueBotCommand](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return std::string("bot_attack = ") + botAttackModeCvarValue(botAttackMode);
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_attack 0|off|easy|medium|hard");
      }
      const std::optional<BotAttackMode> mode = parseBotAttackMode(arguments[1]);
      if (!mode.has_value()) {
        return std::string("usage: bot_attack 0|off|easy|medium|hard");
      }
      return queueBotCommand(PendingBotCommand{
        BotCommandType::AttackMode,
        static_cast<std::int32_t>(*mode),
      });
    }
  );
  console.registerCommand(
    "bot_difficulty",
    "Request normal bot skill: bot_difficulty easy|medium|hard.",
    [&botAttackMode, &queueBotCommand](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return std::string("bot_difficulty = ") + botAttackModeCvarValue(botAttackMode);
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_difficulty easy|medium|hard");
      }
      const std::optional<BotAttackMode> mode = parseBotAttackMode(arguments[1]);
      if (!mode.has_value() || *mode == BotAttackMode::Off) {
        return std::string("usage: bot_difficulty easy|medium|hard");
      }
      return queueBotCommand(PendingBotCommand{
        BotCommandType::AttackMode, static_cast<std::int32_t>(*mode),
      });
    }
  );
  console.registerCommand(
    "bot_weapon",
    "Force a bot weapon or return to auto choice: bot_weapon [auto|mg|sg|gl|rl|lg|sr|pg|fg|re|1..9].",
    [&botWeapon, &botWeaponAuto, &queueBotCommand](const std::vector<std::string>& arguments) {
      if (arguments.size() == 1) {
        return std::string("bot_weapon = ") +
          (botWeaponAuto ? "auto" : std::string(weaponShortName(botWeapon)));
      }
      if (arguments.size() != 2) {
        return std::string("usage: bot_weapon auto|mg|sg|gl|rl|lg|sr|pg|fg|re|1..9");
      }
      if (arguments[1] == "auto") {
        botWeaponAuto = true;
        return queueBotCommand(PendingBotCommand{BotCommandType::Weapon, -1});
      }
      const std::optional<Weapon> weapon = parseWeaponToken(arguments[1]);
      if (!weapon.has_value()) {
        return std::string("usage: bot_weapon auto|mg|sg|gl|rl|lg|sr|pg|fg|re|1..9");
      }
      botWeaponAuto = false;
      return queueBotCommand(PendingBotCommand{
        BotCommandType::Weapon,
        static_cast<std::int32_t>(*weapon),
      });
    }
  );
  console.registerCommand(
    "bot_stare",
    "Request passive server bot staring: bot_stare [0|1].",
    [&botStareEnabled, &queueBotCommand, &parseBotBool](
      const std::vector<std::string>& arguments
    ) {
      bool enabled = !botStareEnabled;
      if (arguments.size() > 2) {
        return std::string("usage: bot_stare [0|1]");
      }
      if (arguments.size() == 2 && !parseBotBool(arguments[1], enabled)) {
        return std::string("usage: bot_stare [0|1]");
      }
      return queueBotCommand(PendingBotCommand{
        BotCommandType::Stare,
        enabled ? 1 : 0,
      });
    }
  );
  console.registerCommand(
    "bot_standstill",
    "Request server bot standstill mode: bot_standstill [0|1].",
    [&botStandstillEnabled, &queueBotCommand, &parseBotBool](
      const std::vector<std::string>& arguments
    ) {
      bool enabled = !botStandstillEnabled;
      if (arguments.size() > 2) {
        return std::string("usage: bot_standstill [0|1]");
      }
      if (arguments.size() == 2 && !parseBotBool(arguments[1], enabled)) {
        return std::string("usage: bot_standstill [0|1]");
      }
      return queueBotCommand(PendingBotCommand{
        BotCommandType::Standstill,
        enabled ? 1 : 0,
      });
    }
  );
  console.registerCommand(
    "player",
    "Set your player name: player <name>.",
    [&console, &pendingPlayerName](const std::vector<std::string>& arguments) {
      if (arguments.size() < 2) {
        return std::string("usage: player <name>");
      }
      std::string name = arguments[1];
      for (std::size_t index = 2; index < arguments.size(); ++index) {
        name += ' ' + arguments[index];
      }
      if (name.size() > kMaxPlayerNameBytes) {
        return "player name is limited to " +
          std::to_string(kMaxPlayerNameBytes) + " characters";
      }
      const std::string result = console.execute("set cl_player_name \"" + name + '"');
      if (!result.starts_with("cl_player_name = ")) {
        return result;
      }
      pendingPlayerName = name;
      return "name = " + pendingPlayerName;
    }
  );
  console.registerCommand(
    "map",
    "Request a server map change: map <name> loads maps/<name>.map.",
    [&pendingMapName](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: map <name>");
      }
      const std::string& name = arguments[1];
      if (name.empty() || name.size() > kMaxMapNameBytes) {
        return "map name is limited to " +
          std::to_string(kMaxMapNameBytes) + " characters";
      }
      const std::filesystem::path requested(name);
      if (requested.has_parent_path() || requested.filename().string() != name) {
        return std::string("map name may not include a path");
      }
      const std::string extension = requested.extension().string();
      if (!extension.empty() && extension != ".map") {
        return std::string("map extension must be .map");
      }
      const std::string stem = extension.empty() ? name : requested.stem().string();
      if (stem.empty()) {
        return std::string("map name may not be empty");
      }
      for (const unsigned char character : stem) {
        if (
          !std::isalnum(character) &&
          character != '_' &&
          character != '-'
        ) {
          return std::string("map name may only use letters, numbers, _ and -");
        }
      }
      pendingMapName = name;
      return "map change requested: " + pendingMapName;
    }
  );
  console.registerCommand(
    "quit",
    "Quit the client.",
    [&quitRequested](const std::vector<std::string>&) {
      quitRequested = true;
      return "quitting";
    }
  );
  console.registerCommand(
    "ready",
    "Toggle ready state while waiting for a match.",
    [&readyRequested](const std::vector<std::string>&) {
      readyRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "mcguffin_throw",
    "Throw the carried McGuffin along the current aim direction.",
    [&mcguffinThrowRequested](const std::vector<std::string>&) {
      mcguffinThrowRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "spectate_next",
    "Follow the next available living teammate while spectating.",
    [&pendingSpectateCycle](const std::vector<std::string>&) {
      pendingSpectateCycle = 1;
      return std::string{};
    }
  );
  console.registerCommand(
    "spectate_prev",
    "Follow the previous available living teammate while spectating.",
    [&pendingSpectateCycle](const std::vector<std::string>&) {
      pendingSpectateCycle = -1;
      return std::string{};
    }
  );
  console.registerCommand(
    "gamemode",
    "Select the active gamemode: gamemode <duel|ca|mcguffin>.",
    [&requestGameModePending, &requestedGameMode](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: gamemode <duel|ca|mcguffin>");
      }
      std::string value = arguments[1];
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (value == "duel") {
        requestedGameMode = GameMode::Duel;
      } else if (value == "ca" || value == "clanarena" || value == "clan_arena") {
        requestedGameMode = GameMode::ClanArena;
      } else if (value == "mcg" || value == "mcguffin") {
        requestedGameMode = GameMode::McGuffin;
      } else {
        return std::string("usage: gamemode <duel|ca|mcguffin>");
      }
      requestGameModePending = true;
      return std::string("gamemode = ") + gameModeName(requestedGameMode);
    }
  );
  console.registerCommand(
    "team",
    "Select a team or become an observer: team <red|blue|none|spectator>.",
    [&requestTeamPending, &requestSpectatorPending, &requestedTeam](
      const std::vector<std::string>& arguments
    ) {
      if (arguments.size() != 2) {
        return std::string("usage: team <red|blue|none|spectator>");
      }
      std::string value = arguments[1];
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (value == "red") {
        requestedTeam = Team::Red;
      } else if (value == "blue") {
        requestedTeam = Team::Blue;
      } else if (value == "none" || value == "unassigned") {
        requestedTeam = Team::None;
      } else if (value == "spectator" || value == "spec") {
        requestSpectatorPending = true;
        requestTeamPending = false;
        return std::string("team = spectator");
      } else {
        return std::string("usage: team <red|blue|none|spectator>");
      }
      requestSpectatorPending = false;
      requestTeamPending = true;
      return std::string("team = ") + teamName(requestedTeam);
    }
  );

  console.registerCommand(
    "connect",
    "Connect to a server: connect <host> [port], or connect <port> for localhost.",
    [&session](const std::vector<std::string>& arguments) {
      if (arguments.size() < 2 || arguments.size() > 3) {
        return std::string("usage: connect <host> [port]");
      }
      std::string host = arguments[1];
      std::uint16_t port = 27960;
      const auto parsePort = [](std::string_view text, std::uint16_t& parsed) {
        unsigned int value = 0;
        const auto result = std::from_chars(
          text.data(),
          text.data() + text.size(),
          value
        );
        if (
          result.ec != std::errc{} ||
          result.ptr != text.data() + text.size() ||
          value == 0 ||
          value > 65535U
        ) {
          return false;
        }
        parsed = static_cast<std::uint16_t>(value);
        return true;
      };
      if (arguments.size() == 2) {
        std::uint16_t shorthandPort = 0;
        if (parsePort(arguments[1], shorthandPort)) {
          host = "127.0.0.1";
          port = shorthandPort;
        }
      } else if (!parsePort(arguments[2], port)) {
        return std::string("invalid UDP port");
      }
      return session.connect(std::move(host), port)
        ? session.statusMessage()
        : "connect failed: " + session.statusMessage();
    }
  );
  console.registerCommand(
    "disconnect",
    "Disconnect from the current server.",
    [&session](const std::vector<std::string>&) {
      session.disconnect();
      return std::string("Disconnected");
    }
  );
  console.registerCommand(
    "reconnect",
    "Reconnect to the most recently used server.",
    [&session](const std::vector<std::string>&) {
      return session.reconnect()
        ? session.statusMessage()
        : "reconnect failed: " + session.statusMessage();
    }
  );
  console.registerCommand(
    "clear",
    "Clear console scrollback.",
    [&clearRequested](const std::vector<std::string>&) {
      clearRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "writeconfig",
    "Write archived client cvars.",
    [&writeConfigRequested](const std::vector<std::string>&) {
      writeConfigRequested = true;
      return "writing client config";
    }
  );
  console.registerCommand(
    "toggleconsole",
    "Toggle the client console.",
    [&toggleConsoleRequested](const std::vector<std::string>&) {
      toggleConsoleRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "settings",
    "Open the in-game Settings menu.",
    [&settingsMenuRequested](const std::vector<std::string>&) {
      settingsMenuRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
      "misc", "Open the tools and debug menu.",
      [&miscMenuRequested](const std::vector<std::string> &) {
        miscMenuRequested = true;
        return std::string{};
      });
  console.registerCommand(
    "messagemode",
    "Open team-wide chat input.",
    [&openChatRequested](const std::vector<std::string>&) {
      openChatRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "showchat",
    "Show chat history for five seconds.",
    [&showChatRequested](const std::vector<std::string>&) {
      showChatRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "resetmatch",
    "Request an authoritative match reset.",
    [&resetRequested](const std::vector<std::string>&) {
      resetRequested = true;
      return std::string{};
    }
  );
  console.registerCommand(
    "bind",
    "Bind a key to a command.",
    [&bindings](const std::vector<std::string>& arguments) {
      if (arguments.size() == 2) {
        const std::string command = bindings.binding(arguments[1]);
        return command.empty()
          ? InputBindings::normalizeKey(arguments[1]) + " is unbound"
          : InputBindings::normalizeKey(arguments[1]) + " = " + command;
      }
      if (arguments.size() < 3) {
        return std::string("usage: bind <key> <command>");
      }
      std::string command = arguments[2];
      for (std::size_t index = 3; index < arguments.size(); ++index) {
        command += ' ' + arguments[index];
      }
      if (!bindings.bind(arguments[1], command)) {
        return std::string("invalid binding");
      }
      return InputBindings::normalizeKey(arguments[1]) + " = " + command;
    }
  );
  console.registerCommand(
    "unbind",
    "Remove a key binding.",
    [&bindings, &console](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: unbind <key>");
      }
      for (const std::string& command : bindings.unbind(arguments[1])) {
        (void)console.execute(command);
      }
      return InputBindings::normalizeKey(arguments[1]) + " unbound";
    }
  );
  console.registerCommand(
    "unbindall",
    "Remove every key binding.",
    [&bindings, &console](const std::vector<std::string>&) {
      for (const std::string& command : bindings.unbindAll()) {
        (void)console.execute(command);
      }
      return std::string{};
    }
  );
  console.registerCommand(
    "bindlist",
    "List key bindings.",
    [&bindings](const std::vector<std::string>&) {
      std::string result;
      for (const std::string& line : bindings.list()) {
        result += line + '\n';
      }
      return result;
    }
  );
  console.registerCommand(
      "actionlist",
      "List bindable gameplay actions using Quake 3 command names.",
      [](const std::vector<std::string> &) {
        return std::string("+forward\n"
                           "+back\n"
                           "+moveleft\n"
                           "+moveright\n"
                           "+moveup\n"
                           "+movedown\n"
                           "+duck\n"
                           "+crouch\n"
                           "+speed\n"
                           "+sneak\n"
                           "+attack\n"
                           "+dash\n"
                           "+scores\n"
                           "+showchat\n"
                           "+zoom\n"
                           "weapon\n"
                           "map\n"
                           "player\n"
                           "resetmatch\n"
                           "ready\n"
                           "mcguffin_throw\n"
                           "gamemode\n"
                           "team\n"
                           "bot_add\n"
                           "bot_kick\n"
                           "bot_attack\n"
                           "bot_weapon\n"
                           "bot_dodge\n"
                           "bot_dodge_min_ms\n"
                           "bot_dodge_max_ms\n"
                           "bot_stare\n"
                           "bot_standstill\n"

                           "settings\n"
                           "misc\n"
                           "messagemode\n"
                           "showchat\n"
                           "toggleconsole\n"
                           "quit");
      });
  console.registerCommand(
    "net_stats",
    "Print current connection diagnostics.",
    [&session](const std::vector<std::string>&) {
      const ClientNetworkSimulationConfig config = session.networkSimulationConfig();
      const ClientNetworkSimulationStats stats = session.networkSimulationStats();
      const NetworkTelemetry telemetry = session.networkTelemetry();
      const SnapshotInterpolation::Diagnostics interpolation =
        session.game() != nullptr
          ? session.game()->interpolationDiagnostics()
          : SnapshotInterpolation::Diagnostics{};
      char text[1024];
      std::snprintf(
        text,
        sizeof(text),
        "state=%d host=%s port=%u client=%zu body=%d ping=%.1fms jitter=%.1fms loss={in=%.1f%% out=%.1f%%} rate=%.1f/s bw={in=%.0f out=%.0f}kbit packet={snap=%zu cmd=%zu} age=%.1fms sim={lat=%dms jit=%dms loss=%d%% reorder=%d%% seed=%u qout=%zu qin=%zu drop=%llu/%llu reorder=%llu/%llu} interp={lead=%.2fms target=%.2fms error=%.2fms delay=%.2fms rate=%.3f started=%d underrun=%d/%u hard=%u buffered=%zu tick=%.3f newest=%.0f}",
        static_cast<int>(session.state()),
        std::string(session.host()).c_str(),
        static_cast<unsigned int>(session.port()),
        session.clientIndex(),
        session.spectator() ? -1 : static_cast<int>(session.playerIndex()),
        telemetry.pingMilliseconds,
        telemetry.snapshotJitterMilliseconds,
        telemetry.incomingLossPercent,
        telemetry.outgoingLossPercent,
        telemetry.snapshotRate,
        telemetry.incomingKilobitsPerSecond,
        telemetry.outgoingKilobitsPerSecond,
        telemetry.lastSnapshotBytes,
        telemetry.lastCommandBytes,
        telemetry.snapshotAgeMilliseconds,
        config.latencyMs,
        config.jitterMs,
        config.lossPercent,
        config.reorderPercent,
        static_cast<unsigned int>(config.seed),
        stats.queuedOutgoingPackets,
        stats.queuedIncomingPackets,
        static_cast<unsigned long long>(stats.droppedOutgoingPackets),
        static_cast<unsigned long long>(stats.droppedIncomingPackets),
        static_cast<unsigned long long>(stats.reorderedOutgoingPackets),
        static_cast<unsigned long long>(stats.reorderedIncomingPackets),
        interpolation.bufferLeadTicks * 1000.0 / static_cast<double>(kFixedTickRate),
        interpolation.desiredBufferLeadTicks * 1000.0 / static_cast<double>(kFixedTickRate),
        interpolation.timelineErrorTicks * 1000.0 / static_cast<double>(kFixedTickRate),
        static_cast<double>(interpolation.effectiveDelaySeconds) * 1000.0,
        static_cast<double>(interpolation.playbackRate),
        interpolation.playbackStarted ? 1 : 0,
        interpolation.bufferUnderrun ? 1 : 0,
        interpolation.underrunCount,
        interpolation.hardCorrectionCount,
        interpolation.bufferedSnapshotCount,
        interpolation.presentationTick,
        interpolation.newestSnapshotTick
      );
      return std::string(text);
    }
  );
  const std::filesystem::path defaultConfigPath =
    defaultClientConfigPath(assetBasePath);
  const char* liveScenarioEnvironment =
    std::getenv("LG_DUEL_LIVE_SCENARIO");
  const bool ownedLiveScenario =
    developerControl_.enabled &&
    liveScenarioEnvironment != nullptr &&
    std::string_view(liveScenarioEnvironment) == "1";
  if (std::filesystem::exists(defaultConfigPath)) {
    loadClientConfig(console, defaultConfigPath.string());
  } else {
    installDefaultBindings(bindings);
    std::cerr << "Config warning: config/default_client.cfg not found; using code default binds\n";
  }
  // Owned evidence runs use shipped defaults. Personal graphics values would
  // make fixed-camera captures differ between machines.
  if (!ownedLiveScenario) {
    loadClientConfig(console, configPath);
  }
  loadSoundMixerConfigs(console, assetBasePath);
  if (console.getInt("cl_config_version") < 7) {
    (void)bindings.bind("f3", "ready");
    (void)bindings.bind("t", "messagemode");
    (void)bindings.bind("z", "+showchat");
    (void)bindings.bind("tab", "+scores");
    if (bindings.binding("mouse2").empty()) {
      (void)bindings.bind("mouse2", "+zoom");
    }
    if (bindings.binding("mouse3").empty()) {
      (void)bindings.bind("mouse3", "+dash");
    }
    (void)bindings.bind("1", "weapon mg");
    (void)bindings.bind("2", "weapon sg");
    (void)bindings.bind("3", "weapon gl");
    (void)bindings.bind("4", "weapon rl");
    (void)bindings.bind("5", "weapon lg");
    (void)bindings.bind("6", "weapon rg");
    (void)bindings.bind("7", "weapon pg");
    (void)bindings.bind("8", "weapon fg");
    (void)bindings.bind("q", "weapon rl");
    (void)bindings.bind("e", "weapon lg");
    (void)bindings.bind("r", "weapon rg");
    if (bindings.binding("f5").empty()) {
      (void)bindings.bind("f5", "resetmatch");
    }
    (void)console.execute("set cl_config_version 7");
  }
  if (console.getInt("cl_config_version") < 8) {
    (void)console.execute(
      console.getBool("r_vsync")
        ? "set r_present_mode 0"
        : "set r_present_mode 2"
    );
    (void)console.execute("set cl_legacy_frame_delay 0");
    (void)console.execute("set cl_config_version 8");
  }
  if (console.getInt("cl_config_version") < 9) {
    if (bindings.binding("f10").empty()) {
      (void)bindings.bind("f10", "settings");
    }
    (void)console.execute("set cl_config_version 9");
  }
  if (console.getInt("cl_config_version") < 10) {
    (void)console.execute("set cl_config_version 10");
  }
  if (console.getInt("cl_config_version") < 11) {
    if (bindings.binding("leftshift") == "+movedown") {
      (void)bindings.bind("leftshift", "+speed");
    }
    if (bindings.binding("rightshift") == "+movedown") {
      (void)bindings.bind("rightshift", "+speed");
    }
    (void)console.execute("set cl_config_version 11");
  }
  if (console.getInt("cl_config_version") < 13) {
    const float migratedSensitivity =
      console.getFloat("sensitivity") * kLegacyToQuakeLiveSensitivityScale;
    (void)console.execute("set sensitivity " + std::to_string(migratedSensitivity));
    (void)console.execute("set cl_config_version 13");
  }
  if (console.getInt("cl_config_version") < 14) {
    if (bindings.binding("9").empty()) {
      (void)bindings.bind("9", "weapon re");
    }
    (void)console.execute("set cl_config_version 14");
  }
  if (console.getInt("cl_config_version") < 15) {
    if (bindings.binding("z") == "showchat") {
      (void)bindings.bind("z", "+showchat");
    }
    (void)console.execute("set cl_config_version 15");
  }
  if (console.getInt("cl_config_version") < 16) {
    if (bindings.binding("g").empty()) {
      (void)bindings.bind("g", "mcguffin_throw");
    }
    (void)console.execute("set cl_config_version 16");
  }
  if (console.getInt("cl_config_version") < 17) {
    const std::string f11Binding = bindings.binding("f11");
    if (f11Binding.empty()) {
      (void)bindings.bind("f11", "misc");
    }
    const std::string f12Binding = bindings.binding("f12");
    if (f12Binding == "+quit" || f12Binding == "quit") {
      (void)bindings.unbind("f12");
    }
    (void)console.execute("set cl_config_version 17");
  }
  if (ownedLiveScenario) {
    // Owned live runs use a fixed window size so capture checks do not inherit
    // the user's archived video settings.
    (void)console.execute("set vid_fullscreen 0");
    (void)console.execute("set vid_width 1280");
    (void)console.execute("set vid_height 720");
  }
  (void)session.connect(serverHost_, serverPort_);
  ClientConsoleState consoleState;
  SettingsMenuState settingsMenu;
  MiscMenuState miscMenu;
  appendConsoleOutput(
    consoleState,
    "LG Duel console. Type actionlist, bindlist, cmdlist, or cvarlist."
  );
  VideoRuntimeState videoRuntime;
  (void)applyVideoSettings(
    window,
    renderer,
    console,
    videoRuntime,
    videoSettingsFromConsole(console),
    consoleState
  );
  bool lastCompatVSync = console.getBool("r_vsync");
  int lastPresentModeInt = console.getInt("r_present_mode");
  bool suppressNextTextInput = false;
  const auto executeBindingCommands =
    [&console, &consoleState](const std::vector<std::string>& commands) {
      for (const std::string& command : commands) {
        const std::string result = console.execute(command);
        if (!result.empty()) {
          appendConsoleOutput(consoleState, result);
        }
      }
    };
  const auto setConsoleOpen =
    [&bindings, &console, &consoleState, &input, window](bool open) {
      if (consoleState.open == open) {
        return;
      }
      for (const std::string& command : bindings.releaseAll()) {
        (void)console.execute(command);
      }
      consoleState.open = open;
      clearConsoleSelection(consoleState);
      consoleState.historyIndex = consoleState.history.size();
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
      if (open) {
        consoleState.scrollRows = 0U;
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        consoleState.cat.reset(
          static_cast<float>(width),
          static_cast<float>(height)
        );
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_StartTextInput(window);
      } else {
        SDL_StopTextInput(window);
        SDL_SetWindowRelativeMouseMode(window, true);
      }
    };
  const auto applyConsoleToggle =
    [&toggleConsoleRequested, &setConsoleOpen, &consoleState]() {
      if (toggleConsoleRequested) {
        toggleConsoleRequested = false;
        setConsoleOpen(!consoleState.open);
      }
    };
  const auto setChatOpen =
    [&bindings, &console, &chatState, &input, window](bool open) {
      if (chatState.inputOpen == open) {
        return;
      }
      for (const std::string& command : bindings.releaseAll()) {
        (void)console.execute(command);
      }
      chatState.inputOpen = open;
      if (open) {
        chatState.cursorIndex = chatState.input.size();
        chatState.scrollRows = 0U;
        clearChatSelections(chatState);
      }
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
      if (open) {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_StartTextInput(window);
      } else {
        SDL_StopTextInput(window);
        SDL_SetWindowRelativeMouseMode(window, true);
      }
    };
  const auto setSettingsOpen = [&bindings, &console, &settingsMenu, &input,
                                window](bool open) {
    if (settingsMenu.open == open) {
      return;
    }
    for (const std::string &command : bindings.releaseAll()) {
      (void)console.execute(command);
    }
    settingsMenu.open = open;
    input.mouseDeltaX = 0.0F;
    input.mouseDeltaY = 0.0F;
    if (open) {
      syncSettingsMenuFromConsole(settingsMenu, console);
      SDL_SetWindowRelativeMouseMode(window, false);
      SDL_ShowCursor();
    } else {
      settingsMenu.hoveredRow = -1;
      settingsMenu.pressedRow = -1;
      settingsMenu.scrollbarDragging = false;
      SDL_SetWindowRelativeMouseMode(window, true);
      SDL_HideCursor();
    }
  };
  const auto setMiscMenuOpen = [&bindings, &console, &miscMenu, &input,
                                window](bool open) {
    if (miscMenu.open == open) {
      return;
    }
    for (const std::string &command : bindings.releaseAll()) {
      (void)console.execute(command);
    }
    miscMenu.open = open;
    input.mouseDeltaX = 0.0F;
    input.mouseDeltaY = 0.0F;
    if (open) {
      miscMenu.selectedRow = std::clamp(
          miscMenu.selectedRow, 0, static_cast<int>(MiscMenuRow::Count) - 1);
      miscMenu.scrollRows = 0U;
      SDL_SetWindowRelativeMouseMode(window, false);
      SDL_ShowCursor();
    } else {
      miscMenu.hoveredRow = -1;
      miscMenu.pressedRow = -1;
      miscMenu.scrollbarDragging = false;
      SDL_SetWindowRelativeMouseMode(window, true);
      SDL_HideCursor();
    }
  };
  const auto applyMiscMenuToggle = [&miscMenuRequested, &setMiscMenuOpen,
                                    &miscMenu]() {
    if (miscMenuRequested) {
      miscMenuRequested = false;
      setMiscMenuOpen(!miscMenu.open);
    }
  };
  const auto applySettingsMenuToggle =
    [&settingsMenuRequested, &setSettingsOpen, &settingsMenu]() {
      if (settingsMenuRequested) {
        settingsMenuRequested = false;
        setSettingsOpen(!settingsMenu.open);
      }
    };

  const Arena fallbackArena;
  std::uint32_t commandSequence = 0;
  std::uint32_t clientTick = 0;

  using Clock = std::chrono::steady_clock;
  auto previousTime = Clock::now();
  const auto appStartTime = previousTime;
  auto previousOuterFrameStart = previousTime;
  Clock::time_point nextFrameDeadline = previousTime;
  int appliedMaxFps = console.getInt("r_maxfps");
  float accumulatorSeconds = 0.0F;
  float titleAccumulatorSeconds = 0.0F;
  float frameStatsAccumulatorSeconds = 0.0F;
  constexpr float kWeaponSwitchDurationSeconds = 0.16F;
  float droppedSimulationSeconds = 0.0F;
  std::uint32_t overloadFrameCount = 0;
  std::uint32_t renderedFrameCount = 0;
  float displayedFramesPerSecond = 0.0F;
  FrameTimeHistory outerFrameTimes;
  FrameTimeSummary displayedFrameTimes;
  PerfTelemetry perfTelemetry;
  PresentationViewState presentationView;
  bool lateMouseSamplingWasEnabled = false;
  float pendingLateViewModelMouseDeltaX = 0.0F;
  float pendingLateViewModelMouseDeltaY = 0.0F;
  std::array<PlayerPresentationState, kDuelPlayerCount> playerPresentationStates = {};
  ViewModelPresentationController viewModelPresentation;
  ClientGame* presentationViewGame = nullptr;
  bool previousFrameUsedPresentationView = false;
  float localDeathElapsedSeconds = 0.0F;
  std::optional<std::size_t> deathSpectatorTarget;
  bool wasTeammateSpectating = false;
  bool previousSpectateAttackDown = false;
  bool previousSpectateZoomDown = false;
  MovementTuning lastRequestedMovementTuning = movementTuningFromCvars(console);
  float lastRequestedPlayerSizeScaleXY =
    console.getFloat("g_playersize_xy");
  float lastRequestedPlayerSizeScaleZ =
    console.getFloat("g_playersize_z");
  float lastRequestedLightningKnockback =
    console.getFloat("g_lg_knockback");
  float lastRequestedLightningFireHz =
    console.getFloat("g_lg_fire_hz");
  float lastRequestedRocketKnockback =
    console.getFloat("g_rl_knockback");
  std::int32_t lastRequestedKnockbackTimeMs =
    knockbackTimeMsFromCvars(console);
  WeaponDamageTuning lastRequestedWeaponDamage =
    weaponDamageTuningFromCvars(console);
  float lastRequestedVampirism =
    console.getFloat("g_vampirism");
  std::uint8_t lastRequestedSelfDamagePercent =
    selfDamagePercent(console);
  std::int32_t lastRequestedHealthAmount =
    healthAmountFromCvars(console);
  WeaponAmmoConfig lastRequestedWeaponAmmo;
  lastRequestedWeaponAmmo.infiniteAmmo = infiniteAmmoFromCvars(console);
  WeaponSwitchingMode lastRequestedWeaponSwitchingMode =
    weaponSwitchingModeFromCvars(console);
  bool lastRequestedBotDodgeEnabled = botDodgeEnabled;
  std::int32_t lastRequestedBotDodgeMinIntervalMs = botDodgeMinIntervalMs;
  std::int32_t lastRequestedBotDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
  bool movementTuningRequestPending = false;
  bool relativeMouseModeEnabled = true;
  const ClientGame* audioGame = nullptr;
  std::uint32_t lastAudioServerTick = 0;
  std::uint32_t lastHitSoundServerTick = 0;
  constexpr std::uint32_t kTransientAudioEventTicks = 8;
  std::array<WeaponFireResult, kDuelPlayerCount> lastPlayedWeaponFires = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedWeaponFireAudioTicks = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedWeaponFire = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> lastPlayedRocketExplosions = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedRocketExplosionAudioTicks = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedRocketExplosion = {};
  std::array<FragEvent, kDuelPlayerCount> lastPlayedFragEvents = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedFragAudioTicks = {};
  std::array<bool, kDuelPlayerCount> hasLastPlayedFragEvent = {};
  DamageNumberState damageNumberState;
  std::uint32_t lastDamageNumberServerTick = 0;
  std::array<std::uint32_t, kDuelPlayerCount> lastDamageNumberFeedbackSequences = {};
  std::array<bool, kDuelPlayerCount> hasLastDamageNumberFeedbackSequence = {};
  bool damageNumberStateInitialized = false;
  std::array<std::uint32_t, kDuelPlayerCount> lastPlayedFootstepAudioSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount>
    lastPlayedGrenadeBounceAudioSequences = {};
  std::uint32_t lastLocalRailFireTick = 0;
  bool hasLocalRailFireTick = false;
  bool localRailReadySoundPlayed = true;
  MatchPhase lastAudioMatchPhase = MatchPhase::WaitingForPlayers;
  std::uint32_t lastAudioCountdownSecond = 0;
  std::array<int, kDuelPlayerCount> lastAudioPlayerHealth = {};
  bool previousLocalHit = false;
  bool audioStateInitialized = false;
  bool hasLocalPlayerAliveState = false;
  bool wasLocalPlayerAlive = false;
  bool hasEnemyHitTime = false;
  Clock::time_point lastEnemyHitTime = {};
  std::array<bool, kDuelPlayerCount> hasEnemyHitTimeByTarget = {};
  std::array<Clock::time_point, kDuelPlayerCount> lastEnemyHitTimeByTarget = {};
  bool hasBeamHitTime = false;
  Clock::time_point lastBeamHitTime = {};
  LocalHitFeedbackDedupeState localHitFeedbackDedupe;
  std::array<int, kDuelPlayerCount> lastRemoteHealth = {};
  std::array<bool, kDuelPlayerCount> hasLastRemoteHealth = {};
  std::array<Clock::time_point, kDuelPlayerCount> lastRemoteDamageTime = {};
  std::array<bool, kDuelPlayerCount> hasLastRemoteDamageTime = {};
  std::array<LingeringWeaponFire, kDuelPlayerCount> lingeringRailBeams = {};
  std::array<std::uint8_t, kDuelPlayerCount> revolverCylinderSteps = {};
  std::array<MachineGunBarrelSpinState, kDuelPlayerCount> machineGunBarrelSpin = {};
  MachineGunFiringResponseState machineGunFiringResponse;
  WeaponFireResult lastMachineGunResponseFire = {};
  bool hasLastMachineGunResponseFire = false;
  std::array<RocketLauncherFiringResponseState, kDuelPlayerCount>
    rocketLauncherFiringResponse = {};
  std::array<FreezeGunFiringResponseState, kDuelPlayerCount>
    freezeGunFiringResponse = {};
  std::array<float, kDuelPlayerCount> freezeGunPulseSeconds = {};
  std::array<std::uint32_t, kDuelPlayerCount> freezeGunPulseSerials = {};
  std::array<WeaponFireResult, kDuelPlayerCount> lastRocketLauncherResponseFire = {};
  std::array<bool, kDuelPlayerCount> hasLastRocketLauncherResponseFire = {};
  std::array<PlasmaGunFiringResponseState, kDuelPlayerCount>
    plasmaGunFiringResponse = {};
  std::array<WeaponFireResult, kDuelPlayerCount> lastPlasmaGunResponseFire = {};
  std::array<bool, kDuelPlayerCount> hasLastPlasmaGunResponseFire = {};
  KillFeedState killFeedState;
  TransientTracerStore transientTracerStore;
  CombatEffects combatEffects;
  LocalTracerAimHistory localTracerAimHistory;
  std::vector<TransientTracer> activeTransientTracers;
  std::vector<TransientEffect> activeTransientEffects;
  activeTransientTracers.reserve(kMaxTransientTracers);
  activeTransientEffects.reserve(
    kMaxTransientEffects +
    CombatEffects::kLightCapacity +
    CombatEffects::kCasingCapacity +
    CombatEffects::kParticleCapacity +
    CombatEffects::kDecalCapacity
  );
  std::array<FootstepAudioState, kDuelPlayerCount> footstepAudioStates = {};

  const auto currentMapName = [&session]() -> std::string {
    const ClientGame* game = session.game();
    return game != nullptr && game->hasSnapshot()
      ? game->snapshot().map.mapName
      : std::string{};
  };
  const auto currentMapRevision = [&session]() -> std::uint32_t {
    const ClientGame* game = session.game();
    return game != nullptr && game->hasSnapshot()
      ? game->snapshot().mapRevision
      : 0U;
  };
  const auto currentMapContentHash = [&session]() -> std::uint32_t {
    const ClientGame* game = session.game();
    return game != nullptr && game->hasSnapshot()
      ? game->snapshot().map.contentHash
      : 0U;
  };
  const auto currentControlCamera = [&]() {
    if (developmentCameraEnabled) return developmentCamera;
    dev::CameraTransform camera;
    const ClientGame* game = session.game();
    if (game != nullptr && game->hasSnapshot() && !session.spectator()) {
      const PlayerState& player = game->predictedPlayer();
      camera.position = player.position + Vec3{0.0F, 0.0F, 0.65F};
      camera.yawDegrees = player.viewYawRadians * kRadiansToDegrees;
      camera.pitchDegrees = player.viewPitchRadians * kRadiansToDegrees;
    }
    camera.fieldOfView = console.getFloat("cl_fov");
    return camera;
  };
  const auto captureRelativePath = [&repositoryRoot](const std::filesystem::path& path) {
    const std::filesystem::path relative = path.lexically_relative(repositoryRoot);
    return relative.empty() ? path.generic_string() : relative.generic_string();
  };
  const auto timestampMilliseconds = []() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()
    ).count();
  };
  const auto controlStatus = [&]() {
    dev::JsonValue status = dev::JsonValue::objectValue();
    const ClientGame* game = session.game();
    const bool hasSnapshot = game != nullptr && game->hasSnapshot();
    status.object["control_protocol"] = dev::JsonValue::numberValue(1);
    status.object["client_running"] = dev::JsonValue::booleanValue(true);
    status.object["server_running"] = dev::JsonValue::booleanValue(session.connected());
    status.object["connected"] = dev::JsonValue::booleanValue(session.connected());
    status.object["connection_state"] = dev::JsonValue::numberValue(
      static_cast<int>(session.state())
    );
    status.object["connection_message"] = dev::JsonValue::stringValue(session.statusMessage());
    status.object["map"] = dev::JsonValue::stringValue(currentMapName());
    status.object["map_revision"] = dev::JsonValue::numberValue(currentMapRevision());
    status.object["game_mode"] = dev::JsonValue::stringValue(
      hasSnapshot ? gameModeName(game->snapshot().gameMode) : "UNKNOWN"
    );
    status.object["match_state"] = dev::JsonValue::stringValue(
      hasSnapshot ? matchPhaseName(game->snapshot().matchPhase) : "UNKNOWN"
    );
    status.object["spectator"] = dev::JsonValue::booleanValue(session.spectator());
    status.object["development_camera"] = dev::JsonValue::booleanValue(developmentCameraEnabled);
    const bool collisionDebugSupported = renderer.backendName() == "SDL_GPU/vulkan";
    const int requestedCollisionDebugMode = console.getInt("r_show_collision");
    status.object["collision_debug_supported"] =
      dev::JsonValue::booleanValue(collisionDebugSupported);
    status.object["collision_debug_requested_mode"] =
      dev::JsonValue::numberValue(requestedCollisionDebugMode);
    status.object["collision_debug_mode"] = dev::JsonValue::numberValue(
      collisionDebugSupported ? requestedCollisionDebugMode : 0
    );
    status.object["benchmark_enabled"] = dev::JsonValue::booleanValue(benchmark_.enabled);
    status.object["game_protocol_version"] = dev::JsonValue::numberValue(kProtocolVersion);
    status.object["camera"] = dev::cameraJson(currentControlCamera());
    status.object["renderer"] = dev::JsonValue::stringValue(std::string(renderer.backendName()));
    status.object["requested_renderer"] =
      dev::JsonValue::stringValue(std::string(renderer.requestedBackendName()));
    status.object["actual_renderer"] =
      dev::JsonValue::stringValue(std::string(renderer.backendName()));
    status.object["gpu_name"] =
      dev::JsonValue::stringValue(std::string(renderer.gpuName()));
    status.object["graphics_driver_name"] =
      dev::JsonValue::stringValue(std::string(renderer.graphicsDriverName()));
    status.object["graphics_driver_version"] =
      dev::JsonValue::stringValue(std::string(renderer.graphicsDriverVersion()));
    status.object["graphics_driver_info"] =
      dev::JsonValue::stringValue(std::string(renderer.graphicsDriverInfo()));
    status.object["software_renderer"] =
      dev::JsonValue::booleanValue(renderer.softwareRenderer());
    status.object["vulkan_api_version"] =
      dev::JsonValue::stringValue(std::string(renderer.vulkanApiVersion()));
    status.object["vulkan_icd_path"] =
      dev::JsonValue::stringValue(std::string(renderer.vulkanIcdPath()));
    status.object["vulkan_icd_sha256"] =
      dev::JsonValue::stringValue(std::string(renderer.vulkanIcdSha256()));
    // The client exposes observed device identity and the launch selection.
    // Only the external launcher can compare both and mark the session
    // verified.
    status.object["gpu_verification_state"] = dev::JsonValue::stringValue(
      renderer.backendName() == "SDL_GPU/vulkan"
        ? "pending-launcher-verification"
        : "not-verified"
    );
    status.object["gpu_verified"] = dev::JsonValue::booleanValue(false);
    status.object["capture_output_directory"] =
      dev::JsonValue::stringValue(captureDirectory.string());
    status.object["capture_output_relative"] =
      dev::JsonValue::stringValue(captureRelativePath(captureDirectory));
    status.object["last_control_error"] = dev::JsonValue::stringValue(lastControlError);
    if (hasSnapshot && !session.spectator()) {
      const PlayerState& player = game->predictedPlayer();
      dev::JsonValue position = dev::JsonValue::arrayValue({
        dev::JsonValue::numberValue(player.position.x),
        dev::JsonValue::numberValue(player.position.y),
        dev::JsonValue::numberValue(player.position.z),
      });
      status.object["player_position"] = std::move(position);
      status.object["player_yaw"] =
        dev::JsonValue::numberValue(player.viewYawRadians * kRadiansToDegrees);
      status.object["player_pitch"] =
        dev::JsonValue::numberValue(player.viewPitchRadians * kRadiansToDegrees);
      status.object["player_health"] = dev::JsonValue::numberValue(player.health);
      status.object["player_weapon"] =
        dev::JsonValue::stringValue(std::string(weaponShortName(selectedWeapon)));
    }
    return status;
  };
  const auto controlClientState = [&]() {
    dev::JsonValue state = dev::JsonValue::objectValue();
    const ClientGame* game = session.game();
    const bool hasSnapshot = game != nullptr && game->hasSnapshot();
    const bool hasLocalPlayer =
      hasSnapshot && !session.spectator() && session.playerIndex() < kDuelPlayerCount;

    state.object["connected"] =
      dev::JsonValue::booleanValue(session.connected());
    state.object["client_tick"] = dev::JsonValue::numberValue(clientTick);
    state.object["latest_snapshot_tick"] = hasSnapshot
      ? dev::JsonValue::numberValue(game->snapshot().serverTick)
      : dev::JsonValue{};
    state.object["latest_server_tick"] = hasSnapshot
      ? dev::JsonValue::numberValue(game->snapshot().serverTick)
      : dev::JsonValue{};

    const SnapshotInterpolation::Diagnostics interpolation = game != nullptr
      ? game->interpolationDiagnostics()
      : SnapshotInterpolation::Diagnostics{};
    state.object["presentation_tick"] = hasSnapshot
      ? dev::JsonValue::numberValue(interpolation.presentationTick)
      : dev::JsonValue{};
    state.object["predicted_local_player"] = hasLocalPlayer
      ? controlPlayerStateJson(game->predictedPlayer())
      : dev::JsonValue{};
    state.object["authoritative_local_player"] = hasLocalPlayer
      ? controlPlayerStateJson(game->snapshot().players[session.playerIndex()])
      : dev::JsonValue{};

    const bool hasAcknowledgedCommand =
      hasLocalPlayer && game->hasAcknowledgedCommand();
    state.object["last_acknowledged_command"] = hasAcknowledgedCommand
      ? dev::JsonValue::numberValue(game->lastAcknowledgedCommand())
      : dev::JsonValue{};
    const PredictionDiagnostics prediction = hasLocalPlayer
      ? game->predictionDiagnostics()
      : PredictionDiagnostics{};
    state.object["pending_command_count"] =
      dev::JsonValue::numberValue(prediction.pendingCommandCount);
    state.object["maximum_pending_command_count"] =
      dev::JsonValue::numberValue(prediction.maximumPendingCommandCount);
    state.object["oldest_pending_command_age_ticks"] =
      dev::JsonValue::numberValue(
        prediction.hasPendingCommand
          ? clientTick - prediction.oldestPendingCommandClientTick
          : 0U
      );
    state.object["correction_count"] =
      dev::JsonValue::numberValue(prediction.correctionCount);
    state.object["last_correction_vector"] = dev::JsonValue::arrayValue({
      dev::JsonValue::numberValue(prediction.lastCorrectionVector.x),
      dev::JsonValue::numberValue(prediction.lastCorrectionVector.y),
      dev::JsonValue::numberValue(prediction.lastCorrectionVector.z),
    });
    state.object["last_correction_distance"] =
      dev::JsonValue::numberValue(prediction.lastCorrectionDistance);
    state.object["maximum_correction_distance"] =
      dev::JsonValue::numberValue(prediction.maximumCorrectionDistance);

    dev::JsonValue interpolationState = dev::JsonValue::objectValue();
    interpolationState.object["buffer_lead_ticks"] =
      dev::JsonValue::numberValue(interpolation.bufferLeadTicks);
    interpolationState.object["desired_buffer_lead_ticks"] =
      dev::JsonValue::numberValue(interpolation.desiredBufferLeadTicks);
    interpolationState.object["timeline_error_ticks"] =
      dev::JsonValue::numberValue(interpolation.timelineErrorTicks);
    interpolationState.object["presentation_tick"] =
      dev::JsonValue::numberValue(interpolation.presentationTick);
    interpolationState.object["newest_snapshot_tick"] =
      dev::JsonValue::numberValue(interpolation.newestSnapshotTick);
    interpolationState.object["playback_rate"] =
      dev::JsonValue::numberValue(interpolation.playbackRate);
    interpolationState.object["effective_delay_ms"] =
      dev::JsonValue::numberValue(interpolation.effectiveDelaySeconds * 1000.0F);
    interpolationState.object["buffered_snapshot_count"] =
      dev::JsonValue::numberValue(interpolation.bufferedSnapshotCount);
    interpolationState.object["underrun_count"] =
      dev::JsonValue::numberValue(interpolation.underrunCount);
    interpolationState.object["hard_correction_count"] =
      dev::JsonValue::numberValue(interpolation.hardCorrectionCount);
    interpolationState.object["playback_started"] =
      dev::JsonValue::booleanValue(interpolation.playbackStarted);
    interpolationState.object["underrun"] =
      dev::JsonValue::booleanValue(interpolation.bufferUnderrun);
    state.object["interpolation"] = std::move(interpolationState);

    const SnapshotDiagnostics snapshots =
      game != nullptr ? game->snapshotDiagnostics() : SnapshotDiagnostics{};
    dev::JsonValue snapshotState = dev::JsonValue::objectValue();
    snapshotState.object["packets_decoded"] =
      dev::JsonValue::numberValue(snapshots.snapshotPacketsDecoded);
    snapshotState.object["snapshots_accepted"] =
      dev::JsonValue::numberValue(snapshots.snapshotsApplied);
    snapshotState.object["queue_depth"] =
      dev::JsonValue::numberValue(snapshots.snapshotQueueDepth);
    snapshotState.object["duplicate_snapshots_ignored"] =
      dev::JsonValue::numberValue(snapshots.duplicateSnapshotsIgnored);
    snapshotState.object["stale_snapshots_ignored"] =
      dev::JsonValue::numberValue(snapshots.staleSnapshotsIgnored);
    snapshotState.object["decode_ms"] =
      dev::JsonValue::numberValue(snapshots.snapshotDecodeMilliseconds);
    snapshotState.object["apply_ms"] =
      dev::JsonValue::numberValue(snapshots.snapshotApplyMilliseconds);
    state.object["snapshots"] = std::move(snapshotState);

    const NetworkTelemetry telemetry = session.networkTelemetry();
    dev::JsonValue networkState = dev::JsonValue::objectValue();
    networkState.object["valid"] = dev::JsonValue::booleanValue(telemetry.valid);
    networkState.object["ping_ms"] =
      dev::JsonValue::numberValue(telemetry.pingMilliseconds);
    networkState.object["ping_variation_ms"] =
      dev::JsonValue::numberValue(telemetry.pingVariationMilliseconds);
    networkState.object["snapshot_jitter_ms"] =
      dev::JsonValue::numberValue(telemetry.snapshotJitterMilliseconds);
    networkState.object["incoming_loss_percent"] =
      dev::JsonValue::numberValue(telemetry.incomingLossPercent);
    networkState.object["outgoing_loss_percent"] =
      dev::JsonValue::numberValue(telemetry.outgoingLossPercent);
    networkState.object["incoming_kbps"] =
      dev::JsonValue::numberValue(telemetry.incomingKilobitsPerSecond);
    networkState.object["outgoing_kbps"] =
      dev::JsonValue::numberValue(telemetry.outgoingKilobitsPerSecond);
    networkState.object["snapshot_rate"] =
      dev::JsonValue::numberValue(telemetry.snapshotRate);
    networkState.object["snapshot_age_ms"] =
      dev::JsonValue::numberValue(telemetry.snapshotAgeMilliseconds);
    networkState.object["last_snapshot_bytes"] =
      dev::JsonValue::numberValue(telemetry.lastSnapshotBytes);
    networkState.object["last_command_bytes"] =
      dev::JsonValue::numberValue(telemetry.lastCommandBytes);
    networkState.object["late_snapshots"] =
      dev::JsonValue::numberValue(telemetry.lateSnapshots);
    networkState.object["reordered_snapshots"] =
      dev::JsonValue::numberValue(telemetry.reorderedSnapshots);
    networkState.object["acknowledged_command_datagram_sequence"] =
      dev::JsonValue::numberValue(telemetry.acknowledgedCommandDatagramSequence);
    state.object["network_telemetry"] = std::move(networkState);

    dev::JsonValue networkSimulation = controlNetworkSimulationConfigJson(
      session.networkSimulationConfig()
    );
    const ClientNetworkSimulationStats simulationStats =
      session.networkSimulationStats();
    dev::JsonValue stats = dev::JsonValue::objectValue();
    stats.object["queued_outgoing_packets"] =
      dev::JsonValue::numberValue(simulationStats.queuedOutgoingPackets);
    stats.object["queued_incoming_packets"] =
      dev::JsonValue::numberValue(simulationStats.queuedIncomingPackets);
    stats.object["dropped_outgoing_packets"] =
      dev::JsonValue::numberValue(simulationStats.droppedOutgoingPackets);
    stats.object["dropped_incoming_packets"] =
      dev::JsonValue::numberValue(simulationStats.droppedIncomingPackets);
    stats.object["reordered_outgoing_packets"] =
      dev::JsonValue::numberValue(simulationStats.reorderedOutgoingPackets);
    stats.object["reordered_incoming_packets"] =
      dev::JsonValue::numberValue(simulationStats.reorderedIncomingPackets);
    networkSimulation.object["stats"] = std::move(stats);
    std::vector<dev::JsonValue> decisions;
    decisions.reserve(session.networkSimulationDecisions().size());
    for (const ClientNetworkSimulationDecision& decision :
         session.networkSimulationDecisions()) {
      dev::JsonValue value = dev::JsonValue::objectValue();
      value.object["sequence"] = dev::JsonValue::numberValue(decision.sequence);
      value.object["direction"] = dev::JsonValue::stringValue(
        std::string(controlNetworkSimulationDirectionName(decision.direction))
      );
      value.object["action"] = dev::JsonValue::stringValue(
        std::string(controlNetworkSimulationActionName(decision.action))
      );
      value.object["delay_ms"] = dev::JsonValue::numberValue(decision.delayMs);
      value.object["reordered"] = dev::JsonValue::booleanValue(decision.reordered);
      value.object["queue_limit_drop"] =
        dev::JsonValue::booleanValue(decision.queueLimitDrop);
      decisions.push_back(std::move(value));
    }
    networkSimulation.object["decisions"] =
      dev::JsonValue::arrayValue(std::move(decisions));
    state.object["network_simulation"] = std::move(networkSimulation);

    const CombatEffectsStats effectStats = combatEffects.stats();
    dev::JsonValue effects = dev::JsonValue::objectValue();
    effects.object["active_lights"] =
      dev::JsonValue::numberValue(effectStats.activeLights);
    effects.object["active_casings"] =
      dev::JsonValue::numberValue(effectStats.activeCasings);
    effects.object["active_particles"] =
      dev::JsonValue::numberValue(effectStats.activeParticles);
    effects.object["active_decals"] =
      dev::JsonValue::numberValue(effectStats.activeDecals);
    effects.object["peak_lights"] =
      dev::JsonValue::numberValue(effectStats.peakLights);
    effects.object["peak_casings"] =
      dev::JsonValue::numberValue(effectStats.peakCasings);
    effects.object["peak_particles"] =
      dev::JsonValue::numberValue(effectStats.peakParticles);
    effects.object["peak_decals"] =
      dev::JsonValue::numberValue(effectStats.peakDecals);
    effects.object["shots_spawned"] =
      dev::JsonValue::numberValue(static_cast<double>(effectStats.shotsSpawned));
    effects.object["effects_dropped"] =
      dev::JsonValue::numberValue(static_cast<double>(effectStats.effectsDropped));
    state.object["combat_effects"] = std::move(effects);

    const RendererFrameDiagnostics& render = renderer.lastFrameDiagnostics();
    dev::JsonValue renderState = dev::JsonValue::objectValue();
    renderState.object["cpu_total_ms"] =
      dev::JsonValue::numberValue(render.totalRenderMilliseconds);
    renderState.object["active_lights_submitted"] =
      dev::JsonValue::numberValue(render.activeTemporaryLights);
    renderState.object["active_casings_submitted"] =
      dev::JsonValue::numberValue(render.activeCasings);
    renderState.object["active_particles_submitted"] =
      dev::JsonValue::numberValue(render.activeImpactParticles);
    renderState.object["active_decals_submitted"] =
      dev::JsonValue::numberValue(render.activeBulletDecals);
    renderState.object["transparent_effects_submitted"] =
      dev::JsonValue::numberValue(render.transparentEffectsSubmitted);
    state.object["render_frame"] = std::move(renderState);
    return state;
  };
  const auto setBenchmarkCamera = [&](const benchmark::CameraPose& pose) {
    developmentCamera.position = pose.position;
    developmentCamera.yawDegrees = pose.yawDegrees;
    developmentCamera.pitchDegrees = pose.pitchDegrees;
    developmentCamera.fieldOfView = pose.fieldOfView;
    developmentCameraEnabled = true;
  };
  const auto restoreControlCvars = [&]() {
    if (!activeControlOperation.has_value()) return;
    ActiveControlOperation& active = *activeControlOperation;
    for (const auto& [name, value] : active.restoredCvars) {
      (void)console.execute("set " + name + " " + value);
    }
    active.restoredCvars.clear();
  };
  const auto restoreBenchmarkState = [&]() {
    if (!activeControlOperation.has_value() ||
        activeControlOperation->queued.request.operation != dev::ControlOperation::RunBenchmark) return;
    ActiveControlOperation& active = *activeControlOperation;
    restoreControlCvars();
    selectedWeapon = active.previousSelectedWeapon;
    input.attack = active.previousAttack;
    if (active.benchmarkBotsConfigured) {
      pendingBotCommands.push_back(PendingBotCommand{BotCommandType::KickAll, 0});
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Weapon,
        active.previousBotWeaponAuto ? -1 : static_cast<std::int32_t>(active.previousBotWeapon),
      });
      botWeaponAuto = active.previousBotWeaponAuto;
      if (active.previousBotCount > 0) {
        pendingBotCommands.push_back(PendingBotCommand{
          BotCommandType::Add, active.previousBotCount
        });
      }
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::AttackMode,
        static_cast<std::int32_t>(active.previousBotAttackMode)
      });
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Stare, active.previousBotStare ? 1 : 0
      });
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Standstill, active.previousBotStandstill ? 1 : 0
      });
      pendingBotCommands.push_back(PendingBotCommand{
        BotCommandType::Dodge,
        active.previousBotDodge ? 1 : 0,
        active.previousBotDodgeMinIntervalMs,
        active.previousBotDodgeMaxIntervalMs,
      });
    }
    developmentCameraEnabled = active.previousDevelopmentCameraEnabled;
    developmentCamera = active.previousDevelopmentCamera;
  };
  const auto completeControlError = [&](std::string code, std::string message) {
    if (!activeControlOperation.has_value()) return;
    lastControlError = message;
    developerControl.complete(
      activeControlOperation->queued.token,
      dev::errorResponse(
        activeControlOperation->queued.request.id,
        std::move(code),
        std::move(message)
      )
    );
    restoreBenchmarkState();
    restoreControlCvars();
    activeControlOperation.reset();
  };
  const auto activateFirstViewpoint = [&]() {
    ActiveControlOperation& active = *activeControlOperation;
    const dev::CameraViewpoint& view = active.queued.request.viewpoints[0];
    developmentCamera = view.camera;
    developmentCameraEnabled = true;
    active.viewpointIndex = 0;
    active.requiredRenderedFrame = renderedFrameSerial + 1U;
    active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    active.stage = ActiveControlOperation::Stage::WaitingForCameraFrame;
  };

  while (running) {
    const auto outerFrameStart = Clock::now();
    const auto outerFrameElapsed =
      std::chrono::duration<float>(outerFrameStart - previousOuterFrameStart);
    previousOuterFrameStart = outerFrameStart;
    const float outerFrameMilliseconds = outerFrameElapsed.count() * 1000.0F;
    if (developerControl.running() && !activeControlOperation.has_value()) {
      if (std::optional<dev::QueuedControlRequest> queued = developerControl.pollRequest();
          queued.has_value()) {
        activeControlOperation.emplace();
        activeControlOperation->queued = std::move(*queued);
        activeControlOperation->deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(20);
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage == ActiveControlOperation::Stage::Start) {
      ActiveControlOperation& active = *activeControlOperation;
      const dev::ControlRequest& request = active.queued.request;
      switch (request.operation) {
      case dev::ControlOperation::Status:
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, controlStatus())
        );
        activeControlOperation.reset();
        break;
      case dev::ControlOperation::GetClientState:
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, controlClientState())
        );
        activeControlOperation.reset();
        break;
      case dev::ControlOperation::SetNetworkSimulation: {
        // A typed control setting remains in force across frames and
        // reconnects. It does not pass through console text or change archived
        // user settings.
        developerNetworkSimulation = request.networkSimulation;
        session.setNetworkSimulationConfig(*developerNetworkSimulation);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(
            request.id,
            controlNetworkSimulationConfigJson(session.networkSimulationConfig())
          )
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::GetCamera:
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, dev::cameraJson(currentControlCamera()))
        );
        activeControlOperation.reset();
        break;
      case dev::ControlOperation::SetCamera: {
        developmentCamera = request.camera;
        if (!developmentCamera.fieldOfView.has_value()) {
          developmentCamera.fieldOfView = console.getFloat("cl_fov");
        }
        developmentCameraEnabled = true;
        dev::JsonValue result = dev::cameraJson(developmentCamera);
        result.object["mode"] = dev::JsonValue::stringValue("development_camera");
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::SetCollisionDebug: {
        if (renderer.backendName() != "SDL_GPU/vulkan") {
          completeControlError(
            "renderer_unsupported",
            "collision debug visualization requires verified SDL_GPU/vulkan"
          );
          break;
        }
        // Keep automation on the same bounded CVAR path as the in-game console.
        // This operation only changes presentation; authoritative traces are
        // untouched.
        const std::string consoleResult = console.execute(
          "set r_show_collision " + std::to_string(request.collisionDebugMode)
        );
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["mode"] =
          dev::JsonValue::numberValue(console.getInt("r_show_collision"));
        result.object["console_result"] = dev::JsonValue::stringValue(consoleResult);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::ExecConsole: {
        const std::string consoleResult = console.execute(request.consoleCommand);
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["command"] = dev::JsonValue::stringValue(request.consoleCommand);
        result.object["output"] = dev::JsonValue::stringValue(consoleResult);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::GetCvar: {
        if (!console.hasCvar(request.cvarName)) {
          completeControlError("unknown_cvar", "unknown cvar: " + request.cvarName);
          break;
        }
        const std::string consoleResult = console.execute(request.cvarName);
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["name"] = dev::JsonValue::stringValue(request.cvarName);
        result.object["value"] = dev::JsonValue::stringValue(console.valueString(request.cvarName));
        result.object["output"] = dev::JsonValue::stringValue(consoleResult);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::SetCvar: {
        const std::string consoleResult = console.execute(
          "set " + request.cvarName + " \"" + request.cvarValue + '"'
        );
        if (consoleResult.starts_with("unknown cvar:")) {
          completeControlError("unknown_cvar", consoleResult);
          break;
        }
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["name"] = dev::JsonValue::stringValue(request.cvarName);
        result.object["value"] = dev::JsonValue::stringValue(console.valueString(request.cvarName));
        result.object["output"] = dev::JsonValue::stringValue(consoleResult);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::SetPlayerView: {
        if (!session.connected() || session.game() == nullptr || !session.game()->hasSnapshot() ||
            session.spectator()) {
          completeControlError("not_playing", "player view requires an active player snapshot");
          break;
        }
        developmentCameraEnabled = false;
        presentationView.yawRadians = request.playerYawDegrees * kDegreesToRadians;
        presentationView.pitchRadians = request.playerPitchDegrees * kDegreesToRadians;
        presentationView.initialized = true;
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["yaw"] = dev::JsonValue::numberValue(request.playerYawDegrees);
        result.object["pitch"] = dev::JsonValue::numberValue(request.playerPitchDegrees);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::SetPlayerWeapon: {
        const std::optional<Weapon> weapon = parseWeaponToken(request.playerWeapon);
        if (!weapon.has_value()) {
          completeControlError("invalid_weapon", "unknown weapon: " + request.playerWeapon);
          break;
        }
        selectedWeapon = *weapon;
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["weapon"] =
          dev::JsonValue::stringValue(std::string(weaponShortName(selectedWeapon)));
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::WaitFrames:
        active.requiredRenderedFrame = renderedFrameSerial + request.waitFrames;
        active.stage = ActiveControlOperation::Stage::WaitingForFrames;
        active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        break;
      case dev::ControlOperation::WaitClientTick:
        active.stage = ActiveControlOperation::Stage::WaitingForClientTick;
        active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        break;
      case dev::ControlOperation::WaitSnapshotTick:
        active.stage = ActiveControlOperation::Stage::WaitingForSnapshotTick;
        active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        break;
      case dev::ControlOperation::WaitCommandAck:
        active.stage = ActiveControlOperation::Stage::WaitingForCommandAck;
        active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        break;
      case dev::ControlOperation::SendInput:
        if (!session.connected() || session.game() == nullptr || !session.game()->hasSnapshot() ||
            session.spectator()) {
          completeControlError("not_playing", "player input requires an active player snapshot");
          break;
        }
        developmentCameraEnabled = false;
        active.inputTicksRemaining = request.playerInput.ticks;
        active.stage = ActiveControlOperation::Stage::PlayerInput;
        active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        break;
      case dev::ControlOperation::LoadMap:
      case dev::ControlOperation::ReloadMap:
      case dev::ControlOperation::CaptureMapViews: {
        const bool captureViews = request.operation == dev::ControlOperation::CaptureMapViews;
        const bool needsMap = request.operation != dev::ControlOperation::CaptureMapViews ||
          !request.mapName.empty();
        if (captureViews) {
          // Visual review must be reproducible even when a developer has an
          // extreme archived mip bias. Restore their preferences afterward.
          static constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
            kCaptureTextureCvars{{
              {"r_texture_filter", "2"},
              {"r_texture_anisotropy", "8"},
              {"r_texture_lod_bias", "0.5"},
            }};
          for (const auto& [name, value] : kCaptureTextureCvars) {
            active.restoredCvars.emplace(std::string(name), console.valueString(name));
            (void)console.execute("set " + std::string(name) + " " + std::string(value));
          }
          const std::string mapPart = request.mapName.empty() ? currentMapName() : request.mapName;
          active.captureStem = dev::sanitizeGeneratedCaptureName(
            mapPart + "-" + (request.presetName.empty() ? "views" : request.presetName) +
            "-" + std::to_string(timestampMilliseconds())
          );
        }
        if (!needsMap) {
          if (currentMapName().empty()) {
            completeControlError("not_ready", "no active map is available for capture");
          } else {
            activateFirstViewpoint();
          }
          break;
        }
        if (!session.connected() || session.game() == nullptr || !session.game()->hasSnapshot()) {
          completeControlError("not_connected", "map changes require a connected client with an active snapshot");
          break;
        }
        active.targetMap = request.operation == dev::ControlOperation::ReloadMap
          ? currentMapName()
          : request.mapName;
        if (!isValidMapName(active.targetMap)) {
          completeControlError("invalid_map", "the current or requested map name is not safe");
          break;
        }
        const LocalMapLoadResult localMap = loadLocalMap(
          active.targetMap,
          (runtimeDirectory / "maps").string()
        );
        if (!localMap.ok) {
          completeControlError("map_load_failed", localMap.error);
          break;
        }
        active.targetMap = localMap.descriptor.mapName;
        active.previousMapRevision = currentMapRevision();
        pendingMapName = active.targetMap;
        active.stage = ActiveControlOperation::Stage::WaitingForMap;
        break;
      }
      case dev::ControlOperation::CaptureScreenshot: {
        if (currentMapName().empty()) {
          completeControlError("not_ready", "no active map is available for capture");
          break;
        }
        const std::string requestedName = request.captureName.empty()
          ? currentMapName() + "-" + std::to_string(timestampMilliseconds())
          : request.captureName;
        active.captureStem = dev::sanitizeGeneratedCaptureName(requestedName);
        active.requiredRenderedFrame = renderedFrameSerial + 1U;
        active.stage = ActiveControlOperation::Stage::WaitingForCameraFrame;
        break;
      }
      case dev::ControlOperation::ArmPhaseCapture: {
        if (currentMapName().empty() || !session.connected() ||
            session.game() == nullptr || !session.game()->hasSnapshot()) {
          completeControlError(
            "not_ready",
            "phase capture requires an active connected map"
          );
          break;
        }
        if (armedPhaseCapture.has_value()) {
          completeControlError(
            "capture_already_armed",
            "collect the existing phase capture before arming another"
          );
          break;
        }
        armedPhaseCapture = ArmedPhaseCapture{
          request.captureName,
          request.capturePhase,
          request.hideHud,
          request.hideOverlays,
          captureDirectory / (request.captureName + ".png"),
          std::nullopt,
          {},
          Clock::now() + std::chrono::seconds(30),
        };
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["name"] =
          dev::JsonValue::stringValue(request.captureName);
        result.object["phase"] =
          dev::JsonValue::stringValue(request.capturePhase);
        result.object["armed"] = dev::JsonValue::booleanValue(true);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(result))
        );
        activeControlOperation.reset();
        break;
      }
      case dev::ControlOperation::CollectPhaseCapture: {
        if (!armedPhaseCapture.has_value() ||
            armedPhaseCapture->name != request.captureName) {
          completeControlError(
            "capture_not_armed",
            "no phase capture is armed with that name"
          );
          break;
        }
        active.stage = ActiveControlOperation::Stage::WaitingForPhaseCapture;
        active.deadline = armedPhaseCapture->deadline;
        break;
      }
      case dev::ControlOperation::RunBenchmark: {
        if (!benchmark_.enabled) {
          completeControlError("benchmark_disabled", "run_benchmark requires the explicit --benchmark client option");
          break;
        }
        if (!session.connected() || session.game() == nullptr || !session.game()->hasSnapshot()) {
          completeControlError("not_connected", "benchmark requires a connected client with an active snapshot");
          break;
        }
        const benchmark::Scenario& scenario = request.benchmarkScenario;
        const LocalMapLoadResult localMap = loadLocalMap(
          scenario.map,
          (runtimeDirectory / "maps").string()
        );
        if (!localMap.ok) {
          completeControlError("map_load_failed", localMap.error);
          break;
        }
        active.previousDevelopmentCameraEnabled = developmentCameraEnabled;
        active.previousDevelopmentCamera = developmentCamera;
        active.previousSelectedWeapon = selectedWeapon;
        active.previousAttack = input.attack;
        active.previousBotAttackMode = botAttackMode;
        active.previousBotWeapon = botWeapon;
        active.previousBotWeaponAuto = botWeaponAuto;
        active.previousBotStare = botStareEnabled;
        active.previousBotStandstill = botStandstillEnabled;
        active.previousBotDodge = botDodgeEnabled;
        active.previousBotDodgeMinIntervalMs = botDodgeMinIntervalMs;
        active.previousBotDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
        const std::map<std::string, std::string, std::less<>> overrides =
          benchmark::benchmarkCvarOverrides(scenario);
        for (const auto& [name, value] : overrides) {
          active.restoredCvars.emplace(name, console.valueString(name));
          (void)console.execute("set " + name + " " + value);
        }
        active.targetMap = localMap.descriptor.mapName;
        active.previousMapRevision = currentMapRevision();
        pendingMapName = active.targetMap;
        active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        active.stage = ActiveControlOperation::Stage::WaitingForMap;
        break;
      }
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage == ActiveControlOperation::Stage::WaitingForMap) {
      ActiveControlOperation& active = *activeControlOperation;
      if (currentMapName() == active.targetMap &&
          currentMapRevision() > active.previousMapRevision) {
        if (active.queued.request.operation == dev::ControlOperation::RunBenchmark) {
          const benchmark::Scenario& scenario = active.queued.request.benchmarkScenario;
          setBenchmarkCamera(scenario.cameraStart);
          // The benchmark uses normal client commands so the server stays
          // authoritative for weapon timing, hits, and damage.
          selectedWeapon = scenario.playerWeapon;
          input.attack = scenario.playerAttack ? 1 : 0;
          active.benchmarkPhaseStart = Clock::now();
          active.benchmarkPhaseFrames = 0;
          active.benchmarkSamples.clear();
          const std::uint64_t maximumMeasuredFrames = scenario.measuredFrames
            ? *scenario.measuredFrames
            : static_cast<std::uint64_t>(
                std::ceil(scenario.measuredSeconds.value_or(10.0) *
                  static_cast<double>(
                    scenario.frameCap > 0
                      ? static_cast<std::uint64_t>(scenario.frameCap) + 1U
                      : kMaximumReservedBenchmarkFramesPerSecond
                  ))
              ) + 1U;
          active.benchmarkSamples.reserve(
            static_cast<std::size_t>(maximumMeasuredFrames)
          );
          active.benchmarkTickSamples.clear();
          const std::uint64_t maximumMeasuredTicks = scenario.measuredFrames
            ? *scenario.measuredFrames *
              static_cast<std::uint64_t>(kMaxSimulationTicksPerFrame)
            : static_cast<std::uint64_t>(
                std::ceil(scenario.measuredSeconds.value_or(10.0) *
                  static_cast<double>(kFixedTickRate))
              ) + static_cast<std::uint64_t>(kMaxSimulationTicksPerFrame);
          // Reserve the complete fixed-tick stream before measurement so a
          // catch-up frame never reallocates inside the benchmark interval.
          active.benchmarkTickSamples.reserve(
            static_cast<std::size_t>(maximumMeasuredTicks)
          );
          active.lastBenchmarkFrameTiming = {};
          active.previousBotCount = static_cast<int>(std::count(
            session.game()->snapshot().botPlayers.begin(),
            session.game()->snapshot().botPlayers.end(),
            true
          ));
          // Start every benchmark from an exact bot roster; map reloads retain
          // bots during ordinary development play.
          pendingBotCommands.push_back(PendingBotCommand{BotCommandType::KickAll, 0});
          pendingBotCommands.push_back(PendingBotCommand{
            BotCommandType::Weapon,
            static_cast<std::int32_t>(scenario.actors.weapon),
          });
          botWeaponAuto = false;
          if (scenario.actors.bots > 0) {
            pendingBotCommands.push_back(PendingBotCommand{BotCommandType::Add, scenario.actors.bots});
          }
          if (const std::optional<BotAttackMode> mode = parseBotAttackMode(scenario.actors.attackMode)) {
            pendingBotCommands.push_back(PendingBotCommand{BotCommandType::AttackMode, static_cast<std::int32_t>(*mode)});
          }
          pendingBotCommands.push_back(PendingBotCommand{BotCommandType::Stare, scenario.actors.stare ? 1 : 0});
          pendingBotCommands.push_back(PendingBotCommand{BotCommandType::Standstill, scenario.actors.standstill ? 1 : 0});
          pendingBotCommands.push_back(PendingBotCommand{
            BotCommandType::Dodge,
            scenario.actors.dodge ? 1 : 0,
            scenario.actors.dodgeMinMilliseconds,
            scenario.actors.dodgeMaxMilliseconds,
          });
          active.benchmarkBotsConfigured = true;
          active.stage = ActiveControlOperation::Stage::BenchmarkWarmup;
        } else if (active.queued.request.operation == dev::ControlOperation::CaptureMapViews) {
          activateFirstViewpoint();
        } else {
          dev::JsonValue result = dev::JsonValue::objectValue();
          result.object["map"] = dev::JsonValue::stringValue(currentMapName());
          result.object["map_revision"] = dev::JsonValue::numberValue(currentMapRevision());
          result.object["previous_map_revision"] =
            dev::JsonValue::numberValue(active.previousMapRevision);
          developerControl.complete(
            active.queued.token,
            dev::successResponse(active.queued.request.id, std::move(result))
          );
          activeControlOperation.reset();
        }
      } else if (std::chrono::steady_clock::now() > active.deadline) {
        completeControlError(
          "map_timeout",
          "server did not activate map '" + active.targetMap +
            "' with a new revision within 20 seconds"
        );
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage == ActiveControlOperation::Stage::WaitingForCameraFrame) {
      if (renderedFrameSerial >= activeControlOperation->requiredRenderedFrame) {
        activeControlOperation->stage = ActiveControlOperation::Stage::CaptureReady;
      } else if (std::chrono::steady_clock::now() > activeControlOperation->deadline) {
        completeControlError(
          "capture_timeout",
          "renderer did not complete the frame required for capture within 20 seconds"
        );
      }
    }
    if (activeControlOperation.has_value()) {
      ActiveControlOperation& active = *activeControlOperation;
      const dev::ControlRequest& request = active.queued.request;
      if (request.operation == dev::ControlOperation::RunBenchmark) {
        const benchmark::Scenario& scenario = request.benchmarkScenario;
        const auto phaseNow = Clock::now();
        if (active.stage == ActiveControlOperation::Stage::BenchmarkWarmup) {
          ++active.benchmarkPhaseFrames;
          const double seconds = std::chrono::duration<double>(
            phaseNow - active.benchmarkPhaseStart
          ).count();
          const bool secondsDone = scenario.warmupSeconds && seconds >= *scenario.warmupSeconds;
          const bool framesDone = scenario.warmupFrames &&
            active.benchmarkPhaseFrames >= *scenario.warmupFrames;
          if (secondsDone || framesDone) {
            // Warmup never carries GPU frame tags or results into measurement.
            renderer.resetGpuTimingResults();
            active.benchmarkPhaseStart = phaseNow;
            active.benchmarkPhaseFrames = 0;
            active.stage = ActiveControlOperation::Stage::BenchmarkMeasure;
          }
        } else if (active.stage == ActiveControlOperation::Stage::BenchmarkMeasure) {
          const double seconds = std::chrono::duration<double>(
            phaseNow - active.benchmarkPhaseStart
          ).count();
          setBenchmarkCamera(benchmark::cameraAt(scenario, seconds));
          const RendererFrameDiagnostics& render = renderer.lastFrameDiagnostics();
          const ClientGame* game = session.game();
          const SnapshotDiagnostics snapshot = game != nullptr
            ? game->snapshotDiagnostics() : SnapshotDiagnostics{};
          benchmark::FrameSample sample;
          sample.index = active.benchmarkPhaseFrames;
          sample.elapsedSeconds = seconds;
          sample.frameMilliseconds = outerFrameMilliseconds;
          sample.sceneBuildMilliseconds = render.sceneBuildMilliseconds;
          sample.vertexUploadMilliseconds = render.gpuVertexUploadMilliseconds;
          sample.swapchainAcquireMilliseconds = render.swapchainAcquireMilliseconds;
          sample.drawIssueMilliseconds = render.worldDrawIssueMilliseconds;
          sample.submitMilliseconds = render.submitMilliseconds;
          sample.renderCpuMilliseconds = render.totalRenderMilliseconds;
          sample.lateMouseSampleMilliseconds =
            render.lateMouseSampleMilliseconds;
          sample.mouseSampleToSubmitMilliseconds =
            render.mouseSampleToSubmitMilliseconds;
          sample.mouseSamplePhaseGainMilliseconds =
            render.mouseSamplePhaseGainMilliseconds;
          sample.lateMouseSampleEnabled = render.lateMouseSampleEnabled;
          sample.lateMouseSampleApplied = render.lateMouseSampleApplied;
          sample.snapshotDecodeMilliseconds = snapshot.snapshotDecodeMilliseconds;
          sample.snapshotApplyMilliseconds = snapshot.snapshotApplyMilliseconds;
          sample.networkProcessingMilliseconds =
            active.lastBenchmarkFrameTiming.milliseconds(
              benchmark::TimingSubsystem::NetworkProcessing
            );
          sample.simulationMilliseconds =
            active.lastBenchmarkFrameTiming.milliseconds(
              benchmark::TimingSubsystem::Simulation
            );
          sample.movementCollisionMilliseconds =
            active.lastBenchmarkFrameTiming.milliseconds(
              benchmark::TimingSubsystem::MovementCollision
            );
          sample.tracesMilliseconds =
            active.lastBenchmarkFrameTiming.milliseconds(
              benchmark::TimingSubsystem::Traces
            );
          sample.interpolationMilliseconds =
            active.lastBenchmarkFrameTiming.milliseconds(
              benchmark::TimingSubsystem::Interpolation
            );
          sample.animationMilliseconds =
            active.lastBenchmarkFrameTiming.milliseconds(
              benchmark::TimingSubsystem::Animation
            );
          sample.worldVisibilityMilliseconds = render.worldVisibilityMilliseconds;
          sample.renderInstanceConstructionMilliseconds =
            render.renderInstanceConstructionMilliseconds;
          sample.worldCommandEncodingMilliseconds =
            render.worldCommandEncodingMilliseconds;
          sample.dynamicCommandEncodingMilliseconds =
            render.dynamicCommandEncodingMilliseconds;
          sample.uiMilliseconds = render.uiMilliseconds;
          sample.uploadedVertices = render.totalUploadedVertices;
          sample.renderedTriangles =
            render.worldSubmittedTriangles + render.dynamicTriangles;
          sample.worldDraws = render.worldDrawCalls;
          sample.worldSubmittedTriangles = render.worldSubmittedTriangles;
          sample.worldSubmittedRanges = render.worldSubmittedRanges;
          sample.worldTotalChunks = render.worldTotalChunks;
          sample.worldVisibleChunks = render.worldVisibleChunks;
          sample.worldCulledChunks = render.worldCulledChunks;
          sample.worldVisibilityTestedNodes = render.worldVisibilityTestedNodes;
          sample.worldVisibilityQueryMilliseconds =
            render.worldVisibilityQueryMilliseconds;
          sample.visiblePlayers = render.visibleRemotePlayers;
          sample.projectileCount = render.projectilesRendered;
          sample.effectCount = render.activeTransientEffects;
          sample.lightCount = render.activeTemporaryLights;
          sample.particleCount = render.activeImpactParticles;
          sample.transparentEffectCount =
            render.transparentEffectsSubmitted;
          sample.instanceUploadBytes = render.projectileInstanceUploadBytes +
            render.tracerInstanceUploadBytes + render.explosionInstanceUploadBytes +
            render.remoteWeaponInstanceUploadBytes;
          sample.instanceDraws = render.projectileMeshDrawCalls +
            render.projectileGlowDrawCalls + render.tracerDrawCalls +
            render.explosionDrawCalls + render.remoteWeaponDrawCalls;
          // Applicability describes rendered work, even when GPU timing is
          // unavailable or its fixed query ring has no free slot.
          sample.outlineGpuTimingApplicable =
            render.outlineCompositeEnabled;
          if (
            active.benchmarkSamples.size() ==
            active.benchmarkSamples.capacity()
          ) {
            completeControlError(
              "benchmark_sample_capacity",
              "benchmark exceeded its allocation-free render sample capacity"
            );
            continue;
          }
          active.benchmarkSamples.push_back(sample);
          // Results can arrive several frames late. Match only the renderer's
          // stored benchmark id, after this frame's CPU sample exists.
          for (const GpuFrameTimingResult& timing :
               renderer.takeGpuTimingResults()) {
            (void)benchmark::applyGpuFrameTiming(
              active.benchmarkSamples,
              {
                .benchmarkFrameIndex = timing.benchmarkFrameIndex,
                .gpuPrimaryCommandBufferMilliseconds =
                  timing.gpuPrimaryCommandBufferMilliseconds,
                .passApplicable = timing.passApplicable,
                .passMilliseconds = timing.passMilliseconds,
                .outlineApplicable = timing.outlineApplicable,
                .outlineGpuMilliseconds = timing.outlineGpuMilliseconds,
                .readbackLatencyFrames = timing.readbackLatencyFrames,
                .unavailableReason = timing.unavailableReason,
              }
            );
          }
          ++active.benchmarkPhaseFrames;
          const bool secondsDone = scenario.measuredSeconds && seconds >= *scenario.measuredSeconds;
          const bool framesDone = scenario.measuredFrames &&
            active.benchmarkPhaseFrames >= *scenario.measuredFrames;
          if (secondsDone || framesDone) {
            // CPU sampling has stopped. Any wait for GPU readback belongs to
            // the drain stage and cannot change measured frame telemetry.
            active.stage = ActiveControlOperation::Stage::BenchmarkGpuDrain;
          }
        } else if (
          active.stage == ActiveControlOperation::Stage::BenchmarkGpuDrain
        ) {
          renderer.drainGpuTimings();
          for (const GpuFrameTimingResult& timing :
               renderer.takeGpuTimingResults()) {
            (void)benchmark::applyGpuFrameTiming(
              active.benchmarkSamples,
              {
                .benchmarkFrameIndex = timing.benchmarkFrameIndex,
                .gpuPrimaryCommandBufferMilliseconds =
                  timing.gpuPrimaryCommandBufferMilliseconds,
                .passApplicable = timing.passApplicable,
                .passMilliseconds = timing.passMilliseconds,
                .outlineApplicable = timing.outlineApplicable,
                .outlineGpuMilliseconds = timing.outlineGpuMilliseconds,
                .readbackLatencyFrames = timing.readbackLatencyFrames,
                .unavailableReason = timing.unavailableReason,
              }
            );
          }
          if (scenario.screenshots.empty()) {
            active.stage = ActiveControlOperation::Stage::BenchmarkFinalize;
          } else {
            active.benchmarkScreenshotIndex = 0;
            const benchmark::Screenshot& screenshot = scenario.screenshots[0];
            const double measuredSeconds =
              active.benchmarkSamples.empty()
                ? 0.0 : active.benchmarkSamples.back().elapsedSeconds;
            const double cameraSeconds =
              scenario.measuredSeconds.value_or(measuredSeconds) *
              screenshot.progress;
            setBenchmarkCamera(benchmark::cameraAt(scenario, cameraSeconds));
            active.requiredRenderedFrame = renderedFrameSerial + 1U;
            active.deadline = phaseNow + std::chrono::seconds(20);
            active.stage =
              ActiveControlOperation::Stage::BenchmarkWaitingForCameraFrame;
          }
        } else if (active.stage == ActiveControlOperation::Stage::BenchmarkWaitingForCameraFrame) {
          if (renderedFrameSerial >= active.requiredRenderedFrame) {
            active.stage = ActiveControlOperation::Stage::BenchmarkCaptureReady;
          } else if (phaseNow > active.deadline) {
            completeControlError("capture_timeout", "benchmark screenshot renderer timeout");
          }
        }
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage == ActiveControlOperation::Stage::WaitingForFrames) {
      ActiveControlOperation& active = *activeControlOperation;
      if (renderedFrameSerial >= active.requiredRenderedFrame) {
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["rendered_frame"] = dev::JsonValue::numberValue(renderedFrameSerial);
        result.object["waited_frames"] =
          dev::JsonValue::numberValue(active.queued.request.waitFrames);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(active.queued.request.id, std::move(result))
        );
        activeControlOperation.reset();
      } else if (std::chrono::steady_clock::now() > active.deadline) {
        completeControlError("frame_wait_timeout", "renderer did not finish the requested frames");
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage ==
          ActiveControlOperation::Stage::WaitingForClientTick) {
      ActiveControlOperation& active = *activeControlOperation;
      // This tick is local fixed-step progress and does not imply server
      // progress.
      if (clientTick >= active.queued.request.minimumTick) {
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["min_tick"] =
          dev::JsonValue::numberValue(active.queued.request.minimumTick);
        result.object["client_tick"] = dev::JsonValue::numberValue(clientTick);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(active.queued.request.id, std::move(result))
        );
        activeControlOperation.reset();
      } else if (std::chrono::steady_clock::now() > active.deadline) {
        completeControlError(
          "client_tick_timeout",
          "client simulation did not reach the requested tick within 30 seconds"
        );
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage ==
          ActiveControlOperation::Stage::WaitingForSnapshotTick) {
      ActiveControlOperation& active = *activeControlOperation;
      const ClientGame* game = session.game();
      // Only an accepted authoritative snapshot may satisfy a server tick wait.
      if (game != nullptr && game->hasSnapshot() &&
          game->snapshot().serverTick >= active.queued.request.minimumTick) {
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["min_tick"] =
          dev::JsonValue::numberValue(active.queued.request.minimumTick);
        result.object["snapshot_tick"] =
          dev::JsonValue::numberValue(game->snapshot().serverTick);
        developerControl.complete(
          active.queued.token,
          dev::successResponse(active.queued.request.id, std::move(result))
        );
        activeControlOperation.reset();
      } else if (std::chrono::steady_clock::now() > active.deadline) {
        completeControlError(
          "snapshot_tick_timeout",
          "client did not accept the requested server snapshot tick within 30 seconds"
        );
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage ==
          ActiveControlOperation::Stage::WaitingForCommandAck) {
      ActiveControlOperation& active = *activeControlOperation;
      const ClientGame* game = session.game();
      // Prediction cannot satisfy this wait; the ack must come from a snapshot.
      if (game != nullptr && game->hasSnapshot() && game->hasAcknowledgedCommand() &&
          isSequenceAcknowledged(
            active.queued.request.commandSequence,
            game->lastAcknowledgedCommand()
          )) {
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["sequence"] =
          dev::JsonValue::numberValue(active.queued.request.commandSequence);
        result.object["acknowledged_sequence"] =
          dev::JsonValue::numberValue(game->lastAcknowledgedCommand());
        developerControl.complete(
          active.queued.token,
          dev::successResponse(active.queued.request.id, std::move(result))
        );
        activeControlOperation.reset();
      } else if (std::chrono::steady_clock::now() > active.deadline) {
        completeControlError(
          "command_ack_timeout",
          "server did not acknowledge the requested command within 30 seconds"
        );
      }
    }
    if (armedPhaseCapture.has_value() &&
        !armedPhaseCapture->result.has_value() &&
        armedPhaseCapture->error.empty() &&
        Clock::now() > armedPhaseCapture->deadline) {
      armedPhaseCapture->error =
        "the named renderer phase did not occur within 30 seconds";
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage ==
          ActiveControlOperation::Stage::WaitingForPhaseCapture) {
      ActiveControlOperation& active = *activeControlOperation;
      if (!armedPhaseCapture.has_value()) {
        completeControlError(
          "capture_not_armed",
          "the armed phase capture no longer exists"
        );
      } else if (!armedPhaseCapture->error.empty()) {
        const std::string captureError = armedPhaseCapture->error;
        armedPhaseCapture.reset();
        completeControlError("phase_capture_failed", captureError);
      } else if (armedPhaseCapture->result.has_value()) {
        dev::JsonValue result = std::move(*armedPhaseCapture->result);
        armedPhaseCapture.reset();
        developerControl.complete(
          active.queued.token,
          dev::successResponse(active.queued.request.id, std::move(result))
        );
        activeControlOperation.reset();
      }
    }
    const bool benchmarkFrameTimingEnabled =
      activeControlOperation.has_value() &&
      activeControlOperation->queued.request.operation ==
        dev::ControlOperation::RunBenchmark &&
      activeControlOperation->stage ==
        ActiveControlOperation::Stage::BenchmarkMeasure;
    benchmark::TimingValues currentBenchmarkFrameTiming;
    benchmark::TimingSinkScope benchmarkFrameTimingScope(
      benchmarkFrameTimingEnabled ? &currentBenchmarkFrameTiming : nullptr,
      nullptr
    );
    if (console.getBool("r_perf_reset")) {
      perfTelemetry.clear();
      (void)console.execute("set r_perf_reset 0");
    }
    outerFrameTimes.push(outerFrameMilliseconds);
    frameStatsAccumulatorSeconds += outerFrameElapsed.count();
    if (frameStatsAccumulatorSeconds >= 0.25F) {
      displayedFrameTimes = outerFrameTimes.summarize();
      frameStatsAccumulatorSeconds = 0.0F;
    }

    const bool lateMouseSamplingEnabled =
      console.getBool("cl_late_mouse_sample");
    const bool lateMouseSamplingJustDisabled =
      !lateMouseSamplingEnabled && lateMouseSamplingWasEnabled;
    if (lateMouseSamplingEnabled && !lateMouseSamplingWasEnabled) {
      // Clear counts gathered while the event path owned mouse input. Queued
      // motion is still drained below, but this frame reads fresh relative
      // counts as one early sample.
      float ignoredMouseDeltaX = 0.0F;
      float ignoredMouseDeltaY = 0.0F;
      (void)SDL_GetRelativeMouseState(
        &ignoredMouseDeltaX,
        &ignoredMouseDeltaY
      );
    }
    if (!lateMouseSamplingEnabled) {
      pendingLateViewModelMouseDeltaX = 0.0F;
      pendingLateViewModelMouseDeltaY = 0.0F;
    }
    lateMouseSamplingWasEnabled = lateMouseSamplingEnabled;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
        const std::string key = keyName(event.key.scancode);
        if (chatState.inputOpen) {
          if (!pressed) {
            (void)bindings.handleKey(key, false);
            break;
          }
          if ((event.key.key == SDLK_A) && (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) {
            clearChatHistorySelection(chatState);
            selectAll(chatState.input, chatState.selection);
            chatState.cursorIndex = chatState.input.size();
          } else if (isClipboardPasteKey(event.key)) {
            pasteClipboardTextIntoChat(chatState);
          } else if (isClipboardCopyKey(event.key)) {
            copyTextToClipboard(chatClipboardText(window, chatState));
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            chatState.input.clear();
            chatState.cursorIndex = 0U;
            clearChatSelections(chatState);
            setChatOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            clearChatHistorySelection(chatState);
            backspaceSelectionOrText(
              chatState.input,
              chatState.cursorIndex,
              chatState.selection
            );
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            clearChatSelections(chatState);
            moveCursorLeft(chatState.input, chatState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            clearChatSelections(chatState);
            moveCursorRight(chatState.input, chatState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!chatState.input.empty()) {
              chatState.pendingMessage = chatState.input;
            }
            chatState.input.clear();
            chatState.cursorIndex = 0U;
            clearChatSelections(chatState);
            setChatOpen(false);
          }
          break;
        }
        if (settingsMenu.open) {
          if (!pressed) {
            break;
          }
          // The bound settings command remains a toggle while the menu owns
          // input. Escape is the explicit draft-cancel close path.
          if (bindings.binding(key) == "settings") {
            setSettingsOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setSettingsOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_UP) {
            settingsMenu.selectedRow =
              (settingsMenu.selectedRow + kSettingsRowCount - 1) %
              kSettingsRowCount;
            const OptionMenuLayout layout = optionMenuLayoutForWindow(
                window, kSettingsRowCount, settingsMenu.scrollRows);
            keepOptionMenuSelectionVisible(
                settingsMenu.selectedRow, settingsMenu.scrollRows,
                layout.visibleRows, kSettingsRowCount);
          } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
            settingsMenu.selectedRow =
              (settingsMenu.selectedRow + 1) % kSettingsRowCount;
            const OptionMenuLayout layout = optionMenuLayoutForWindow(
                window, kSettingsRowCount, settingsMenu.scrollRows);
            keepOptionMenuSelectionVisible(
                settingsMenu.selectedRow, settingsMenu.scrollRows,
                layout.visibleRows, kSettingsRowCount);
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            adjustSettingsMenuValue(settingsMenu, -1);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            adjustSettingsMenuValue(settingsMenu, 1);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (settingsMenu.selectedRow == kSettingsResetRow) {
              applyGraphicsProfile(settingsMenu, static_cast<int>(GraphicsProfile::Default));
            } else if (settingsMenu.selectedRow == kSettingsApplyRow) {
              applySettingsMenu(console, settingsMenu);
            } else if (settingsMenu.selectedRow == kSettingsCloseRow) {
              setSettingsOpen(false);
            } else {
              adjustSettingsMenuValue(settingsMenu, 1);
            }
          }
          break;
        }
        if (miscMenu.open) {
          if (!pressed) {
            break;
          }
          constexpr int rowCount = static_cast<int>(MiscMenuRow::Count);
          if (bindings.binding(key) == "misc") {
            setMiscMenuOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setMiscMenuOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_UP) {
            miscMenu.selectedRow =
                (miscMenu.selectedRow + rowCount - 1) % rowCount;
            const OptionMenuLayout layout = optionMenuLayoutForWindow(
                window, static_cast<std::size_t>(rowCount),
                miscMenu.scrollRows);
            keepOptionMenuSelectionVisible(
                miscMenu.selectedRow, miscMenu.scrollRows, layout.visibleRows,
                static_cast<std::size_t>(rowCount));
          } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
            miscMenu.selectedRow = (miscMenu.selectedRow + 1) % rowCount;
            const OptionMenuLayout layout = optionMenuLayoutForWindow(
                window, static_cast<std::size_t>(rowCount),
                miscMenu.scrollRows);
            keepOptionMenuSelectionVisible(
                miscMenu.selectedRow, miscMenu.scrollRows, layout.visibleRows,
                static_cast<std::size_t>(rowCount));
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            (void)adjustMiscMenuValue(
                console, static_cast<MiscMenuRow>(miscMenu.selectedRow), -1);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            (void)adjustMiscMenuValue(
                console, static_cast<MiscMenuRow>(miscMenu.selectedRow), 1);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (static_cast<MiscMenuRow>(miscMenu.selectedRow) ==
                MiscMenuRow::Close) {
              setMiscMenuOpen(false);
            } else {
              (void)adjustMiscMenuValue(
                  console, static_cast<MiscMenuRow>(miscMenu.selectedRow), 1);
            }
          }
          break;
        }
        if (consoleState.open) {
          if (!pressed) {
            if (bindings.binding(key) == "toggleconsole") {
              suppressNextTextInput = false;
            }
            executeBindingCommands(bindings.handleKey(key, false));
            break;
          }
          if (bindings.binding(key) == "toggleconsole") {
            suppressNextTextInput = true;
            executeBindingCommands(bindings.handleKey(key, true));
            applyConsoleToggle();
            break;
          }
          if (event.key.key == SDLK_A &&
              (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) {
            clearConsoleSelection(consoleState);
            selectAll(consoleState.input, consoleState.inputSelection);
            consoleState.cursorIndex = consoleState.input.size();
          } else if (isClipboardPasteKey(event.key)) {
            consoleState.hasSelection = false;
            consoleState.selecting = false;
            pasteClipboardTextIntoConsole(consoleState.input,
                                          consoleState.cursorIndex,
                                          consoleState.inputSelection);
          } else if (isClipboardCopyKey(event.key)) {
            copyTextToClipboard(
                consoleClipboardTextForWindow(window, consoleState));
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setConsoleOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (!consoleState.input.empty()) {
              consoleState.hasSelection = false;
              consoleState.selecting = false;
              backspaceSelectionOrText(consoleState.input,
                                       consoleState.cursorIndex,
                                       consoleState.inputSelection);
            }
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            clearConsoleSelection(consoleState);
            moveConsoleCursorLeft(consoleState.input, consoleState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            clearConsoleSelection(consoleState);
            moveConsoleCursorRight(consoleState.input, consoleState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!consoleState.input.empty()) {
              clearConsoleSelection(consoleState);
              appendConsoleOutput(consoleState, "] " + consoleState.input);
              const std::string result = console.execute(consoleState.input);
              if (!result.empty()) {
                appendConsoleOutput(consoleState, result);
              }
              applyConsoleToggle();
              consoleState.history.push_back(consoleState.input);
              consoleState.historyIndex = consoleState.history.size();
              consoleState.input.clear();
              consoleState.cursorIndex = 0U;
            }
          } else if (event.key.scancode == SDL_SCANCODE_UP &&
                     !consoleState.history.empty()) {
            if (consoleState.historyIndex > 0) {
              --consoleState.historyIndex;
            }
            clearConsoleSelection(consoleState);
            consoleState.input = consoleState.history[consoleState.historyIndex];
            consoleState.cursorIndex = consoleState.input.size();
          } else if (event.key.scancode == SDL_SCANCODE_DOWN &&
                     !consoleState.history.empty()) {
            if (consoleState.historyIndex + 1 < consoleState.history.size()) {
              ++consoleState.historyIndex;
              clearConsoleSelection(consoleState);
              consoleState.input = consoleState.history[consoleState.historyIndex];
              consoleState.cursorIndex = consoleState.input.size();
            } else {
              consoleState.historyIndex = consoleState.history.size();
              clearConsoleSelection(consoleState);
              consoleState.input.clear();
              consoleState.cursorIndex = 0U;
            }
          } else if (event.key.scancode == SDL_SCANCODE_TAB) {
            const std::string prefix = consoleCompletionPrefix(
              consoleState.input,
              consoleState.cursorIndex
            );
            const std::vector<std::string> matches = console.complete(prefix);
            if (matches.size() == 1) {
              clearConsoleSelection(consoleState);
              replaceConsoleCompletion(
                consoleState.input,
                consoleState.cursorIndex,
                matches[0]
              );
            } else if (!matches.empty()) {
              clearConsoleSelection(consoleState);
              std::string line;
              for (const std::string& match : matches) {
                line += match + ' ';
              }
              appendConsoleOutput(consoleState, line);
            }
          }
          break;
        }
        if (bindings.binding(key) == "toggleconsole") {
          suppressNextTextInput = pressed;
        }
        if (pressed && bindings.binding(key) == "messagemode") {
          suppressNextTextInput = true;
        }
        if (
          pressed &&
          event.key.scancode == SDL_SCANCODE_RETURN &&
          (event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0
        ) {
          const bool windowed = console.getInt("vid_fullscreen") == 0;
          (void)console.execute(windowed ? "set vid_fullscreen 1" : "set vid_fullscreen 0");
          break;
        }
        executeBindingCommands(bindings.handleKey(key, pressed));
        applyConsoleToggle();
        applySettingsMenuToggle();
        applyMiscMenuToggle();
        if (openChatRequested && !consoleState.open && !settingsMenu.open &&
            !miscMenu.open) {
          openChatRequested = false;
          setChatOpen(true);
        }
        break;
      }
      case SDL_EVENT_TEXT_INPUT:
        if (
          suppressNextTextInput &&
          (
            isConsoleToggleText(event.text.text) ||
            std::string_view(event.text.text) == "t" ||
            std::string_view(event.text.text) == "T"
          )
        ) {
          suppressNextTextInput = false;
        } else if (consoleState.open) {
          suppressNextTextInput = false;
          consoleState.scrollRows = 0U;
          consoleState.hasSelection = false;
          consoleState.selecting = false;
          replaceSelectionOrInsert(consoleState.input, consoleState.cursorIndex,
                                   consoleState.inputSelection, event.text.text,
                                   TextInputFilter::Console);
        } else if (chatState.inputOpen) {
          suppressNextTextInput = false;
          clearChatHistorySelection(chatState);
          replaceSelectionOrInsert(
            chatState.input,
            chatState.cursorIndex,
            chatState.selection,
            event.text.text,
            TextInputFilter::Chat,
            kMaxChatMessageBytes
          );
        } else if (settingsMenu.open || miscMenu.open) {
          suppressNextTextInput = false;
        } else {
          suppressNextTextInput = false;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const std::string key = mouseButtonName(event.button.button);
        if (consoleState.open && event.button.button == SDL_BUTTON_LEFT) {
          if (pressed) {
            beginConsoleSelection(
              window,
              consoleState,
              event.button.x,
              event.button.y
            );
          } else {
            updateConsoleSelection(
              window,
              consoleState,
              event.button.x,
              event.button.y
            );
            consoleState.selecting = false;
            consoleState.selectingInput = false;
            if (consoleState.selectionAnchor == consoleState.selectionFocus &&
                consoleState.inputSelection.anchor ==
                    consoleState.inputSelection.focus) {
              clearConsoleSelection(consoleState);
            }
          }
        } else if (chatState.inputOpen && event.button.button == SDL_BUTTON_LEFT) {
          if (pressed) {
            beginChatSelection(
              window,
              chatState,
              event.button.x,
              event.button.y
            );
          } else {
            updateChatSelection(
              window,
              chatState,
              event.button.x,
              event.button.y
            );
            chatState.selecting = false;
            chatState.selectingHistory = false;
            if (chatState.selection.anchor == chatState.selection.focus &&
                chatState.historySelectionAnchor ==
                    chatState.historySelectionFocus) {
              clearChatSelections(chatState);
            }
          }
        } else if (settingsMenu.open) {
          if (event.button.button != SDL_BUTTON_LEFT) {
            break;
          }
          const OptionMenuLayout layout = optionMenuLayoutForWindow(
              window, kSettingsRowCount, settingsMenu.scrollRows);
          if (pressed && optionMenuPointInScrollbarTrack(layout, event.button.x,
                                                         event.button.y)) {
            settingsMenu.pressedRow = -1;
            settingsMenu.hoveredRow = -1;
            settingsMenu.scrollbarDragging = true;
            settingsMenu.scrollbarGrabOffsetY =
                optionMenuPointInScrollbarThumb(layout, event.button.x,
                                                event.button.y)
                    ? event.button.y - layout.scrollbarThumbY
                    : layout.scrollbarThumbHeight * 0.5F;
            settingsMenu.scrollRows = optionMenuScrollForThumbPointer(
                layout, event.button.y, settingsMenu.scrollbarGrabOffsetY);
            break;
          }
          if (settingsMenu.scrollbarDragging) {
            settingsMenu.scrollRows = optionMenuScrollForThumbPointer(
                layout, event.button.y, settingsMenu.scrollbarGrabOffsetY);
            if (!pressed) {
              settingsMenu.scrollbarDragging = false;
            }
            break;
          }
          const int row = optionMenuRowAt(layout, settingsMenu.scrollRows,
                                          kSettingsRowCount, event.button.y);
          if (pressed) {
            settingsMenu.pressedRow = row;
            if (settingsMenu.pressedRow >= 0) {
              settingsMenu.selectedRow = settingsMenu.pressedRow;
            }
          } else if (settingsMenu.pressedRow >= 0 && settingsMenu.pressedRow == row) {
            const int clickedRow = settingsMenu.pressedRow;
            settingsMenu.pressedRow = -1;
            if (clickedRow == kSettingsResetRow) {
              applyGraphicsProfile(settingsMenu, static_cast<int>(GraphicsProfile::Default));
            } else if (clickedRow == kSettingsApplyRow) {
              applySettingsMenu(console, settingsMenu);
            } else if (clickedRow == kSettingsCloseRow) {
              setSettingsOpen(false);
            } else {
              const float arrowX =
                  layout.panelX + layout.panelWidth - 28.0F - 9.0F * 18.0F;
              adjustSettingsMenuValue(settingsMenu, event.button.x < arrowX + 36.0F ? -1 : 1);
            }
          } else {
            settingsMenu.pressedRow = -1;
          }
          break;
        } else if (miscMenu.open) {
          if (event.button.button != SDL_BUTTON_LEFT) {
            break;
          }
          constexpr std::size_t itemCount =
              static_cast<std::size_t>(MiscMenuRow::Count);
          const OptionMenuLayout layout =
              optionMenuLayoutForWindow(window, itemCount, miscMenu.scrollRows);
          if (pressed && optionMenuPointInScrollbarTrack(layout, event.button.x,
                                                         event.button.y)) {
            miscMenu.pressedRow = -1;
            miscMenu.hoveredRow = -1;
            miscMenu.scrollbarDragging = true;
            miscMenu.scrollbarGrabOffsetY =
                optionMenuPointInScrollbarThumb(layout, event.button.x,
                                                event.button.y)
                    ? event.button.y - layout.scrollbarThumbY
                    : layout.scrollbarThumbHeight * 0.5F;
            miscMenu.scrollRows = optionMenuScrollForThumbPointer(
                layout, event.button.y, miscMenu.scrollbarGrabOffsetY);
            break;
          }
          if (miscMenu.scrollbarDragging) {
            miscMenu.scrollRows = optionMenuScrollForThumbPointer(
                layout, event.button.y, miscMenu.scrollbarGrabOffsetY);
            if (!pressed) {
              miscMenu.scrollbarDragging = false;
            }
            break;
          }
          const int row = optionMenuRowAt(layout, miscMenu.scrollRows,
                                          itemCount, event.button.y);
          if (pressed) {
            miscMenu.pressedRow = row;
            if (row >= 0) {
              miscMenu.selectedRow = row;
            }
          } else if (miscMenu.pressedRow >= 0 && miscMenu.pressedRow == row) {
            const int clickedRow = miscMenu.pressedRow;
            miscMenu.pressedRow = -1;
            if (static_cast<MiscMenuRow>(clickedRow) == MiscMenuRow::Close) {
              setMiscMenuOpen(false);
            } else {
              const float arrowX =
                  layout.panelX + layout.panelWidth - 28.0F - 9.0F * 18.0F;
              (void)adjustMiscMenuValue(
                  console, static_cast<MiscMenuRow>(clickedRow),
                  event.button.x < arrowX + 36.0F ? -1 : 1);
            }
          } else {
            miscMenu.pressedRow = -1;
          }
          break;
        } else if (!consoleState.open && !chatState.inputOpen) {
          executeBindingCommands(bindings.handleKey(key, pressed));
          applyConsoleToggle();
          applySettingsMenuToggle();
          applyMiscMenuToggle();
        } else if (!pressed) {
          executeBindingCommands(bindings.handleKey(key, false));
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION:
        if (settingsMenu.open) {
          input.mouseDeltaX = 0.0F;
          input.mouseDeltaY = 0.0F;
          const OptionMenuLayout layout = optionMenuLayoutForWindow(
              window, kSettingsRowCount, settingsMenu.scrollRows);
          if (settingsMenu.scrollbarDragging) {
            settingsMenu.scrollRows = optionMenuScrollForThumbPointer(
                layout, event.motion.y, settingsMenu.scrollbarGrabOffsetY);
            settingsMenu.hoveredRow = -1;
          } else {
            settingsMenu.hoveredRow =
                optionMenuPointInScrollbarTrack(layout, event.motion.x,
                                                event.motion.y)
                    ? -1
                    : optionMenuRowAt(layout, settingsMenu.scrollRows,
                                      kSettingsRowCount, event.motion.y);
          }
        } else if (miscMenu.open) {
          input.mouseDeltaX = 0.0F;
          input.mouseDeltaY = 0.0F;
          constexpr std::size_t itemCount =
              static_cast<std::size_t>(MiscMenuRow::Count);
          const OptionMenuLayout layout =
              optionMenuLayoutForWindow(window, itemCount, miscMenu.scrollRows);
          if (miscMenu.scrollbarDragging) {
            miscMenu.scrollRows = optionMenuScrollForThumbPointer(
                layout, event.motion.y, miscMenu.scrollbarGrabOffsetY);
            miscMenu.hoveredRow = -1;
          } else {
            miscMenu.hoveredRow =
                optionMenuPointInScrollbarTrack(layout, event.motion.x,
                                                event.motion.y)
                    ? -1
                    : optionMenuRowAt(layout, miscMenu.scrollRows, itemCount,
                                      event.motion.y);
          }
        } else if (consoleState.open) {
          updateConsoleSelection(
            window,
            consoleState,
            event.motion.x,
            event.motion.y
          );
        } else if (chatState.inputOpen) {
          updateChatSelection(
            window,
            chatState,
            event.motion.x,
            event.motion.y
          );
        } else if (
          !lateMouseSamplingEnabled &&
          !lateMouseSamplingJustDisabled
        ) {
          input.mouseDeltaX += event.motion.xrel;
          input.mouseDeltaY += event.motion.yrel;
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        if (settingsMenu.open) {
          const OptionMenuLayout layout = optionMenuLayoutForWindow(
              window, kSettingsRowCount, settingsMenu.scrollRows);
          settingsMenu.scrollRows = optionMenuScrollForWheel(
              layout, settingsMenu.scrollRows, event.wheel.y);
        } else if (miscMenu.open) {
          constexpr std::size_t itemCount =
              static_cast<std::size_t>(MiscMenuRow::Count);
          const OptionMenuLayout layout =
              optionMenuLayoutForWindow(window, itemCount, miscMenu.scrollRows);
          miscMenu.scrollRows = optionMenuScrollForWheel(
              layout, miscMenu.scrollRows, event.wheel.y);
        } else if (consoleState.open) {
          constexpr std::size_t consoleWheelRows = 3U;
          if (event.wheel.y > 0.0F) {
            const ConsoleTextLayout layout =
              consoleLayoutForWindow(window, consoleState);
            consoleState.scrollRows = std::min(
              consoleState.scrollRows + consoleWheelRows,
              layout.maxScrollRows
            );
          } else if (event.wheel.y < 0.0F) {
            consoleState.scrollRows =
              consoleState.scrollRows > consoleWheelRows
                ? consoleState.scrollRows - consoleWheelRows
                : 0U;
          }
          clearConsoleSelection(consoleState);
        } else if (chatState.inputOpen || chatHistoryPressCount > 0) {
          constexpr std::size_t chatWheelRows = 3U;
          // Clamp against wrapped rows, not the protocol message capacity. This
          // keeps one wheel step reversible even when messages span many rows.
          const ChatTextLayout layout = chatLayoutForWindow(window, chatState);
          if (event.wheel.y > 0.0F) {
            chatState.scrollRows = std::min(
              chatState.scrollRows + chatWheelRows,
              layout.maxScrollRows
            );
          } else if (event.wheel.y < 0.0F) {
            chatState.scrollRows = chatState.scrollRows > chatWheelRows
              ? chatState.scrollRows - chatWheelRows
              : 0U;
          }
          clearChatHistorySelection(chatState);
        }
        break;
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        clearConsoleOutputSelection(consoleState);
        clearChatHistorySelection(chatState);
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        executeBindingCommands(bindings.releaseAll());
        consoleState.selecting = false;
        consoleState.selectingInput = false;
        chatState.selecting = false;
        chatState.selectingHistory = false;
        settingsMenu.scrollbarDragging = false;
        miscMenu.scrollbarDragging = false;
        input.mouseDeltaX = 0.0F;
        input.mouseDeltaY = 0.0F;
        pendingLateViewModelMouseDeltaX = 0.0F;
        pendingLateViewModelMouseDeltaY = 0.0F;
        break;
      default:
        break;
      }
    }

    if (clearRequested) {
      consoleState.output.clear();
      clearRequested = false;
    }
    if (consoleState.open) {
      float cursorX = 0.0F;
      float cursorY = 0.0F;
      (void)SDL_GetMouseState(&cursorX, &cursorY);
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(window, &width, &height);
      consoleState.cat.update(
        outerFrameElapsed.count(),
        cursorX,
        cursorY,
        static_cast<float>(width),
        static_cast<float>(height)
      );
    }
    if (showChatRequested) {
      chatState.visibleUntil = Clock::now() + std::chrono::seconds(5);
      chatState.scrollRows = 0U;
      showChatRequested = false;
    }
    if (settingsMenuRequested) {
      if (consoleState.open) {
        setConsoleOpen(false);
      }
      if (chatState.inputOpen) {
        setChatOpen(false);
      }
      if (miscMenu.open) {
        setMiscMenuOpen(false);
      }
      applySettingsMenuToggle();
    }
    if (miscMenuRequested) {
      if (consoleState.open) {
        setConsoleOpen(false);
      }
      if (chatState.inputOpen) {
        setChatOpen(false);
      }
      if (settingsMenu.open) {
        setSettingsOpen(false);
      }
      applyMiscMenuToggle();
    }
    const ClientGame* chatGame = session.game();
    if (chatGame != chatState.sourceGame) {
      chatState.sourceGame = chatGame;
      chatState.history.clear();
      chatState.lastSequence = 0U;
      chatState.scrollRows = 0U;
      clearChatHistorySelection(chatState);
    }
    if (chatGame != nullptr && !chatGame->chatHistory().empty()) {
      const auto& serverHistory = chatGame->chatHistory();
      const std::uint32_t latestSequence = serverHistory.back().sequence;
      if (latestSequence != chatState.lastSequence) {
        clearChatHistorySelection(chatState);
        chatState.history.clear();
        for (const ChatMessage& message : serverHistory) {
          chatState.history.push_back(ClientChatState::Message{
            message.playerIndex,
            message.message,
            message.speakerName,
          });
        }
        chatState.lastSequence = latestSequence;
        chatState.scrollRows = 0U;
        chatState.visibleUntil = Clock::now() + std::chrono::seconds(5);
      }
    }
    if (writeConfigRequested) {
      appendConsoleOutput(
        consoleState,
        saveClientConfig(console, bindings, configPath)
          ? "wrote " + configPath
          : "failed to write " + configPath
      );
      writeConfigRequested = false;
    }
    if (quitRequested) {
      running = false;
    }
    session.setNetworkSimulationConfig(
      developerNetworkSimulation.value_or(networkSimulationConfigFromConsole(console))
    );
    session.update();
    const bool currentCompatVSync = console.getBool("r_vsync");
    const int currentPresentModeInt = console.getInt("r_present_mode");
    // r_vsync remains a compatibility alias. Whichever cvar changed since the
    // last frame drives the other, avoiding a feedback loop between both names.
    if (currentCompatVSync != lastCompatVSync) {
      (void)console.execute(
        currentCompatVSync
          ? "set r_present_mode 0"
          : "set r_present_mode 2"
      );
      lastCompatVSync = currentCompatVSync;
      lastPresentModeInt = console.getInt("r_present_mode");
    } else if (currentPresentModeInt != lastPresentModeInt) {
      (void)console.execute(
        currentPresentModeInt == 0 ? "set r_vsync 1" : "set r_vsync 0"
      );
      lastCompatVSync = console.getBool("r_vsync");
      lastPresentModeInt = currentPresentModeInt;
    }

    const VideoSettings requestedVideoSettings = videoSettingsFromConsole(console);
    if (
      !videoRuntime.hasApplied ||
      !sameVideoSettings(requestedVideoSettings, videoRuntime.applied)
    ) {
      (void)applyVideoSettings(
        window,
        renderer,
        console,
        videoRuntime,
        requestedVideoSettings,
        consoleState
      );
      lastCompatVSync = console.getBool("r_vsync");
      lastPresentModeInt = console.getInt("r_present_mode");
    }
    const bool usePresentationView = true;
    const bool baseGameInputControlsView =
        usePresentationView && !consoleState.open && !chatState.inputOpen &&
        !settingsMenu.open && !miscMenu.open && !wasTeammateSpectating;
    const bool windowHasInputFocus =
      (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool lateMouseInputControlsView =
      baseGameInputControlsView &&
      windowHasInputFocus &&
      !session.spectator() &&
      !developmentCameraEnabled;
    const bool gameInputControlsView = lateMouseSamplingEnabled
      ? lateMouseInputControlsView
      : baseGameInputControlsView;
    const bool wantsRelativeMouse = !consoleState.open &&
                                    !chatState.inputOpen &&
                                    !settingsMenu.open && !miscMenu.open;

    if (wantsRelativeMouse != relativeMouseModeEnabled) {
      SDL_SetWindowRelativeMouseMode(window, wantsRelativeMouse);
      relativeMouseModeEnabled = wantsRelativeMouse;
    }
    std::uint64_t earlyMouseSampleNanoseconds = 0;
    if (lateMouseSamplingEnabled || lateMouseSamplingJustDisabled) {
      float relativeMouseDeltaX = 0.0F;
      float relativeMouseDeltaY = 0.0F;
      (void)SDL_GetRelativeMouseState(
        &relativeMouseDeltaX,
        &relativeMouseDeltaY
      );
      if (lateMouseSamplingEnabled) {
        earlyMouseSampleNanoseconds = steadyClockNanoseconds();
      }
      if (gameInputControlsView && relativeMouseModeEnabled) {
        input.mouseDeltaX = relativeMouseDeltaX;
        input.mouseDeltaY = relativeMouseDeltaY;
      } else {
        input.mouseDeltaX = 0.0F;
        input.mouseDeltaY = 0.0F;
      }
    }
    ClientGame* currentPresentationGame = session.game();
    if (currentPresentationGame == nullptr) {
      presentationView = {};
      presentationViewGame = nullptr;
      pendingLateViewModelMouseDeltaX = 0.0F;
      pendingLateViewModelMouseDeltaY = 0.0F;
      previousFrameUsedPresentationView = usePresentationView;
    } else if (currentPresentationGame != presentationViewGame) {
      // A new ClientGame represents a new connection/prediction timeline; do
      // not carry view initialization or mouse state across that authority
      // reset.
      presentationView = {};
      playerPresentationStates = {};
      viewModelPresentation.reset();
      pendingLateViewModelMouseDeltaX = 0.0F;
      pendingLateViewModelMouseDeltaY = 0.0F;
      presentationViewGame = currentPresentationGame;
    }
    const bool enteredPresentationView =
      usePresentationView && !previousFrameUsedPresentationView;
    if (enteredPresentationView) {
      presentationView.initialized = false;
    }
    if (
      usePresentationView &&
      !presentationView.initialized &&
      currentPresentationGame != nullptr &&
      currentPresentationGame->hasSnapshot()
    ) {
      const PlayerState& predictedPlayer =
        currentPresentationGame->predictedPlayer();
      presentationView.yawRadians = predictedPlayer.viewYawRadians;
      presentationView.pitchRadians = predictedPlayer.viewPitchRadians;
      presentationView.initialized = true;
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }
    const float earlyMouseDeltaX =
      gameInputControlsView ? input.mouseDeltaX : 0.0F;
    const float earlyMouseDeltaY =
      gameInputControlsView ? input.mouseDeltaY : 0.0F;
    const float viewModelMouseDeltaX = gameInputControlsView
      ? earlyMouseDeltaX + pendingLateViewModelMouseDeltaX
      : 0.0F;
    const float viewModelMouseDeltaY = gameInputControlsView
      ? earlyMouseDeltaY + pendingLateViewModelMouseDeltaY
      : 0.0F;
    pendingLateViewModelMouseDeltaX = 0.0F;
    pendingLateViewModelMouseDeltaY = 0.0F;
    const MouseAimSettings frameMouseAimSettings =
      mouseAimSettingsFromConsole(
        console,
        zoomPressCount > 0,
        selectedWeapon == Weapon::Railgun,
        selectedWeapon == Weapon::Railgun ? sniperAdsAmount : 1.0F
      );
    const float viewPitchBeforeEarlyMouseSample =
      presentationView.pitchRadians;
    bool earlyMouseViewApplied = false;
    if (gameInputControlsView && presentationView.initialized) {
      const MouseAimDelta mouseAimDelta = quakeLiveMouseAimDelta(
        earlyMouseDeltaX,
        earlyMouseDeltaY,
        outerFrameElapsed.count(),
        frameMouseAimSettings
      );
      presentationView.yawRadians -= mouseAimDelta.yawRadians;
      presentationView.pitchRadians = clamp(
        presentationView.pitchRadians - mouseAimDelta.pitchRadians,
        -kMaxPitchRadians,
        kMaxPitchRadians
      );
      earlyMouseViewApplied = true;
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }
    previousFrameUsedPresentationView = usePresentationView;
    const MovementTuning currentMovementTuning = movementTuningFromCvars(console);
    const float currentPlayerSizeScaleXY =
      console.getFloat("g_playersize_xy");
    const float currentPlayerSizeScaleZ =
      console.getFloat("g_playersize_z");
    const float currentLightningKnockback =
      console.getFloat("g_lg_knockback");
    const float currentLightningFireHz =
      console.getFloat("g_lg_fire_hz");
    const float currentRocketKnockback =
      console.getFloat("g_rl_knockback");
    const std::int32_t currentKnockbackTimeMs =
      knockbackTimeMsFromCvars(console);
    const WeaponDamageTuning currentWeaponDamage =
      weaponDamageTuningFromCvars(console);
    const float currentVampirism =
      console.getFloat("g_vampirism");
    const std::uint8_t currentSelfDamagePercent =
      selfDamagePercent(console);
    const std::int32_t currentHealthAmount =
      healthAmountFromCvars(console);
    WeaponAmmoConfig currentWeaponAmmo = lastRequestedWeaponAmmo;
    currentWeaponAmmo.infiniteAmmo = infiniteAmmoFromCvars(console);
    const WeaponSwitchingMode currentWeaponSwitchingMode =
      weaponSwitchingModeFromCvars(console);
    if (!sameRuntimeMovementTuning(
          currentMovementTuning,
          lastRequestedMovementTuning
        ) ||
        currentPlayerSizeScaleXY != lastRequestedPlayerSizeScaleXY ||
        currentPlayerSizeScaleZ != lastRequestedPlayerSizeScaleZ ||
        currentLightningKnockback != lastRequestedLightningKnockback ||
        currentLightningFireHz != lastRequestedLightningFireHz ||
        currentVampirism != lastRequestedVampirism ||
        currentRocketKnockback != lastRequestedRocketKnockback ||
        currentKnockbackTimeMs != lastRequestedKnockbackTimeMs ||
        currentWeaponDamage.shotgunDamagePerPellet !=
          lastRequestedWeaponDamage.shotgunDamagePerPellet ||
        currentWeaponDamage.machineGunDamage !=
          lastRequestedWeaponDamage.machineGunDamage ||
        currentWeaponDamage.lightningGunDamage !=
          lastRequestedWeaponDamage.lightningGunDamage ||
        currentWeaponDamage.freezeGunDamage !=
          lastRequestedWeaponDamage.freezeGunDamage ||
        currentWeaponDamage.railgunDamage !=
          lastRequestedWeaponDamage.railgunDamage ||
        currentWeaponDamage.rocketLauncherDamage !=
          lastRequestedWeaponDamage.rocketLauncherDamage ||
        currentWeaponDamage.plasmaGunDamage !=
          lastRequestedWeaponDamage.plasmaGunDamage ||
        currentSelfDamagePercent != lastRequestedSelfDamagePercent ||
        currentHealthAmount != lastRequestedHealthAmount ||
        currentWeaponAmmo.infiniteAmmo != lastRequestedWeaponAmmo.infiniteAmmo ||
        currentWeaponSwitchingMode != lastRequestedWeaponSwitchingMode ||
        botDodgeEnabled != lastRequestedBotDodgeEnabled ||
        botDodgeMinIntervalMs != lastRequestedBotDodgeMinIntervalMs ||
        botDodgeMaxIntervalMs != lastRequestedBotDodgeMaxIntervalMs) {
      lastRequestedMovementTuning = currentMovementTuning;
      lastRequestedPlayerSizeScaleXY = currentPlayerSizeScaleXY;
      lastRequestedPlayerSizeScaleZ = currentPlayerSizeScaleZ;
      lastRequestedLightningKnockback = currentLightningKnockback;
      lastRequestedLightningFireHz = currentLightningFireHz;
      lastRequestedVampirism = currentVampirism;
      lastRequestedRocketKnockback = currentRocketKnockback;
      lastRequestedKnockbackTimeMs = currentKnockbackTimeMs;
      lastRequestedWeaponDamage = currentWeaponDamage;
      lastRequestedSelfDamagePercent = currentSelfDamagePercent;
      lastRequestedHealthAmount = currentHealthAmount;
      lastRequestedWeaponAmmo = currentWeaponAmmo;
      lastRequestedWeaponSwitchingMode = currentWeaponSwitchingMode;
      lastRequestedBotDodgeEnabled = botDodgeEnabled;
      lastRequestedBotDodgeMinIntervalMs = botDodgeMinIntervalMs;
      lastRequestedBotDodgeMaxIntervalMs = botDodgeMaxIntervalMs;
      movementTuningRequestPending = true;
    }

    const auto now = Clock::now();
    const auto elapsed = std::chrono::duration<float>(now - previousTime);
    previousTime = now;
    const float renderAnimationTimeSeconds =
      std::chrono::duration<float>(now - appStartTime).count();
    titleAccumulatorSeconds += elapsed.count();
    Weapon displayedSelectedWeapon = selectedWeapon;

    const FixedTickFrame fixedTickFrame = planFixedTicks(
      accumulatorSeconds,
      elapsed.count(),
      kFixedTickSeconds,
      kMaxSimulationTicksPerFrame
    );
    if (fixedTickFrame.droppedSeconds > 0.0F) {
      droppedSimulationSeconds += fixedTickFrame.droppedSeconds;
      ++overloadFrameCount;
    }

    bool consumedMouseForTick = false;
    for (int tick = 0; tick < fixedTickFrame.tickCount; ++tick) {
      ClientGame* client = session.game();
      if (client == nullptr || !client->hasSnapshot()) {
        break;
      }
      benchmark::TimingValues currentBenchmarkTickTiming;
      std::optional<benchmark::TimingSinkScope> benchmarkTickTimingScope;
      std::optional<benchmark::ScopedTiming> simulationTiming;
      if (benchmarkFrameTimingEnabled) {
        benchmarkTickTimingScope.emplace(
          &currentBenchmarkFrameTiming,
          &currentBenchmarkTickTiming
        );
        // The fixed-tick total is inclusive. Nested network, movement and trace
        // spans remain separately visible for attribution.
        simulationTiming.emplace(benchmark::TimingSubsystem::Simulation);
      }
      LocalInputState tickInput = input;
      bool controlInputCommand = false;
      bool controlInputRelease = false;
      const dev::PlayerInput* requestedControlInput = nullptr;
      if (activeControlOperation.has_value() &&
          activeControlOperation->queued.request.operation == dev::ControlOperation::SendInput &&
          activeControlOperation->stage == ActiveControlOperation::Stage::PlayerInput) {
        // Remote input uses the normal command path and excludes physical input
        // for this tick. The server still checks movement, fire rate, hits, and
        // damage.
        controlInputCommand = true;
        requestedControlInput = &activeControlOperation->queued.request.playerInput;
        controlInputRelease = activeControlOperation->inputTicksRemaining == 0U;
        const bool firstControlInputTick =
          activeControlOperation->inputTicksRemaining ==
          requestedControlInput->ticks;
        tickInput = {};
        if (!controlInputRelease) {
          tickInput.forward = requestedControlInput->forward > 0.0F ? 1 : 0;
          tickInput.back = requestedControlInput->forward < 0.0F ? 1 : 0;
          tickInput.right = requestedControlInput->right > 0.0F ? 1 : 0;
          tickInput.left = requestedControlInput->right < 0.0F ? 1 : 0;
          tickInput.up =
            requestedControlInput->up > 0.0F ||
            (
              requestedControlInput->jump &&
              (!requestedControlInput->jumpOneTick || firstControlInputTick)
            ) ? 1 : 0;
          tickInput.down =
            requestedControlInput->up < 0.0F ||
            (
              requestedControlInput->crouch &&
              (!requestedControlInput->crouchOneTick || firstControlInputTick)
            ) ? 1 : 0;
          tickInput.attack =
            requestedControlInput->attack &&
            (!requestedControlInput->attackOneTick || firstControlInputTick);
          tickInput.dash =
            requestedControlInput->dash &&
            (!requestedControlInput->dashOneTick || firstControlInputTick);
          tickInput.sneak =
            requestedControlInput->sneak &&
            (!requestedControlInput->sneakOneTick || firstControlInputTick);
          if (requestedControlInput->yawDegrees.has_value()) {
            presentationView.yawRadians =
              *requestedControlInput->yawDegrees * kDegreesToRadians;
            presentationView.pitchRadians =
              *requestedControlInput->pitchDegrees * kDegreesToRadians;
            presentationView.initialized = true;
          }
          if (!requestedControlInput->weapon.empty()) {
            selectedWeapon = *parseWeaponToken(requestedControlInput->weapon);
          }
        }
      }
      if (consumedMouseForTick) {
        // SDL reports one mouse delta per rendered frame. Apply it to only the
        // first catch-up command or low frame rates would multiply the turn.
        tickInput.mouseDeltaX = 0.0F;
        tickInput.mouseDeltaY = 0.0F;
      }
      const PlayerState& predictedPlayer = client->predictedPlayer();

      const MouseAimSettings mouseAimSettings =
        mouseAimSettingsFromConsole(
          console,
          zoomPressCount > 0,
          selectedWeapon == Weapon::Railgun,
          selectedWeapon == Weapon::Railgun ? sniperAdsAmount : 1.0F
        );

      UserCommand command =
        (usePresentationView || controlInputCommand) && presentationView.initialized
          ? buildCommandWithViewAngles(
              tickInput,
              commandSequence++,
              clientTick++,
              presentationView.yawRadians,
              presentationView.pitchRadians,
              selectedWeapon,
              controlInputCommand
                ? !controlInputRelease && requestedControlInput != nullptr &&
                  requestedControlInput->zoom &&
                  (
                    !requestedControlInput->zoomOneTick ||
                    activeControlOperation->inputTicksRemaining ==
                      requestedControlInput->ticks
                  )
                : zoomPressCount > 0
            )
          : buildCommand(
              tickInput,
              predictedPlayer,
              commandSequence++,
              clientTick++,
              mouseAimSettings,
              elapsed.count(),
              selectedWeapon,
              controlInputCommand
                ? !controlInputRelease && requestedControlInput != nullptr &&
                  requestedControlInput->zoom &&
                  (
                    !requestedControlInput->zoomOneTick ||
                    activeControlOperation->inputTicksRemaining ==
                      requestedControlInput->ticks
                  )
                : zoomPressCount > 0
            );
      if (controlInputCommand && requestedControlInput != nullptr && !controlInputRelease) {
        command.forwardMove = requestedControlInput->forward;
        command.rightMove = requestedControlInput->right;
        command.upMove = requestedControlInput->up;
        const bool firstControlInputTick =
          activeControlOperation->inputTicksRemaining ==
          requestedControlInput->ticks;
        command.jump =
          requestedControlInput->jump &&
          (!requestedControlInput->jumpOneTick || firstControlInputTick);
        command.crouch =
          requestedControlInput->crouch &&
          (!requestedControlInput->crouchOneTick || firstControlInputTick);
      }
      localTracerAimHistory.remember(command);
      PendingBotCommand botCommandForPacket;
      if (!pendingBotCommands.empty()) {
        botCommandForPacket = pendingBotCommands.front();
        pendingBotCommands.pop_front();
      }
      std::string playerNameForCommand = std::move(pendingPlayerName);
      const std::string configuredPlayerName = console.getString("cl_player_name");
      if (
        playerNameForCommand.empty() &&
        !configuredPlayerName.empty() &&
        configuredPlayerName != lastSentPlayerName
      ) {
        playerNameForCommand = configuredPlayerName;
      }
      const std::string sentPlayerName = playerNameForCommand;
      session.sendCommand(
        command,
        resetRequested,
        readyRequested,
        movementTuningRequestPending,
        lastRequestedMovementTuning,
        lastRequestedPlayerSizeScaleXY,
        lastRequestedPlayerSizeScaleZ,
        lastRequestedLightningKnockback,
        lastRequestedRocketKnockback,
        lastRequestedKnockbackTimeMs,
        lastRequestedVampirism,
        lastRequestedSelfDamagePercent,
        lastRequestedHealthAmount,
        lastRequestedWeaponDamage,
        lastRequestedWeaponAmmo,
        lastRequestedLightningFireHz,
        lastRequestedBotDodgeEnabled,
        lastRequestedBotDodgeMinIntervalMs,
        lastRequestedBotDodgeMaxIntervalMs,
        std::move(chatState.pendingMessage),
        std::move(playerNameForCommand),
        std::move(pendingMapName),
        console.getInt("cl_interp_mode") != 0,
        requestGameModePending,
        requestedGameMode,
        requestTeamPending,
        requestedTeam,
        lastRequestedWeaponSwitchingMode,
        botCommandForPacket.type,
        botCommandForPacket.value,
        botCommandForPacket.minIntervalMs,
        botCommandForPacket.maxIntervalMs,
        mcguffinThrowRequested,
        scoreboardPressCount > 0,
        requestSpectatorPending
      );
      if (controlInputCommand && activeControlOperation.has_value()) {
        ActiveControlOperation& control = *activeControlOperation;
        if (controlInputRelease) {
          control.inputReleaseSequence = command.sequence;
          control.inputReleaseSent = true;
          control.stage = ActiveControlOperation::Stage::WaitingForInputAck;
        } else if (control.inputTicksRemaining > 0U) {
          --control.inputTicksRemaining;
        }
      }
      if (!sentPlayerName.empty()) {
        lastSentPlayerName = sentPlayerName;
      }
      chatState.pendingMessage.clear();
      pendingPlayerName.clear();
      pendingMapName.clear();
      resetRequested = false;
      readyRequested = false;
      mcguffinThrowRequested = false;
      requestGameModePending = false;
      requestTeamPending = false;
      requestSpectatorPending = false;
      movementTuningRequestPending = false;
      session.update();
      if (ClientGame* updatedGame = session.game();
          updatedGame != nullptr &&
          updatedGame->hasSnapshot() &&
          !updatedGame->hasPendingMovementTuning()) {
        const ServerSnapshot& updatedSnapshot = updatedGame->snapshot();
        botStareEnabled = updatedSnapshot.botStareEnabled;
        botStandstillEnabled = updatedSnapshot.botStandstillEnabled;
        botAttackMode = updatedSnapshot.botAttackMode;
        botWeapon = updatedSnapshot.botWeapon;
        const MovementTuning consoleMovementTuning =
          movementTuningFromCvars(console);
        const WeaponDamageTuning consoleWeaponDamage =
          weaponDamageTuningFromCvars(console);
        const bool gameplayConsoleOutOfDate =
          !nearlySameGameplayMovementTuning(
            consoleMovementTuning,
            updatedSnapshot.movementTuning
          ) ||
          !nearlyEqualGameplayFloat(
            console.getFloat("g_playersize_xy"),
            updatedSnapshot.playerSizeScaleXY
          ) ||
          !nearlyEqualGameplayFloat(
            console.getFloat("g_playersize_z"),
            updatedSnapshot.playerSizeScaleZ
          ) ||
          !nearlyEqualGameplayFloat(
            console.getFloat("g_lg_knockback"),
            updatedSnapshot.lightningKnockback
          ) ||
          !nearlyEqualGameplayFloat(
            console.getFloat("g_lg_fire_hz"),
            updatedSnapshot.lightningFireHz
          ) ||
          !nearlyEqualGameplayFloat(
            console.getFloat("g_rl_knockback"),
            updatedSnapshot.rocketKnockback
          ) ||
          knockbackTimeMsFromCvars(console) !=
            updatedSnapshot.knockbackTimeMs ||
          !nearlyEqualGameplayFloat(
            console.getFloat("g_vampirism"),
            updatedSnapshot.vampirism
          ) ||
          selfDamagePercent(console) != updatedSnapshot.selfDamagePercent ||
          healthAmountFromCvars(console) != updatedSnapshot.healthAmount ||
          infiniteAmmoFromCvars(console) !=
            updatedSnapshot.weaponAmmo.infiniteAmmo ||
          botDodgeEnabled != updatedSnapshot.botDodgeEnabled ||
          botDodgeMinIntervalMs != updatedSnapshot.botDodgeMinIntervalMs ||
          botDodgeMaxIntervalMs != updatedSnapshot.botDodgeMaxIntervalMs ||
          weaponSwitchingModeFromCvars(console) != updatedSnapshot.weaponSwitchingMode ||
          consoleWeaponDamage.shotgunDamagePerPellet !=
            updatedSnapshot.weaponDamage.shotgunDamagePerPellet ||
          consoleWeaponDamage.machineGunDamage !=
            updatedSnapshot.weaponDamage.machineGunDamage ||
          consoleWeaponDamage.lightningGunDamage !=
            updatedSnapshot.weaponDamage.lightningGunDamage ||
          consoleWeaponDamage.freezeGunDamage !=
            updatedSnapshot.weaponDamage.freezeGunDamage ||
          consoleWeaponDamage.railgunDamage !=
            updatedSnapshot.weaponDamage.railgunDamage ||
          consoleWeaponDamage.rocketLauncherDamage !=
            updatedSnapshot.weaponDamage.rocketLauncherDamage ||
          consoleWeaponDamage.plasmaGunDamage !=
            updatedSnapshot.weaponDamage.plasmaGunDamage;
        if (gameplayConsoleOutOfDate) {
          syncGameplayCvarsFromSnapshot(console, updatedSnapshot);
          lastRequestedMovementTuning = updatedSnapshot.movementTuning;
          lastRequestedMovementTuning.maxAirSpeed =
            lastRequestedMovementTuning.maxGroundSpeed;
          lastRequestedPlayerSizeScaleXY = updatedSnapshot.playerSizeScaleXY;
          lastRequestedPlayerSizeScaleZ = updatedSnapshot.playerSizeScaleZ;
          lastRequestedLightningKnockback = updatedSnapshot.lightningKnockback;
          lastRequestedLightningFireHz = updatedSnapshot.lightningFireHz;
          lastRequestedRocketKnockback = updatedSnapshot.rocketKnockback;
          lastRequestedKnockbackTimeMs = updatedSnapshot.knockbackTimeMs;
          lastRequestedWeaponDamage = updatedSnapshot.weaponDamage;
          lastRequestedWeaponAmmo = updatedSnapshot.weaponAmmo;
          lastRequestedVampirism = updatedSnapshot.vampirism;
          lastRequestedSelfDamagePercent = updatedSnapshot.selfDamagePercent;
          lastRequestedHealthAmount = updatedSnapshot.healthAmount;
          lastRequestedWeaponSwitchingMode = updatedSnapshot.weaponSwitchingMode;
          botDodgeEnabled = updatedSnapshot.botDodgeEnabled;
          botDodgeMinIntervalMs = updatedSnapshot.botDodgeMinIntervalMs;
          botDodgeMaxIntervalMs = updatedSnapshot.botDodgeMaxIntervalMs;
          lastRequestedBotDodgeEnabled = updatedSnapshot.botDodgeEnabled;
          lastRequestedBotDodgeMinIntervalMs =
            updatedSnapshot.botDodgeMinIntervalMs;
          lastRequestedBotDodgeMaxIntervalMs =
            updatedSnapshot.botDodgeMaxIntervalMs;
          movementTuningRequestPending = false;
        }
      }
      if (benchmarkFrameTimingEnabled && activeControlOperation.has_value()) {
        simulationTiming.reset();
        benchmarkTickTimingScope.reset();
        benchmark::SimulationTickSample tickSample;
        tickSample.index = activeControlOperation->benchmarkTickSamples.size();
        tickSample.renderFrameIndex =
          activeControlOperation->benchmarkPhaseFrames;
        tickSample.elapsedSeconds = std::chrono::duration<double>(
          Clock::now() - activeControlOperation->benchmarkPhaseStart
        ).count();
        tickSample.simulationMilliseconds =
          currentBenchmarkTickTiming.milliseconds(
            benchmark::TimingSubsystem::Simulation
          );
        tickSample.networkProcessingMilliseconds =
          currentBenchmarkTickTiming.milliseconds(
            benchmark::TimingSubsystem::NetworkProcessing
          );
        tickSample.movementCollisionMilliseconds =
          currentBenchmarkTickTiming.milliseconds(
            benchmark::TimingSubsystem::MovementCollision
          );
        tickSample.tracesMilliseconds =
          currentBenchmarkTickTiming.milliseconds(
            benchmark::TimingSubsystem::Traces
          );
        activeControlOperation->benchmarkTickSamples.push_back(tickSample);
      }
      consumedMouseForTick = true;
    }
    if (consumedMouseForTick) {
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage == ActiveControlOperation::Stage::WaitingForInputAck) {
      ActiveControlOperation& active = *activeControlOperation;
      const ClientGame* game = session.game();
      if (game != nullptr && game->hasSnapshot() && game->hasAcknowledgedCommand() &&
          isSequenceAcknowledged(
            active.inputReleaseSequence, game->lastAcknowledgedCommand())) {
        const PlayerState& player = game->predictedPlayer();
        dev::JsonValue position = dev::JsonValue::arrayValue({
          dev::JsonValue::numberValue(player.position.x),
          dev::JsonValue::numberValue(player.position.y),
          dev::JsonValue::numberValue(player.position.z),
        });
        dev::JsonValue result = dev::JsonValue::objectValue();
        result.object["ticks"] =
          dev::JsonValue::numberValue(active.queued.request.playerInput.ticks);
        result.object["release_sequence"] =
          dev::JsonValue::numberValue(active.inputReleaseSequence);
        result.object["acknowledged_sequence"] =
          dev::JsonValue::numberValue(game->lastAcknowledgedCommand());
        result.object["position"] = std::move(position);
        result.object["yaw"] =
          dev::JsonValue::numberValue(player.viewYawRadians * kRadiansToDegrees);
        result.object["pitch"] =
          dev::JsonValue::numberValue(player.viewPitchRadians * kRadiansToDegrees);
        result.object["health"] = dev::JsonValue::numberValue(player.health);
        result.object["weapon"] =
          dev::JsonValue::stringValue(std::string(weaponShortName(selectedWeapon)));
        developerControl.complete(
          active.queued.token,
          dev::successResponse(active.queued.request.id, std::move(result))
        );
        activeControlOperation.reset();
      } else if (std::chrono::steady_clock::now() > active.deadline) {
        completeControlError(
          "input_ack_timeout",
          "server did not acknowledge the neutral input release within 30 seconds"
        );
      }
    } else if (activeControlOperation.has_value() &&
               activeControlOperation->stage == ActiveControlOperation::Stage::PlayerInput &&
               std::chrono::steady_clock::now() > activeControlOperation->deadline) {
      completeControlError("input_timeout", "player input did not finish within 30 seconds");
    }

    DeathCameraDecision deathCamera;
    if (const ClientGame* cameraGame = session.game();
        cameraGame != nullptr && cameraGame->hasSnapshot()) {
      const ServerSnapshot& cameraSnapshot = cameraGame->snapshot();
      const std::size_t localPlayerIndex = session.playerIndex();
      const bool dedicatedSpectator = session.spectator();
      const bool localPlayerDead = !dedicatedSpectator &&
        cameraSnapshot.players[localPlayerIndex].health <= 0;
      const bool previouslySpectating = wasTeammateSpectating;
      localDeathElapsedSeconds = localPlayerDead
        ? localDeathElapsedSeconds + elapsed.count()
        : 0.0F;
      const DeathCameraConfig deathCameraConfig = {
        console.getFloat("cl_death_spectate_threshold"),
        console.getFloat("cl_death_camera_hold"),
        console.getFloat("cl_death_desaturation"),
      };
      deathCamera = dedicatedSpectator
        ? spectatorCameraDecision(cameraSnapshot, deathSpectatorTarget)
        : deathCameraDecision(
            cameraSnapshot,
            localPlayerIndex,
            localDeathElapsedSeconds,
            deathCameraConfig,
            deathSpectatorTarget
          );
      if (deathCamera.mode == DeathCameraMode::Teammate) {
        deathSpectatorTarget = deathCamera.teammateIndex;
        const bool attackDown = input.attack > 0;
        const bool zoomDown = zoomPressCount > 0;
        int cycleDirection = pendingSpectateCycle;
        if (wasTeammateSpectating && cycleDirection == 0 &&
            attackDown && !previousSpectateAttackDown) {
          cycleDirection = 1;
        }
        if (wasTeammateSpectating && cycleDirection == 0 &&
            zoomDown && !previousSpectateZoomDown) {
          cycleDirection = -1;
        }
        if (cycleDirection != 0) {
          deathSpectatorTarget = dedicatedSpectator
            ? cycleSpectatorTarget(
                cameraSnapshot, deathSpectatorTarget, cycleDirection
              )
            : cycleDeathCameraTeammate(
                cameraSnapshot,
                localPlayerIndex,
                deathSpectatorTarget,
                cycleDirection
              );
          deathCamera = dedicatedSpectator
            ? spectatorCameraDecision(cameraSnapshot, deathSpectatorTarget)
            : deathCameraDecision(
                cameraSnapshot,
                localPlayerIndex,
                localDeathElapsedSeconds,
                deathCameraConfig,
                deathSpectatorTarget
              );
        }
        pendingSpectateCycle = 0;
        previousSpectateAttackDown = attackDown;
        previousSpectateZoomDown = zoomDown;
        wasTeammateSpectating = true;
      } else {
        wasTeammateSpectating = false;
        previousSpectateAttackDown = input.attack > 0;
        previousSpectateZoomDown = zoomPressCount > 0;
        if (!localPlayerDead && !dedicatedSpectator) {
          if (previouslySpectating) {
            // Reacquire the newly spawned authoritative facing instead of
            // carrying an unrelated pre-spectate view into the new life.
            presentationView.initialized = false;
          }
          deathSpectatorTarget.reset();
          pendingSpectateCycle = 0;
        }
      }
    }

    if (const ClientGame* weaponGame = session.game();
        weaponGame != nullptr && weaponGame->hasSnapshot()) {
      displayedSelectedWeapon = presentationSubjectWeapon(
        weaponGame->snapshot(),
        deathCamera,
        session.playerIndex(),
        session.spectator(),
        selectedWeapon
      );
    }
    if (displayedSelectedWeapon != viewWeapon) {
      previousViewWeapon = viewWeapon;
      viewWeapon = displayedSelectedWeapon;
      weaponSwitchSeconds = 0.0F;
    }
    weaponSwitchSeconds = std::min(
      kWeaponSwitchDurationSeconds,
      weaponSwitchSeconds + elapsed.count()
    );

    const ClientGame* currentAudioGame = session.game();
    if (currentAudioGame != audioGame) {
      audioGame = currentAudioGame;
      netGraphCorrectionSerials = {};
      netGraphCorrectionDistances = {};
      netGraphUnderrunSerials = {};
      netGraphHardCorrectionSerials = {};
      lastNetGraphCorrectionCount = 0;
      lastNetGraphUnderrunCount = 0;
      lastNetGraphHardCorrectionCount = 0;
      audioStateInitialized = false;
      lastAudioCountdownSecond = 0;
      lastHitSoundServerTick = 0;
      lastPlayedWeaponFires = {};
      lastPlayedWeaponFireAudioTicks = {};
      hasLastPlayedWeaponFire = {};
      lastPlayedRocketExplosions = {};
      lastPlayedRocketExplosionAudioTicks = {};
      hasLastPlayedRocketExplosion = {};
      lastPlayedFragEvents = {};
      lastPlayedFragAudioTicks = {};
      hasLastPlayedFragEvent = {};
      lastPlayedFootstepAudioSequences = {};
      lastPlayedGrenadeBounceAudioSequences = {};
      lastLocalRailFireTick = 0;
      hasLocalRailFireTick = false;
      localRailReadySoundPlayed = true;
      lastAudioPlayerHealth = {};
      previousLocalHit = false;
      hasLocalPlayerAliveState = false;
      wasLocalPlayerAlive = false;
      hasEnemyHitTime = false;
      lingeringRailBeams = {};
      revolverCylinderSteps = {};
      machineGunBarrelSpin = {};
      machineGunFiringResponse = {};
      lastMachineGunResponseFire = {};
      hasLastMachineGunResponseFire = false;
      rocketLauncherFiringResponse = {};
      freezeGunFiringResponse = {};
      freezeGunPulseSeconds = {};
      freezeGunPulseSerials = {};
      lastRocketLauncherResponseFire = {};
      hasLastRocketLauncherResponseFire = {};
      plasmaGunFiringResponse = {};
      lastPlasmaGunResponseFire = {};
      hasLastPlasmaGunResponseFire = {};
      resetKillFeedState(killFeedState);
      transientTracerStore = TransientTracerStore{};
      combatEffects.clear();
      activeTransientTracers.clear();
      activeTransientEffects.clear();
      footstepAudioStates = {};
      damageNumberState.reset();
      lastDamageNumberServerTick = 0;
      lastDamageNumberFeedbackSequences = {};
      hasLastDamageNumberFeedbackSequence = {};
      damageNumberStateInitialized = false;
      audio.resetLightningGunFire();
    }
    if (
      audioAvailable &&
      currentAudioGame != nullptr &&
      currentAudioGame->hasSnapshot() &&
      (!session.spectator() || deathCamera.mode == DeathCameraMode::Teammate)
    ) {
      const ServerSnapshot& audioSnapshot = currentAudioGame->snapshot();
      if (
        !audioStateInitialized ||
        audioSnapshot.serverTick != lastAudioServerTick
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const std::size_t audioSubjectIndex =
          deathCameraSubjectIndex(deathCamera, localPlayerIndex);
        const bool followingPlayer =
          deathCamera.mode == DeathCameraMode::Teammate;
        const std::size_t feedbackPlayerIndex = followingPlayer
          ? audioSubjectIndex : localPlayerIndex;
        // Spectator rendering and sound share one subject. Keep the predicted
        // local listener while playing, but use the followed authoritative
        // player while dead so world panning never remains at the corpse.
        const PlayerState audioListener =
          !session.spectator() && audioSubjectIndex == localPlayerIndex
          ? currentAudioGame->predictedPlayer()
          : audioSnapshot.players[audioSubjectIndex];
        const bool localPlayerAlive = !session.spectator() &&
          currentAudioGame->predictedPlayer().health > 0;
        if (
          hasLocalPlayerAliveState &&
          wasLocalPlayerAlive &&
          !localPlayerAlive
        ) {
          audio.resetLightningGunFire();
        }
        hasLocalPlayerAliveState = true;
        wasLocalPlayerAlive = localPlayerAlive;

        const DamageNumbersConfig damageConfig = damageNumbersConfig(console);
        if (audioSnapshot.serverTick != lastDamageNumberServerTick) {
          if (damageNumberStateInitialized) {
            std::uint32_t newestFeedbackSequence =
              lastDamageNumberFeedbackSequences[feedbackPlayerIndex];
            const std::uint32_t previousFeedbackSequence =
              newestFeedbackSequence;
            const bool hadFeedbackSequence =
              hasLastDamageNumberFeedbackSequence[feedbackPlayerIndex];
            bool consumedFeedback = false;
            for (
              const LocalHitFeedbackEvent& feedback :
              audioSnapshot.localHitFeedbackEvents[feedbackPlayerIndex]
            ) {
              if (
                !feedback.active ||
                feedback.damageApplied <= 0 ||
                feedback.targetPlayerIndex >= kDuelPlayerCount ||
                (
                  hadFeedbackSequence &&
                  !isSequenceNewer(feedback.sequence, previousFeedbackSequence)
                )
              ) {
                continue;
              }
              LocalDamageEvent event{
                localDamageSourceForWeapon(feedback.weapon),
                feedback.sequence,
                static_cast<std::uint8_t>(feedbackPlayerIndex),
                feedback.targetPlayerIndex,
                feedback.damageApplied,
                true,
                feedback.weapon,
              };
              event.headshot = feedback.headshot;
              event.hasTargetPosition = true;
              event.targetPosition =
                audioSnapshot.players[feedback.targetPlayerIndex].position;
              damageNumberState.addLocalDamageEvent(event, damageConfig);
              consumedFeedback = true;
              if (
                !hasLastDamageNumberFeedbackSequence[feedbackPlayerIndex] ||
                isSequenceNewer(feedback.sequence, newestFeedbackSequence)
              ) {
                newestFeedbackSequence = feedback.sequence;
              }
            }
            lastDamageNumberFeedbackSequences[feedbackPlayerIndex] =
              newestFeedbackSequence;
            hasLastDamageNumberFeedbackSequence[feedbackPlayerIndex] =
              hadFeedbackSequence || consumedFeedback;
          } else {
            bool foundFeedbackSequence = false;
            std::uint32_t newestFeedbackSequence = 0;
            for (
              const LocalHitFeedbackEvent& feedback :
              audioSnapshot.localHitFeedbackEvents[feedbackPlayerIndex]
            ) {
              if (!feedback.active) {
                continue;
              }
              if (
                !foundFeedbackSequence ||
                isSequenceNewer(feedback.sequence, newestFeedbackSequence)
              ) {
                newestFeedbackSequence = feedback.sequence;
              }
              foundFeedbackSequence = true;
            }
            if (foundFeedbackSequence) {
              lastDamageNumberFeedbackSequences[feedbackPlayerIndex] =
                newestFeedbackSequence;
              hasLastDamageNumberFeedbackSequence[feedbackPlayerIndex] = true;
            }
          }
          damageNumberStateInitialized = true;
          lastDamageNumberServerTick = audioSnapshot.serverTick;
        }

        const bool localHit =
          audioSnapshot.lightningGuns[feedbackPlayerIndex].hit;
        const float volume = console.getFloat("s_volume");
        const bool soundEnabled = console.getBool("s_enable");
        const auto soundVolume = [&console, volume](std::string_view name) {
          return volume * console.getFloat(name);
        };
        const float hitVolume = soundVolume("s_hit_volume");
        const float painVolume = soundVolume("s_pain_volume");
        const float fragVolume = soundVolume("s_frag_volume");
        const float footstepVolume = soundVolume("s_footstep_volume");
        auto playPainGruntIfDamaged = [&](std::size_t playerIndex) {
          if (!soundEnabled || !audioStateInitialized) {
            return;
          }
          const PlayerState& player = audioSnapshot.players[playerIndex];
          if (
            (
              !audioSnapshot.connectedPlayers[playerIndex] &&
              !audioSnapshot.botPlayers[playerIndex]
            ) ||
            player.health >= lastAudioPlayerHealth[playerIndex] ||
            lastAudioPlayerHealth[playerIndex] <= 0
          ) {
            return;
          }
          const SpatialAudio painAudio = playerIndex == audioSubjectIndex
            ? SpatialAudio{painVolume, 0.0F}
            : worldAudio(painVolume, player.position, audioListener);
          audio.playPainGrunt(painAudio.volume, painAudio.pan);
        };
        playPainGruntIfDamaged(localPlayerIndex);
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          if (playerIndex != localPlayerIndex) {
            playPainGruntIfDamaged(playerIndex);
          }
        }
        constexpr std::uint32_t kHitSoundIntervalTicks = 10;
        if (
          soundEnabled &&
          audioStateInitialized &&
          localHit &&
          (
            !previousLocalHit ||
            audioSnapshot.serverTick - lastHitSoundServerTick >=
              kHitSoundIntervalTicks
          )
        ) {
          audio.playHit(
            hitVolume,
            audioSnapshot.lightningGuns[feedbackPlayerIndex].damageApplied,
            audioSnapshot.lightningGuns[feedbackPlayerIndex].headshot
          );
          lastHitSoundServerTick = audioSnapshot.serverTick;
        }
        if (audioStateInitialized) {
          updateFootstepAudio(
            footstepAudioStates[audioSubjectIndex],
            audioListener,
            audioListener,
            true,
            soundEnabled ? footstepVolume : 0.0F,
            audio
          );
          if (soundEnabled) {
            for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
              if (playerIndex == audioSubjectIndex) {
                continue;
              }
              const FootstepAudioEvent& event =
                audioSnapshot.footstepAudioEvents[playerIndex];
              if (
                !event.active ||
                event.sequence == lastPlayedFootstepAudioSequences[playerIndex]
              ) {
                continue;
              }
              const SpatialAudio spatial =
                worldAudio(
                  footstepVolume,
                  event.position,
                  audioListener
                );
              if (event.jumping) {
                audio.playJump(spatial.volume, spatial.pan);
              } else if (event.landing) {
                audio.playLand(spatial.volume, spatial.pan);
              } else {
                audio.playFootstep(spatial.volume, event.sequence, spatial.pan);
              }
              lastPlayedFootstepAudioSequences[playerIndex] = event.sequence;
            }
            for (
              std::size_t eventIndex = 0;
              eventIndex < audioSnapshot.grenadeBounceAudioEvents.size();
              ++eventIndex
            ) {
              const GrenadeBounceAudioEvent& event =
                audioSnapshot.grenadeBounceAudioEvents[eventIndex];
              if (
                !event.active ||
                event.sequence == lastPlayedGrenadeBounceAudioSequences[eventIndex]
              ) {
                continue;
              }
              const SpatialAudio spatial =
                worldAudio(
                  soundVolume("s_gl_bounce_volume"),
                  event.position,
                  audioListener
                );
              audio.playGrenadeBounce(spatial.volume * 0.5F, spatial.pan);
              lastPlayedGrenadeBounceAudioSequences[eventIndex] = event.sequence;
            }
          }
        }
        if (soundEnabled && audioStateInitialized) {
          const FragEvent& localFrag =
            audioSnapshot.fragEvents[feedbackPlayerIndex];
          if (
            localFrag.active &&
            localFrag.targetPlayerIndex != feedbackPlayerIndex &&
            shouldPlaySnapshotAudioEvent(
              hasLastPlayedFragEvent[feedbackPlayerIndex],
              sameFragEvent(localFrag, lastPlayedFragEvents[feedbackPlayerIndex]),
              audioSnapshot.serverTick,
              lastPlayedFragAudioTicks[feedbackPlayerIndex],
              kTransientAudioEventTicks
            )
          ) {
            audio.playFrag(fragVolume);
            lastPlayedFragEvents[feedbackPlayerIndex] = localFrag;
            lastPlayedFragAudioTicks[feedbackPlayerIndex] = audioSnapshot.serverTick;
            hasLastPlayedFragEvent[feedbackPlayerIndex] = true;
          }

          for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {

            const WeaponFireResult& fire = audioSnapshot.weaponFires[playerIndex];
            const bool listenerWeaponEvent = playerIndex == audioSubjectIndex;
            const bool localWeaponEvent = followingPlayer
              ? listenerWeaponEvent : playerIndex == localPlayerIndex;
            if (
              fire.fired &&
              shouldPlaySnapshotAudioEvent(
                hasLastPlayedWeaponFire[playerIndex],
                sameWeaponFireEvent(fire, lastPlayedWeaponFires[playerIndex]),
                audioSnapshot.serverTick,
                lastPlayedWeaponFireAudioTicks[playerIndex],
                kTransientAudioEventTicks
              )
            ) {
              const WeaponFireAudioEvent fireAudio =
                routeWeaponFireAudioEvent(fire, localWeaponEvent);
              float weaponFireVolume = volume;
              if (
                fireAudio.cue == WeaponFireAudioCue::Railgun ||
                fireAudio.cue == WeaponFireAudioCue::Revolver
              ) {
                weaponFireVolume = soundVolume("s_rg_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::RocketLauncher) {
                weaponFireVolume = soundVolume("s_rl_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::MachineGun) {
                weaponFireVolume = soundVolume("s_mg_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::Shotgun) {
                weaponFireVolume = soundVolume("s_sg_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::GrenadeLauncher) {
                weaponFireVolume = soundVolume("s_gl_fire_volume");
              } else if (fireAudio.cue == WeaponFireAudioCue::PlasmaGun) {
                weaponFireVolume = soundVolume("s_pg_fire_volume");
              }
              const SpatialAudio weaponFireAudio = listenerWeaponEvent
                ? SpatialAudio{weaponFireVolume, 0.0F}
                : worldAudio(
                  weaponFireVolume,
                  fire.start,
                  audioListener
                );
              if (fireAudio.cue == WeaponFireAudioCue::Railgun) {
                audio.playRailFire(weaponFireAudio.volume, weaponFireAudio.pan);
                if (fireAudio.startsLocalRailCooldown) {
                  lastLocalRailFireTick = audioSnapshot.serverTick;
                  hasLocalRailFireTick = true;
                  localRailReadySoundPlayed = false;
                }
              } else if (fireAudio.cue == WeaponFireAudioCue::Revolver) {
                audio.playRevolverFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::RocketLauncher) {
                audio.playRocketFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::MachineGun) {
                audio.playMachineGunFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::Shotgun) {
                audio.playShotgunFire(weaponFireAudio.volume, weaponFireAudio.pan);
              } else if (fireAudio.cue == WeaponFireAudioCue::GrenadeLauncher) {
                audio.playGrenadeLauncherFire(
                  weaponFireAudio.volume,
                  weaponFireAudio.pan
                );
              } else if (fireAudio.cue == WeaponFireAudioCue::PlasmaGun) {
                audio.playPlasmaGunFire(weaponFireAudio.volume, weaponFireAudio.pan);
              }
              if (fireAudio.localHitConfirmDamage > 0) {
                audio.playHit(
                  hitVolume,
                  fireAudio.localHitConfirmDamage,
                  fireAudio.localHitConfirmHeadshot
                );
                lastHitSoundServerTick = audioSnapshot.serverTick;
              }
              lastPlayedWeaponFires[playerIndex] = fire;
              lastPlayedWeaponFireAudioTicks[playerIndex] = audioSnapshot.serverTick;
              hasLastPlayedWeaponFire[playerIndex] = true;
            }

            const RocketExplosionResult& explosion =
              audioSnapshot.rocketExplosions[playerIndex];
            if (
              explosion.active &&
              shouldPlaySnapshotAudioEvent(
                hasLastPlayedRocketExplosion[playerIndex],
                sameRocketExplosionEvent(
                  explosion,
                  lastPlayedRocketExplosions[playerIndex]
                ),
                audioSnapshot.serverTick,
                lastPlayedRocketExplosionAudioTicks[playerIndex],
                kTransientAudioEventTicks
              )
            ) {
              const SpatialAudio explosionAudio = worldAudio(
                  soundVolume("s_rl_explosion_volume"),
                  explosion.position,
                  audioListener
              );
              audio.playRocketExplosion(explosionAudio.volume, explosionAudio.pan);
              if (
                localWeaponEvent &&
                explosion.opponentDamageApplied > 0
              ) {
                audio.playHit(hitVolume, explosion.opponentDamageApplied);
                lastHitSoundServerTick = audioSnapshot.serverTick;
              }
              lastPlayedRocketExplosions[playerIndex] = explosion;
              lastPlayedRocketExplosionAudioTicks[playerIndex] =
                audioSnapshot.serverTick;
              hasLastPlayedRocketExplosion[playerIndex] = true;
            }
          }

          if (
            hasLocalRailFireTick &&
            !localRailReadySoundPlayed &&
            displayedSelectedWeapon == Weapon::Railgun &&
            audioSnapshot.serverTick - lastLocalRailFireTick >=
              kClientRailgunCooldownTicks
          ) {
            audio.playRailReady(soundVolume("s_rg_ready_volume"));
            localRailReadySoundPlayed = true;
          }
        }
        if (
          !session.spectator() &&
          soundEnabled &&
          audioStateInitialized &&
          audioSnapshot.matchPhase != lastAudioMatchPhase &&
          (
            audioSnapshot.matchPhase == MatchPhase::RoundEnd ||
            audioSnapshot.matchPhase == MatchPhase::MatchEnd
          )
        ) {
          const bool won = localPlayerWonResult(
            audioSnapshot,
            localPlayerIndex,
            audioSnapshot.matchPhase == MatchPhase::MatchEnd
          );
          audio.playRoundResult(
            won,
            soundVolume(won ? "s_round_win_volume" : "s_round_loss_volume")
          );
        }
        const std::uint32_t countdownSecond =
          audioSnapshot.matchPhase == MatchPhase::Countdown
          ? (audioSnapshot.phaseTicksRemaining + 124U) / 125U
          : 0U;
        if (
          soundEnabled &&
          audioStateInitialized &&
          countdownSecond > 0U &&
          countdownSecond != lastAudioCountdownSecond
        ) {
          audio.playCountdown(countdownSecond, soundVolume("s_countdown_volume"));
        }
        lastAudioCountdownSecond = countdownSecond;
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          lastAudioPlayerHealth[playerIndex] = audioSnapshot.players[playerIndex].health;
        }
        previousLocalHit = localHit;
        lastAudioServerTick = audioSnapshot.serverTick;
        lastAudioMatchPhase = audioSnapshot.matchPhase;
        audioStateInitialized = true;
      }
    }
    if (audioAvailable) {
      float lightningGunVolume = 0.0F;
      float lightningGunPan = 0.0F;
      if (
        console.getBool("s_enable") &&
        currentAudioGame != nullptr &&
        currentAudioGame->hasSnapshot() &&
        (!session.spectator() || deathCamera.mode == DeathCameraMode::Teammate)
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const ServerSnapshot& snapshot = currentAudioGame->snapshot();
        const std::size_t audioSubjectIndex =
          deathCameraSubjectIndex(deathCamera, localPlayerIndex);
        const PlayerState audioListener =
          !session.spectator() && audioSubjectIndex == localPlayerIndex
          ? currentAudioGame->predictedPlayer()
          : snapshot.players[audioSubjectIndex];
        const float masterVolume =
          console.getFloat("s_volume") * console.getFloat("s_lg_fire_volume");
        if (audioListener.health > 0 &&
            snapshot.lightningGuns[audioSubjectIndex].active) {
          lightningGunVolume = masterVolume;
          lightningGunPan = 0.0F;
        }
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          if (
            playerIndex == audioSubjectIndex ||
            !snapshot.lightningGuns[playerIndex].active ||
            snapshot.players[playerIndex].health <= 0
          ) {
            continue;
          }
          const SpatialAudio spatial = worldAudio(
              masterVolume,
              snapshot.players[playerIndex].position,
              audioListener
          );
          if (spatial.volume > lightningGunVolume) {
            lightningGunVolume = spatial.volume;
            lightningGunPan = spatial.pan;
          }
        }
      }
      audio.setLightningGunFire(
        lightningGunVolume > 0.0F,
        lightningGunVolume,
        lightningGunPan
      );
      audio.update();
    }
    if (ClientGame* interpolationGame = session.game();
        interpolationGame != nullptr && interpolationGame->hasSnapshot()) {
      benchmark::ScopedTiming interpolationTiming(
        benchmark::TimingSubsystem::Interpolation
      );
      interpolationGame->advanceInterpolation(
        elapsed.count(),
        console.getFloat("cl_interp"),
        console.getBool("cl_interp_adaptive"),
        console.getFloat("cl_interp_min"),
        console.getFloat("cl_interp_max"),
        console.getFloat("cl_interp_extrapolate")
      );
    }

    ++renderedFrameCount;
    if (titleAccumulatorSeconds >= 0.1F) {
      displayedFramesPerSecond =
        static_cast<float>(renderedFrameCount) / titleAccumulatorSeconds;
      renderedFrameCount = 0;
      char title[512];
      char fpsText[256] = {};
      if (console.getBool("cl_showfps_titlebar")) {
        const float frameMilliseconds = displayedFramesPerSecond > 0.0F
          ? 1000.0F / displayedFramesPerSecond
          : 0.0F;
        const RendererFrameDiagnostics& renderDiagnostics =
          renderer.lastFrameDiagnostics();
        char rendererTimingText[96] = {};
        if (console.getBool("cl_show_frame_stats")) {
          std::snprintf(
            rendererTimingText,
            sizeof(rendererTimingText),
            " render %.2f/%.2f/%.2f/%.2f",
            renderDiagnostics.swapchainAcquireMilliseconds,
            renderDiagnostics.renderBuildUploadMilliseconds,
            renderDiagnostics.submitMilliseconds,
            renderDiagnostics.totalRenderMilliseconds
          );
        }
        const std::string_view presentMode =
          renderDiagnostics.selectedPresentModeName;
        std::snprintf(
          fpsText,
          sizeof(fpsText),
          " | %.0f FPS %.2f ms %s delay %d lrp %d frame %.2f/%.2f/%.2f/%.2f/%.2f mode %.*s%s",
          displayedFramesPerSecond,
          frameMilliseconds,
          std::string(renderer.backendName()).c_str(),
          console.getBool("cl_legacy_frame_delay") ? 1 : 0,
          console.getBool("cl_local_render_prediction") ? 1 : 0,
          displayedFrameTimes.averageMilliseconds,
          displayedFrameTimes.p50Milliseconds,
          displayedFrameTimes.p95Milliseconds,
          displayedFrameTimes.p99Milliseconds,
          displayedFrameTimes.maxMilliseconds,
          static_cast<int>(presentMode.size()),
          presentMode.data(),
          rendererTimingText
        );
      }
      const ClientGame* titleClient = session.game();
      const std::optional<std::size_t> titleSubjectPlayerIndex =
        presentationSubjectIndex(
          deathCamera,
          session.playerIndex(),
          session.spectator()
        );
      if (
        console.getBool("cl_show_net") &&
        titleClient != nullptr &&
        titleClient->hasSnapshot() &&
        titleSubjectPlayerIndex.has_value()
      ) {
        const std::size_t localPlayerIndex = *titleSubjectPlayerIndex;
        const ServerSnapshot& snapshot = titleClient->snapshot();
        const LightningGunResult& lightningGun =
          snapshot.lightningGuns[localPlayerIndex];
        const PredictionDiagnostics& prediction =
          titleClient->predictionDiagnostics();
        std::snprintf(
          title,
          sizeof(title),
          "%s%s | P%zu | ping %.1f ms | tick %u | cmd %u/%u | rewind %u/%u%s | pending %zu | corrections %u %.4f | overload %u %.3fs | phase %s",
          name().data(),
          fpsText,
          localPlayerIndex + 1,
          session.pingMilliseconds(),
          snapshot.serverTick,
          commandSequence == 0 ? 0 : commandSequence - 1,
          titleClient->lastAcknowledgedCommand(),
          lightningGun.requestedRewindTicks,
          lightningGun.appliedRewindTicks,
          lightningGun.rewindClamped ? " CLAMP" : "",
          prediction.pendingCommandCount,
          prediction.correctionCount,
          prediction.lastCorrectionDistance,
          overloadFrameCount,
          droppedSimulationSeconds,
          matchPhaseName(snapshot.matchPhase).c_str()
        );
      } else {
        std::snprintf(
          title,
          sizeof(title),
          "%s%s",
          name().data(),
          fpsText
        );
      }
      SDL_SetWindowTitle(window, title);
      titleAccumulatorSeconds = 0.0F;
    }

    const float interpolationAlpha = clamp(
      accumulatorSeconds / kFixedTickSeconds,
      0.0F,
      1.0F
    );
    const bool bufferedInterpolation = console.getInt("cl_interp_mode") != 0;
    const bool localRenderPredictionEnabled =
      console.getBool("cl_local_render_prediction");
    const float localRenderPredictionSeconds =
      localRenderPredictionEnabled
        ? clamp(accumulatorSeconds, 0.0F, kFixedTickSeconds)
        : 0.0F;
    PlayerState renderPlayer;
    LightningGunResult renderLocalLightningGun;
    std::array<RemotePlayerView, kDuelPlayerCount> renderRemotePlayers = {};
    std::array<WeaponFireResult, kDuelPlayerCount> renderWeaponFires = {};
    std::array<RocketExplosionResult, kDuelPlayerCount> renderRocketExplosions = {};
    std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> renderRockets = {};
    IcePoolArray renderIcePools = {};
    std::array<bool, kDuelPlayerCount> freezeGunPulseDue = {};
    std::size_t renderLocalPlayerIndex = 0;
    if (const ClientGame* renderClient = session.game();
        renderClient != nullptr && renderClient->hasSnapshot()) {
      const std::size_t localPlayerIndex = session.playerIndex();
      if (localPlayerIndex < kDuelPlayerCount) {
        renderLocalPlayerIndex = localPlayerIndex;
      }
      renderPlayer = renderClient->predictedPlayer();
      const ServerSnapshot& renderSnapshot = renderClient->snapshot();
      std::size_t cameraPlayerIndex = localPlayerIndex;
      if (session.spectator() && deathCamera.mode != DeathCameraMode::Teammate) {
        // A dedicated observer has no predicted body or corpse to borrow when
        // no living spectate target exists.
        renderPlayer = {};
      }
      if (deathCamera.mode == DeathCameraMode::Teammate &&
          deathCamera.teammateIndex.has_value()) {
        cameraPlayerIndex = deathCameraSubjectIndex(
          deathCamera, localPlayerIndex
        );
        renderPlayer = bufferedInterpolation
          ? renderClient->interpolatedPlayer(cameraPlayerIndex)
          : renderClient->interpolatedPlayer(cameraPlayerIndex, interpolationAlpha);
        displayedSelectedWeapon = renderSnapshot.selectedWeapons[cameraPlayerIndex];
        // Viewmodel animation, weapon effects and renderer subject must follow
        // the same body as the death/spectator camera.
        renderLocalPlayerIndex = cameraPlayerIndex;
      }
      if (
        !session.spectator() &&
        localRenderPredictionSeconds > 0.0F &&
        renderPlayer.health > 0
      ) {
        const MouseAimSettings mouseAimSettings =
          mouseAimSettingsFromConsole(
            console,
            zoomPressCount > 0,
            selectedWeapon == Weapon::Railgun,
            selectedWeapon == Weapon::Railgun ? sniperAdsAmount : 1.0F
          );
        const UserCommand visualCommand =
          usePresentationView && presentationView.initialized
            ? buildCommandWithViewAngles(
                input,
                commandSequence,
                clientTick,
                presentationView.yawRadians,
                presentationView.pitchRadians,
                selectedWeapon,
                zoomPressCount > 0
              )
            : buildCommand(
                input,
                renderPlayer,
                commandSequence,
                clientTick,
                mouseAimSettings,
                elapsed.count(),
                selectedWeapon,
                zoomPressCount > 0
              );

        PlayerState visualPlayer = renderPlayer;
        simulateMovement(
          visualPlayer,
          visualCommand,
          renderClient->arena(),
          renderClient->movementTuning(),
          renderSnapshot.icePools,
          renderSnapshot.icePoolTuning,
          localRenderPredictionSeconds
        );
        renderPlayer = visualPlayer;
      }
      for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
        if (playerIndex == cameraPlayerIndex ||
            (!session.spectator() && playerIndex == localPlayerIndex)) {
          playerPresentationStates[playerIndex] = {};
          continue;
        }
        if (!renderSnapshot.participatingPlayers[playerIndex]) {
          playerPresentationStates[playerIndex] = {};
          continue;
        }
        const bool teammate = !session.spectator() && playerPresentedAsTeammate(
          renderSnapshot, localPlayerIndex, playerIndex
        );
        renderRemotePlayers[playerIndex] = RemotePlayerView{
          bufferedInterpolation
            ? renderClient->interpolatedPlayer(playerIndex)
            : renderClient->interpolatedPlayer(playerIndex, interpolationAlpha),
          renderSnapshot.lightningGuns[playerIndex],
          renderSnapshot.selectedWeapons[playerIndex],
          0.0F,
          1.0F,
          true,
          teammate,
          renderSnapshot.playerNames[playerIndex],
          renderAnimationTimeSeconds,
        };
        PlayerPresentationConfig presentationConfig;
        presentationConfig.leanScale = teammate
          ? console.getFloat("r_teammate_lean_scale")
          : console.getFloat("r_enemy_lean_scale");
        {
          benchmark::ScopedTiming animationTiming(
            benchmark::TimingSubsystem::Animation
          );
          renderRemotePlayers[playerIndex].presentation = updatePlayerPresentation(
            playerPresentationStates[playerIndex],
            renderRemotePlayers[playerIndex].player,
            elapsed.count(),
            static_cast<std::uint32_t>(playerIndex),
            presentationConfig
          );
        }
        renderRemotePlayers[playerIndex].hasPresentation = true;
        const int currentRemoteHealth =
          renderSnapshot.players[playerIndex].health;
        if (
          hasLastRemoteHealth[playerIndex] &&
          currentRemoteHealth < lastRemoteHealth[playerIndex]
        ) {
          lastRemoteDamageTime[playerIndex] = now;
          hasLastRemoteDamageTime[playerIndex] = true;
        }
        lastRemoteHealth[playerIndex] = currentRemoteHealth;
        hasLastRemoteHealth[playerIndex] = true;
      }
      renderLocalLightningGun =
        renderSnapshot.lightningGuns[cameraPlayerIndex];
      renderWeaponFires = renderSnapshot.weaponFires;
      renderRocketExplosions = renderSnapshot.rocketExplosions;
      renderRockets = renderClient->projectiles();
      renderIcePools = renderSnapshot.icePools;
      const LocalHitFeedbackBatch hitFeedback = session.spectator()
        ? LocalHitFeedbackBatch{}
        : consumeLocalHitFeedbackEvents(
            renderSnapshot.localHitFeedbackEvents[localPlayerIndex],
            localHitFeedbackDedupe
          );
      if (hitFeedback.active) {
        lastEnemyHitTime = now;
        hasEnemyHitTime = true;
        for (std::size_t targetIndex = 0; targetIndex < kDuelPlayerCount; ++targetIndex) {
          if (hitFeedback.hitTargets[targetIndex]) {
            lastEnemyHitTimeByTarget[targetIndex] = now;
            hasEnemyHitTimeByTarget[targetIndex] = true;
          }
        }
        if (hitFeedback.lightningGunHit) {
          lastBeamHitTime = now;
          hasBeamHitTime = true;
        }
      }
    }
    if (usePresentationView && presentationView.initialized &&
        deathCamera.mode != DeathCameraMode::Teammate) {
      renderPlayer.viewYawRadians = presentationView.yawRadians;
      renderPlayer.viewPitchRadians = presentationView.pitchRadians;
    }
    std::optional<benchmark::ScopedTiming> weaponAnimationTiming;
    weaponAnimationTiming.emplace(benchmark::TimingSubsystem::Animation);
    const MachineGunBarrelSpinTuning machineGunSpinTuning = {
      console.getFloat("r_mg_barrel_max_rps"),
      console.getFloat("r_mg_barrel_spin_up"),
      console.getFloat("r_mg_barrel_spin_down"),
    };
    const bool ownsPresentedSubject =
      !session.spectator() && renderLocalPlayerIndex == session.playerIndex();
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      const bool localPlayer = playerIndex == renderLocalPlayerIndex;
      const bool authoritativeMachineGunFire =
        renderWeaponFires[playerIndex].fired &&
        renderWeaponFires[playerIndex].weapon == Weapon::MachineGun;
      const bool motorDriven = localPlayer
        ? (
            ownsPresentedSubject
              ? input.attack > 0 &&
                displayedSelectedWeapon == Weapon::MachineGun &&
                renderPlayer.health > 0
              : authoritativeMachineGunFire
          )
        : renderRemotePlayers[playerIndex].visible &&
          authoritativeMachineGunFire;
      machineGunBarrelSpin[playerIndex].update(
        motorDriven,
        elapsed.count(),
        machineGunSpinTuning
      );
      renderRemotePlayers[playerIndex].machineGunBarrelRotationRadians =
        machineGunBarrelSpin[playerIndex].angleRadians;
    }
    const WeaponFireResult& localPresentationFire =
      renderWeaponFires[renderLocalPlayerIndex];
    if (
      localPresentationFire.fired &&
      localPresentationFire.weapon == Weapon::MachineGun &&
      (
        !hasLastMachineGunResponseFire ||
        !sameWeaponFireEvent(
          localPresentationFire,
          lastMachineGunResponseFire
        )
      )
    ) {
      machineGunFiringResponse.triggerShot(localPresentationFire.visualSeed);
      lastMachineGunResponseFire = localPresentationFire;
      hasLastMachineGunResponseFire = true;
    }
    machineGunFiringResponse.update(
      elapsed.count(),
      machineGunBarrelSpin[renderLocalPlayerIndex].normalizedSpeed(
        machineGunSpinTuning
      )
    );
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      const WeaponFireResult& fire = renderWeaponFires[playerIndex];
      if (
        fire.fired &&
        fire.weapon == Weapon::RocketLauncher &&
        (
          !hasLastRocketLauncherResponseFire[playerIndex] ||
          !sameWeaponFireEvent(fire, lastRocketLauncherResponseFire[playerIndex])
        )
      ) {
        rocketLauncherFiringResponse[playerIndex].triggerShot();
        lastRocketLauncherResponseFire[playerIndex] = fire;
        hasLastRocketLauncherResponseFire[playerIndex] = true;
      }
      rocketLauncherFiringResponse[playerIndex].update(elapsed.count());
      renderRemotePlayers[playerIndex].rocketLauncherMechanicalAmount =
        rocketLauncherFiringResponse[playerIndex].mechanicalAmount();

      if (
        fire.fired &&
        fire.weapon == Weapon::PlasmaGun &&
        (
          !hasLastPlasmaGunResponseFire[playerIndex] ||
          !sameWeaponFireEvent(fire, lastPlasmaGunResponseFire[playerIndex])
        )
      ) {
        plasmaGunFiringResponse[playerIndex].triggerShot();
        lastPlasmaGunResponseFire[playerIndex] = fire;
        hasLastPlasmaGunResponseFire[playerIndex] = true;
      }
      plasmaGunFiringResponse[playerIndex].update(elapsed.count());
      renderRemotePlayers[playerIndex].plasmaGunContainmentAmount =
        plasmaGunFiringResponse[playerIndex].containmentAmount();

      const bool localPlayer = playerIndex == renderLocalPlayerIndex;
      const bool freezeDriven = localPlayer
        ? (
            displayedSelectedWeapon == Weapon::FreezeGun &&
            renderLocalLightningGun.active
          )
        : (
            renderRemotePlayers[playerIndex].visible &&
            renderRemotePlayers[playerIndex].selectedWeapon == Weapon::FreezeGun &&
            renderRemotePlayers[playerIndex].lightningGun.active
          );
      if (freezeDriven) {
        const CombatEffectPulseTimerAdvance advance =
          advanceCombatEffectPulseTimer(
            freezeGunPulseSeconds[playerIndex],
            elapsed.count(),
            0.10F
          );
        freezeGunPulseDue[playerIndex] = advance.pulseDue;
        freezeGunPulseSeconds[playerIndex] = advance.remainingSeconds;
      } else {
        freezeGunPulseSeconds[playerIndex] = 0.0F;
      }
      freezeGunFiringResponse[playerIndex].update(freezeDriven, elapsed.count());
      if (!localPlayer) {
        renderRemotePlayers[playerIndex].freezeGunFiringAmount =
          freezeGunFiringResponse[playerIndex].amount;
        renderRemotePlayers[playerIndex].freezeGunActivationFlashAmount =
          freezeGunFiringResponse[playerIndex].activationFlashAmount();
        renderRemotePlayers[playerIndex].freezeGunCoolantPulse =
          freezeGunFiringResponse[playerIndex].coolantPulse();
        renderRemotePlayers[playerIndex].freezeGunVibrationPhaseRadians =
          freezeGunFiringResponse[playerIndex].phaseRadians;
      }
    }
    RenderSettings currentRenderSettings = renderSettings(
      console,
      renderer.backendName() == "SDL_GPU/vulkan"
    );
    currentRenderSettings.benchmarkTimingEnabled = benchmarkFrameTimingEnabled;
    currentRenderSettings.benchmarkGpuFrameIndex = benchmarkFrameTimingEnabled
      ? std::optional<std::uint64_t>(
          activeControlOperation->benchmarkPhaseFrames
        )
      : std::nullopt;
    currentRenderSettings.mapRevision = currentMapRevision();
    if (developmentCameraEnabled) {
      // The development camera replaces presentation state only. The client
      // continues to send ordinary commands and the server remains
      // authoritative.
      renderPlayer = {};
      renderPlayer.position =
        developmentCamera.position - Vec3{0.0F, 0.0F, 0.65F};
      renderPlayer.viewYawRadians = developmentCamera.yawDegrees * kDegreesToRadians;
      renderPlayer.viewPitchRadians = developmentCamera.pitchDegrees * kDegreesToRadians;
      renderPlayer.health = 100;
      currentRenderSettings.fieldOfView = developmentCamera.fieldOfView.value_or(
        currentRenderSettings.fieldOfView
      );
      currentRenderSettings.showOwnWeapons = false;
    }
    const Vec3 localViewVelocity = {
      dot(renderPlayer.velocity, yawForward(renderPlayer.viewYawRadians)),
      dot(renderPlayer.velocity, yawRight(renderPlayer.viewYawRadians)),
      renderPlayer.velocity.z,
    };
    currentRenderSettings.viewModelPresentation = viewModelPresentation.update(
      {
        localViewVelocity,
        viewModelMouseDeltaX,
        viewModelMouseDeltaY,
        renderPlayer.onGround || renderPlayer.movementMode == MovementMode::Grounded,
        elapsed.count(),
      },
      {
        console.getFloat("cl_viewmodel_motion_scale"),
        console.getFloat("cl_viewmodel_bob_scale"),
        console.getFloat("cl_viewmodel_sway_scale"),
        console.getFloat("cl_viewmodel_inertia_scale"),
        console.getFloat("cl_viewmodel_landing_scale"),
        console.getFloat("cl_camera_position_response"),
      }
    );
    weaponAnimationTiming.reset();
    if (deathCamera.mode != DeathCameraMode::Alive) {
      currentRenderSettings.crosshairEnabled = false;
      currentRenderSettings.showOwnWeapons =
        deathCamera.mode == DeathCameraMode::Teammate &&
        console.getBool("r_show_weapons");
    }
    currentRenderSettings.localSelectedWeapon = displayedSelectedWeapon;
    currentRenderSettings.localPlayerIndex =
      static_cast<std::uint8_t>(renderLocalPlayerIndex);
    currentRenderSettings.revolverCylinderRotationRadians =
      static_cast<float>(revolverCylinderSteps[renderLocalPlayerIndex]) *
      (kTwoPi / 6.0F);
    currentRenderSettings.machineGunBarrelRotationRadians =
      machineGunBarrelSpin[renderLocalPlayerIndex].angleRadians;
    currentRenderSettings.machineGunRecoilAmount =
      machineGunFiringResponse.kickAmount();
    currentRenderSettings.machineGunVibrationAmount =
      machineGunFiringResponse.vibrationAmount(
        machineGunBarrelSpin[renderLocalPlayerIndex].normalizedSpeed(
          machineGunSpinTuning
        )
      );
    currentRenderSettings.machineGunVibrationPhaseRadians =
      machineGunFiringResponse.vibrationPhaseRadians;
    currentRenderSettings.rocketLauncherMechanicalAmount =
      rocketLauncherFiringResponse[renderLocalPlayerIndex].mechanicalAmount();
    currentRenderSettings.rocketLauncherRecoilAmount =
      rocketLauncherFiringResponse[renderLocalPlayerIndex].wholeWeaponRecoilAmount();
    currentRenderSettings.freezeGunFiringAmount =
      freezeGunFiringResponse[renderLocalPlayerIndex].amount;
    currentRenderSettings.freezeGunActivationFlashAmount =
      freezeGunFiringResponse[renderLocalPlayerIndex].activationFlashAmount();
    currentRenderSettings.freezeGunCoolantPulse =
      freezeGunFiringResponse[renderLocalPlayerIndex].coolantPulse();
    currentRenderSettings.freezeGunVibrationPhaseRadians =
      freezeGunFiringResponse[renderLocalPlayerIndex].phaseRadians;
    currentRenderSettings.plasmaGunContainmentAmount =
      plasmaGunFiringResponse[renderLocalPlayerIndex].containmentAmount();
    currentRenderSettings.hasRemotePlayer = std::any_of(
      renderRemotePlayers.begin(),
      renderRemotePlayers.end(),
      [](const RemotePlayerView& remote) { return remote.visible; }
    );
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      WeaponFireResult& currentFire = renderWeaponFires[playerIndex];
      LingeringWeaponFire& lingeringRailBeam = lingeringRailBeams[playerIndex];
      if (
        currentFire.fired &&
        (currentFire.weapon == Weapon::Railgun || currentFire.weapon == Weapon::Revolver)
      ) {
        const WeaponFireResult sourceFire = currentFire;
        const bool localPerspectiveRail =
          playerIndex == renderLocalPlayerIndex;
        // Server snapshots retain a fire event longer than the compact smoke
        // cue. Event identity, not current visibility, decides whether to
        // restart it after the cue expires.
        const bool newRailEvent =
          !sameWeaponFireEvent(sourceFire, lingeringRailBeam.sourceFire);
        if (newRailEvent) {
          if (localPerspectiveRail) {
            if (!currentRenderSettings.showOwnWeapons) {
              currentFire.start = hiddenWeaponVisualOrigin(renderPlayer);
            } else if (currentFire.weapon == Weapon::Railgun) {
              currentFire.start = firstPersonSniperRifleMuzzlePosition(
                renderPlayer,
                currentRenderSettings
              );
            } else {
              currentFire.start = firstPersonRevolverMuzzlePosition(
                renderPlayer,
                currentRenderSettings
              );
            }
          } else {
            currentFire.start =
              currentFire.weapon == Weapon::Railgun &&
                playerIndex < renderRemotePlayers.size() &&
                renderRemotePlayers[playerIndex].visible
              ? remoteSniperRifleMuzzlePosition(
                  renderRemotePlayers[playerIndex],
                  currentRenderSettings
                )
              : revolverMuzzleSource(
                  sourceFire,
                  renderPlayer,
                  renderRemotePlayers,
                  playerIndex,
                  currentRenderSettings
                );
          }
          if (sourceFire.weapon == Weapon::Revolver) {
            revolverCylinderSteps[playerIndex] = static_cast<std::uint8_t>(
              (revolverCylinderSteps[playerIndex] + 1U) % 6U
            );
          }
          lingeringRailBeam.sourceFire = sourceFire;
          lingeringRailBeam.fire = currentFire;
          lingeringRailBeam.active = true;
          lingeringRailBeam.startedAt = now;
          const float lingerSeconds = sourceFire.weapon == Weapon::Revolver
            ? kRevolverTracerLifetimeSeconds
            : kSniperSmokeTracerLifetimeSeconds;
          lingeringRailBeam.expiresAt =
            now + std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<float>(lingerSeconds)
            );
        } else if (
          lingeringRailBeam.active &&
          now < lingeringRailBeam.expiresAt
        ) {
          currentFire = lingeringRailBeam.fire;
        } else {
          // The server deliberately retains fire events for snapshot delivery.
          // Do not replay that same event after this shorter visual cue ends.
          lingeringRailBeam.active = false;
          currentFire.fired = false;
        }
      } else if (
        !currentFire.fired &&
        lingeringRailBeam.active &&
        now < lingeringRailBeam.expiresAt
      ) {
        currentFire = lingeringRailBeam.fire;
      } else if (lingeringRailBeam.active && now >= lingeringRailBeam.expiresAt) {
        lingeringRailBeam.active = false;
        }
        if (
          lingeringRailBeam.active &&
          lingeringRailBeam.fire.weapon == Weapon::Railgun &&
          now < lingeringRailBeam.expiresAt
        ) {
          // The fire start was captured from the rendered muzzle when this
          // event began. Keep it fixed while only the fade and smoke shape
          // continue to animate.
          const float ageSeconds = std::chrono::duration<float>(
            now - lingeringRailBeam.startedAt
          ).count();
          const Vec3 authoritativeTrace =
            lingeringRailBeam.sourceFire.end - lingeringRailBeam.sourceFire.start;
          const float authoritativeTraceLength = length(authoritativeTrace);
          currentRenderSettings.sniperSmokeTracerAlpha[playerIndex] =
            sniperSmokeTracerPresentation(ageSeconds).alpha;
          if (
            std::isfinite(authoritativeTraceLength) &&
            authoritativeTraceLength > 0.0001F
          ) {
            currentRenderSettings.sniperSmokeTracerDirections[playerIndex] =
              normalize(authoritativeTrace);
            currentRenderSettings.sniperSmokeTracerTraceLengths[playerIndex] =
              authoritativeTraceLength;
          }
        }
        if (
        lingeringRailBeam.active &&
        lingeringRailBeam.fire.weapon == Weapon::Revolver &&
        now < lingeringRailBeam.expiresAt
      ) {
        const float ageSeconds = std::chrono::duration<float>(
          now - lingeringRailBeam.startedAt
        ).count();
        const RevolverTracerPresentation tracerPresentation =
          revolverTracerPresentation(ageSeconds);
        currentRenderSettings.revolverTracerAlpha[playerIndex] =
          tracerPresentation.alpha;
      }
      if (
        playerIndex == renderLocalPlayerIndex &&
        lingeringRailBeam.active &&
        lingeringRailBeam.fire.weapon == Weapon::Revolver &&
        now < lingeringRailBeam.expiresAt
      ) {
        const float animationProgress = std::clamp(
          std::chrono::duration<float>(now - lingeringRailBeam.startedAt).count() /
            kRevolverTracerLifetimeSeconds,
          0.0F,
          1.0F
        );
        currentRenderSettings.revolverRecoilAmount =
          std::sin(animationProgress * 3.14159265359F);
        const float indexT = std::clamp(animationProgress * 4.0F, 0.0F, 1.0F);
        const float smoothIndexT = indexT * indexT * (3.0F - 2.0F * indexT);
        const float completedSteps = static_cast<float>(
          (revolverCylinderSteps[playerIndex] + 5U) % 6U
        );
        currentRenderSettings.revolverCylinderRotationRadians =
          (completedSteps + smoothIndexT) * (kTwoPi / 6.0F);
      }
    }
    const bool sniperAdsRequested =
      zoomPressCount > 0 &&
      displayedSelectedWeapon == Weapon::Railgun &&
      deathCamera.mode == DeathCameraMode::Alive &&
      renderPlayer.health > 0;
    const float sniperAdsStep =
      static_cast<float>(outerFrameElapsed.count()) / kSniperAdsSeconds;
    sniperAdsAmount = std::clamp(
      sniperAdsAmount + (sniperAdsRequested ? sniperAdsStep : -sniperAdsStep),
      0.0F,
      1.0F
    );
    const bool sniperScopeActive = sniperAdsAmount > 0.001F;
    if (
      sniperScopeActive ||
      (zoomPressCount > 0 && deathCamera.mode != DeathCameraMode::Teammate)
    ) {
      currentRenderSettings.fieldOfView = resolvedZoomFieldOfView(
        currentRenderSettings.fieldOfView,
        console.getFloat("cl_zoom_fov"),
        console.getFloat("cl_zoom_sniper_fov"),
        zoomPressCount > 0,
        sniperScopeActive,
        sniperAdsAmount
      );
    }
    if (sniperScopeActive) {
      // The scope owns the center view while ADS is held; hiding the viewmodel
      // keeps the rifle from drawing over its lens and charge readout.
      currentRenderSettings.showOwnWeapons = false;
      currentRenderSettings.crosshairEnabled = false;
    }
    constexpr float kBeamPulseRadiansPerSecond = 31.4159265359F;
    const double presentationSeconds =
      std::chrono::duration<double>(now.time_since_epoch()).count();
    currentRenderSettings.presentationTimeSeconds = presentationSeconds;
    currentRenderSettings.beamPhaseRadians =
      static_cast<float>(std::fmod(presentationSeconds, 1.0)) *
      kBeamPulseRadiansPerSecond;
    currentRenderSettings.beamPulse =
      std::sin(currentRenderSettings.beamPhaseRadians);
    const float elapsedSinceHit = hasEnemyHitTime
      ? std::chrono::duration<float>(now - lastEnemyHitTime).count()
      : 0.0F;
    const auto hitFeedbackAmount =
      [&](float duration, bool fade) {
        if (!hasEnemyHitTime || duration <= 0.0F || elapsedSinceHit >= duration) {
          return 0.0F;
        }
        return fade ? 1.0F - (elapsedSinceHit / duration) : 1.0F;
      };
    const auto beamHitFeedbackAmount =
      [&](float duration, bool fade) {
        if (!hasBeamHitTime || duration <= 0.0F) {
          return 0.0F;
        }
        const float elapsedSinceBeamHit =
          std::chrono::duration<float>(now - lastBeamHitTime).count();
        if (elapsedSinceBeamHit >= duration) {
          return 0.0F;
        }
        return fade ? 1.0F - (elapsedSinceBeamHit / duration) : 1.0F;
      };
    currentRenderSettings.enemyHitAmount = 0.0F;
    if (console.getBool("r_enemy_hit")) {
      for (
        std::size_t playerIndex = 0;
        playerIndex < renderRemotePlayers.size();
        ++playerIndex
      ) {
        RemotePlayerView& hitRemote = renderRemotePlayers[playerIndex];
        if (
          hitRemote.teammate ||
          !hasEnemyHitTimeByTarget[playerIndex]
        ) {
          continue;
        }
        const float elapsedSinceTargetHit =
          std::chrono::duration<float>(
            now - lastEnemyHitTimeByTarget[playerIndex]
          ).count();
        const float duration = console.getFloat("r_enemy_hit_duration");
        if (duration <= 0.0F || elapsedSinceTargetHit >= duration) {
          hitRemote.enemyHitAmount = 0.0F;
          continue;
        }
        hitRemote.enemyHitAmount =
          console.getBool("r_enemy_hit_fade")
            ? 1.0F - (elapsedSinceTargetHit / duration)
            : 1.0F;
      }
    }
    if (console.getBool("r_beam_hit")) {
      currentRenderSettings.beamHitAmount = beamHitFeedbackAmount(
        console.getFloat("r_beam_hit_duration"),
        console.getBool("r_beam_hit_fade")
      );
    }
    if (console.getBool("crosshair_hit")) {
      currentRenderSettings.crosshairHitAmount = hitFeedbackAmount(
        console.getFloat("crosshair_hit_duration"),
        console.getBool("crosshair_hit_fade")
      );
    }
    if (currentRenderSettings.hitMarkerEnabled) {
      currentRenderSettings.hitMarkerAmount = hitFeedbackAmount(
        console.getFloat("r_hitmarker_duration"),
        true
      );
    }
    damageNumberState.update(
      outerFrameElapsed.count(),
      damageNumbersConfig(console)
    );
    updateKillFeedState(killFeedState, outerFrameElapsed.count());
    if (session.game() != nullptr && session.game()->hasSnapshot()) {
      consumeKillFeedEvents(killFeedState, session.game()->snapshot());
    }
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      RemotePlayerView& remote = renderRemotePlayers[playerIndex];
      if (!remote.visible) {
        continue;
      }
      const bool damageOnly = remote.teammate
        ? currentRenderSettings.teammateHealthBarDamageOnly
        : currentRenderSettings.enemyHealthBarDamageOnly;
      if (!damageOnly) {
        remote.enemyHealthAlpha = 1.0F;
        continue;
      }
      const float duration = remote.teammate
        ? currentRenderSettings.teammateHealthBarVisibleDuration
        : currentRenderSettings.enemyHealthBarVisibleDuration;
      if (!hasLastRemoteDamageTime[playerIndex] || duration <= 0.0F) {
        remote.enemyHealthAlpha = 0.0F;
        continue;
      }
      const float elapsed =
        std::chrono::duration<float>(now - lastRemoteDamageTime[playerIndex]).count();
      if (elapsed >= duration) {
        remote.enemyHealthAlpha = 0.0F;
        continue;
      }
      const bool fade = remote.teammate
        ? currentRenderSettings.teammateHealthBarFade
        : currentRenderSettings.enemyHealthBarFade;
      remote.enemyHealthAlpha = fade ? 1.0F - (elapsed / duration) : 1.0F;
    }
    currentRenderSettings.playerSizePixels =
      14.0F * (renderPlayer.bounds.radius / 0.35F);

    const Arena& renderArena =
      session.game() != nullptr && session.game()->hasSnapshot()
        ? session.game()->arena()
        : fallbackArena;
    std::array<bool, Arena::kHealthPickupCount> renderHealthPickupAvailable = {};
    renderHealthPickupAvailable.fill(true);
    if (session.game() != nullptr && session.game()->hasSnapshot()) {
      renderHealthPickupAvailable =
        session.game()->snapshot().healthPickupAvailable;
    }
    std::optional<std::size_t> hudSubjectPlayerIndex;
    if (session.game() != nullptr && session.game()->hasSnapshot()) {
      hudSubjectPlayerIndex = presentationSubjectIndex(
        deathCamera,
        session.playerIndex(),
        session.spectator()
      );
    }
    HudRenderState hud = buildHud(
      session,
      console.getBool("cl_show_alive_counts"),
      hudSubjectPlayerIndex
    );
    hud.netGraph.mode = console.getInt("cl_netgraph");
    hud.netGraph.scale = console.getFloat("cl_netgraph_scale");
    hud.netGraph.telemetry = session.networkTelemetry();
    hud.netGraph.interpolationEffectiveDelayMilliseconds =
      console.getFloat("cl_interp") * 1000.0F;
    if (const ClientGame* netGame = session.game();
        netGame != nullptr && netGame->hasSnapshot()) {
      const SnapshotInterpolation::Diagnostics interpolation =
        netGame->interpolationDiagnostics();
      hud.netGraph.interpolationEffectiveDelayMilliseconds =
        interpolation.effectiveDelaySeconds * 1000.0F;
      hud.netGraph.interpolationBufferLeadTicks = interpolation.bufferLeadTicks;
      hud.netGraph.interpolationDesiredBufferLeadTicks =
        interpolation.desiredBufferLeadTicks;
      hud.netGraph.interpolationTimelineErrorTicks =
        interpolation.timelineErrorTicks;
      hud.netGraph.interpolationPresentationTick = interpolation.presentationTick;
      hud.netGraph.interpolationNewestSnapshotTick = interpolation.newestSnapshotTick;
      hud.netGraph.interpolationPlaybackRate = interpolation.playbackRate;
      hud.netGraph.interpolationBufferedSnapshotCount =
        interpolation.bufferedSnapshotCount;
      hud.netGraph.interpolationUnderrunCount = interpolation.underrunCount;
      hud.netGraph.interpolationHardCorrectionCount =
        interpolation.hardCorrectionCount;
      hud.netGraph.interpolationPlaybackStarted = interpolation.playbackStarted;
      hud.netGraph.interpolationUnderrun = interpolation.bufferUnderrun;
      const std::size_t samplePlayerIndex = deathCameraSubjectIndex(
        deathCamera, session.playerIndex()
      );
      const SnapshotInterpolation::PlayerCollisionSample collisionSample =
        netGame->interpolationCollisionSample(samplePlayerIndex);
      hud.netGraph.interpolationSampleTick = collisionSample.discreteServerTick;
      hud.netGraph.interpolationSampleEligible = collisionSample.eligible;
      if (hud.netGraph.telemetry.historyCount > 0) {
        const std::uint64_t serial = hud.netGraph.telemetry.history[
          hud.netGraph.telemetry.historyCount - 1U
        ].serial;
        if (interpolation.underrunCount > lastNetGraphUnderrunCount) {
          netGraphUnderrunSerials[serial % netGraphUnderrunSerials.size()] = serial;
        }
        if (interpolation.hardCorrectionCount > lastNetGraphHardCorrectionCount) {
          netGraphHardCorrectionSerials[
            serial % netGraphHardCorrectionSerials.size()
          ] = serial;
        }
      }
      lastNetGraphUnderrunCount = interpolation.underrunCount;
      lastNetGraphHardCorrectionCount = interpolation.hardCorrectionCount;
      const PredictionDiagnostics& prediction = netGame->predictionDiagnostics();
      const SnapshotDiagnostics snapshotDiagnostics = netGame->snapshotDiagnostics();
      if (
        prediction.correctionCount != lastNetGraphCorrectionCount &&
        hud.netGraph.telemetry.historyCount > 0
      ) {
        const std::uint64_t serial = hud.netGraph.telemetry.history[
          hud.netGraph.telemetry.historyCount - 1U
        ].serial;
        const std::size_t slot = serial % netGraphCorrectionSerials.size();
        netGraphCorrectionSerials[slot] = serial;
        netGraphCorrectionDistances[slot] = prediction.lastCorrectionDistance;
      }
      lastNetGraphCorrectionCount = prediction.correctionCount;
      for (std::size_t index = 0;
           index < hud.netGraph.telemetry.historyCount;
           ++index) {
        NetworkTelemetrySample& sample = hud.netGraph.telemetry.history[index];
        const std::size_t slot = sample.serial % netGraphCorrectionSerials.size();
        if (netGraphCorrectionSerials[slot] == sample.serial) {
          sample.predictionCorrectionDistance =
            netGraphCorrectionDistances[slot];
        }
        sample.interpolationUnderrun =
          netGraphUnderrunSerials[slot] == sample.serial;
        sample.interpolationHardCorrection =
          netGraphHardCorrectionSerials[slot] == sample.serial;
      }
      hud.netGraph.pendingCommands = prediction.pendingCommandCount;
      hud.netGraph.correctionCount = prediction.correctionCount;
      hud.netGraph.lastCorrectionDistance = prediction.lastCorrectionDistance;
      hud.netGraph.snapshotQueueDepth = snapshotDiagnostics.snapshotQueueDepth;
      if (!session.spectator()) {
        const LightningGunResult& lightning = netGame->snapshot().lightningGuns[
          session.playerIndex()
        ];
        hud.netGraph.requestedRewindTicks = lightning.requestedRewindTicks;
        hud.netGraph.appliedRewindTicks = lightning.appliedRewindTicks;
      }
    }
    if (deathCamera.mode != DeathCameraMode::Alive) {
      hud.deathDesaturation = deathCamera.desaturation;
      hud.centerLines.clear();
      if (deathCamera.mode == DeathCameraMode::Teammate &&
          deathCamera.teammateIndex.has_value()) {
        hud.topCenterLines.push_back(
          "SPECTATING " +
          session.game()->snapshot().playerNames[*deathCamera.teammateIndex]
        );
      }
      if (session.spectator()) {
        hud.bottomCenterLines.clear();
        hud.centerLines.clear();
        hud.deathDesaturation = 0.0F;
      }
      if (deathCamera.respawnSecondsRemaining > 0.0F) {
        std::ostringstream respawn;
        respawn.setf(std::ios::fixed);
        respawn.precision(1);
        respawn << "RESPAWNING IN " << deathCamera.respawnSecondsRemaining;
        hud.centerLines.push_back(respawn.str());
      } else if (deathCamera.mode != DeathCameraMode::Teammate) {
        hud.centerLines.push_back("WAITING FOR ROUND END");
      }
    }
    hud.selectedWeapon = displayedSelectedWeapon;
    hud.sniperScopeActive = sniperScopeActive;
    hud.sniperScopeAmount = sniperAdsAmount;
    hud.previousWeapon = previousViewWeapon;
    hud.damageNumbers = damageNumberState.presentation();
    hud.killFeedLines = killFeedPresentation(killFeedState);
    if (console.getBool("cl_showfps")) {
      hud.fpsText = std::to_string(static_cast<int>(
        std::lround(displayedFramesPerSecond)
      )) + "fps";
    }
    hud.weaponSwitchProgress = kWeaponSwitchDurationSeconds > 0.0F
      ? weaponSwitchSeconds / kWeaponSwitchDurationSeconds
      : 1.0F;
    if (
      console.getBool("cl_showspeed") &&
      session.game() != nullptr &&
      session.game()->hasSnapshot()
    ) {
      constexpr float kQuakeUnitsPerProjectUnit = 40.0F;
      const float horizontalSpeed = std::hypot(
        renderPlayer.velocity.x,
        renderPlayer.velocity.y
      );
      hud.speedText =
        std::to_string(static_cast<int>(std::lround(
          horizontalSpeed * kQuakeUnitsPerProjectUnit
        ))) + " ups";
    }
    if (
      session.game() != nullptr &&
      session.game()->hasSnapshot()
    ) {
      appendGroundDebugHudLines(
        hud,
        renderArena,
        renderPlayer,
        console.getInt("cg_ground_debug")
      );
    }
    if (currentRenderSettings.showRendererPerf) {
      const RendererFrameDiagnostics& diagnostics =
        renderer.lastFrameDiagnostics();
      hud.topLeftLines.emplace_back(
        "dynamic vertices " +
        std::to_string(diagnostics.totalUploadedVertices)
      );
      if (currentRenderSettings.showRendererPerfDetail) {
        hud.topLeftLines.emplace_back(
          "world BVH: chunks " +
          std::to_string(diagnostics.worldVisibleChunks) + "/" +
          std::to_string(diagnostics.worldTotalChunks) +
          " | culled " +
          std::to_string(diagnostics.worldCulledChunks) +
          " | nodes " +
          std::to_string(diagnostics.worldVisibilityTestedNodes) +
          " | tris " +
          std::to_string(diagnostics.worldSubmittedTriangles) + "/" +
          std::to_string(diagnostics.worldRenderedTriangles) +
          " | ranges " +
          std::to_string(diagnostics.worldSubmittedRanges)
        );
        hud.topLeftLines.emplace_back(
          "culling: candidates " +
          std::to_string(diagnostics.remoteCandidates) +
          " | frustum visible " +
          std::to_string(diagnostics.remoteFrustumVisible) +
          " | culled " +
          std::to_string(diagnostics.remoteFrustumCulled)
        );
        hud.topLeftLines.emplace_back(
          "remote geometry: bodies " +
          std::to_string(diagnostics.remoteBodyModelsBuilt) +
          " | weapons " +
          std::to_string(diagnostics.remoteWeaponModelsBuilt) +
          " | outlines " +
          std::to_string(diagnostics.playerOutlinesBuilt)
        );
        hud.topLeftLines.emplace_back(
          "procedural box players: visible " +
          std::to_string(diagnostics.visibleProceduralBoxPlayers) +
          " | culled " +
          std::to_string(diagnostics.culledProceduralBoxPlayers) +
          " | instances " +
          std::to_string(diagnostics.playerBoxInstancesSubmitted)
        );
        hud.topLeftLines.emplace_back(
          "procedural box batches: opaque " +
          std::to_string(diagnostics.proceduralPlayerOpaqueBatches) +
          " | draws " +
          std::to_string(diagnostics.proceduralPlayerOpaqueDrawCalls) +
          " | outline batches " +
          std::to_string(diagnostics.proceduralPlayerOutlineMaskBatches) +
          " | outline draws " +
          std::to_string(diagnostics.proceduralPlayerOutlineMaskDrawCalls)
        );
        hud.topLeftLines.emplace_back(
          "procedural box upload: instances " +
          std::to_string(diagnostics.playerBoxInstanceUploadBytes) +
          " B | shared cube " +
          std::to_string(diagnostics.sharedCubeStaticGpuBytes) +
          " B | legacy player vertices " +
          std::to_string(diagnostics.legacyCpuGeneratedPlayerVertices) +
          " | legacy upload " +
          std::to_string(diagnostics.legacyDynamicPlayerVertexUploadBytes) +
          " B"
        );
        hud.topLeftLines.emplace_back(
          "gltf players: active " +
          std::to_string(diagnostics.gltfPlayerModelInstances) +
          " | culled " +
          std::to_string(diagnostics.gltfPlayerModelFrustumCulled) +
          " | skinned " +
          std::to_string(diagnostics.gltfGpuSkinnedInstances) +
          " | rigid " +
          std::to_string(diagnostics.gltfRigidFallbackInstances)
        );
        hud.topLeftLines.emplace_back(
          "gltf resident: vertex " +
          std::to_string(diagnostics.gltfStaticMeshGpuBytes) +
          " B | index " +
          std::to_string(diagnostics.gltfStaticIndexGpuBytes) +
          " B | material " +
          std::to_string(diagnostics.gltfMaterialTextureGpuBytes) +
          " B" +
          " | material mips " +
          std::to_string(diagnostics.gltfMaterialTextureMipLevels) +
          " | material binds " +
          std::to_string(diagnostics.gltfMaterialTextureBinds) +
          " | authored " +
          (diagnostics.gltfAuthoredMaterialTexturesReady ? "ready" : "flat") +
          (diagnostics.gltfMaterialFallbackUsed ? " fallback" : "") +
          " | pose " +
          std::to_string(diagnostics.gltfPoseUploadBytes) +
          " B | bones " +
          std::to_string(diagnostics.gltfBonePaletteEntriesUploaded)
        );
        hud.topLeftLines.emplace_back(
          "gltf batches: body " +
          std::to_string(diagnostics.gltfBodyBatches) +
          " | body draws " +
          std::to_string(diagnostics.gltfBodyDrawCalls) +
          " | outline batches " +
          std::to_string(diagnostics.gltfOutlineMaskBatches) +
          " | outline draws " +
          std::to_string(diagnostics.gltfOutlineMaskDrawCalls)
        );
        hud.topLeftLines.emplace_back(
          "gltf legacy cpu-skinned upload " +
          std::to_string(diagnostics.legacyCpuSkinnedGltfVertexUploadBytes) +
          " B"
        );
        if (console.getInt("r_player_model") > 0) {
          std::string loadedAnimations = "gltf clips:";
          const GltfSkinnedModel& activeModel = console.getInt("r_player_model") == 2
            ? workerPlayerModel()
            : duelistMaleModel();
          for (const std::string& name : activeModel.animationNames()) {
            loadedAnimations += " " + name;
          }
          hud.topLeftLines.emplace_back(std::move(loadedAnimations));
        }
        for (std::size_t playerIndex = 0; playerIndex < renderRemotePlayers.size(); ++playerIndex) {
          const RemotePlayerView& remote = renderRemotePlayers[playerIndex];
          if (!remote.visible || !remote.hasPresentation) continue;
          const PlayerPresentationDiagnostics& animation =
            remote.presentation.diagnostics;
          hud.topLeftLines.emplace_back(
            "anim p" + std::to_string(playerIndex) +
            ": state " + std::to_string(static_cast<int>(animation.currentState)) +
            " prev " + std::to_string(static_cast<int>(animation.previousState)) +
            " dir " + std::to_string(static_cast<int>(animation.moveDirection)) +
            " phase " + std::to_string(animation.stridePhase) +
            " blend " + std::to_string(animation.currentBlendWeight) +
            " speed " + std::to_string(animation.horizontalSpeed) +
            (animation.airborne ? " air" : (animation.landing ? " landing" : " ground"))
          );
          for (std::size_t layerIndex = 0;
               layerIndex < remote.presentation.poseLayerCount;
               ++layerIndex) {
            const PlayerPoseLayer& layer = remote.presentation.poseLayers[layerIndex];
            hud.topLeftLines.emplace_back(
              "  clip " + std::string(layer.animationName) +
              " t " + std::to_string(layer.timeSeconds) +
              " w " + std::to_string(layer.weight)
            );
          }
        }
        hud.topLeftLines.emplace_back(
          "remote weapon instances: candidates " +
          std::to_string(diagnostics.remoteWeaponCandidates) +
          " | submitted " +
          std::to_string(diagnostics.remoteWeaponInstances) +
          " | culled " +
          std::to_string(diagnostics.remoteWeaponsFrustumCulled)
        );
        hud.topLeftLines.emplace_back(
          "remote weapon batches: batches " +
          std::to_string(diagnostics.remoteWeaponBatches) +
          " | draws " +
          std::to_string(diagnostics.remoteWeaponDrawCalls) +
          " | upload " +
          std::to_string(diagnostics.remoteWeaponInstanceUploadBytes) +
          " B | legacy vertices " +
          std::to_string(diagnostics.legacyRemoteWeaponDynamicVertices)
        );
        hud.topLeftLines.emplace_back(
          "viewmodel static: draws " +
          std::to_string(diagnostics.firstPersonViewModelDrawCalls) +
          " | dynamic vertices " +
          std::to_string(diagnostics.firstPersonViewModelDynamicVertices)
        );
        hud.topLeftLines.emplace_back(
          "projectiles: active " +
          std::to_string(diagnostics.projectilesActive) +
          " | rendered " +
          std::to_string(diagnostics.projectilesRendered) +
          " | culled " +
          std::to_string(diagnostics.projectilesFrustumCulled)
        );
        hud.topLeftLines.emplace_back(
          "projectile instances: plasma " +
          std::to_string(diagnostics.plasmaInstances) +
          " | rocket " +
          std::to_string(diagnostics.rocketInstances) +
          " | grenade " +
          std::to_string(diagnostics.grenadeInstances) +
          " | glow " +
          std::to_string(diagnostics.projectileGlowInstances) +
          " | instance upload " +
          std::to_string(diagnostics.projectileInstanceUploadBytes) +
          " B"
        );
        hud.topLeftLines.emplace_back(
          "projectile batches: opaque " +
          std::to_string(diagnostics.opaqueProjectileBatches) +
          " | additive " +
          std::to_string(diagnostics.additiveProjectileBatches) +
          " | draw calls " +
          std::to_string(
            diagnostics.projectileMeshDrawCalls +
            diagnostics.projectileGlowDrawCalls
          ) +
          " | legacy projectile vertices " +
            std::to_string(diagnostics.legacyProjectileDynamicVertices)
        );
        hud.topLeftLines.emplace_back(
          "transient VFX: active " +
          std::to_string(diagnostics.activeTransientEffects) +
          " | MG " +
          std::to_string(diagnostics.activeMachineGunTracers) +
          " | SG " +
          std::to_string(diagnostics.activeShotgunTracers) +
          " | explosions " +
          std::to_string(diagnostics.activeExplosionEffects) +
          " | new explosions " +
          std::to_string(diagnostics.newExplosionEventsConsumed)
        );
        hud.topLeftLines.emplace_back(
          "tracer instances: submitted " +
          std::to_string(diagnostics.tracerInstancesSubmitted) +
          " | upload " +
          std::to_string(diagnostics.tracerInstanceUploadBytes) +
          " B | batches " +
          std::to_string(diagnostics.tracerBatches) +
          " | draws " +
          std::to_string(diagnostics.tracerDrawCalls) +
          " | legacy MG/SG draws " +
          std::to_string(diagnostics.legacyMachineGunShotgunVisualDraws)
        );
        hud.topLeftLines.emplace_back(
          "explosion instances: submitted " +
          std::to_string(diagnostics.explosionInstancesSubmitted) +
          " | culled " +
          std::to_string(diagnostics.explosionFrustumCulled) +
          " | upload " +
          std::to_string(diagnostics.explosionInstanceUploadBytes) +
          " B | batches " +
          std::to_string(
            diagnostics.explosionOpaqueBatches +
            diagnostics.explosionAdditiveBatches
          ) +
          " | draws " +
          std::to_string(diagnostics.explosionDrawCalls) +
          " | legacy wireframes " +
          std::to_string(diagnostics.legacyWireframeExplosionDraws)
        );
      }
    }
    if (
      currentRenderSettings.showLagCompensation &&
      session.game() != nullptr &&
      session.game()->hasSnapshot() &&
      !session.spectator()
    ) {
      const std::size_t localPlayerIndex = session.playerIndex();
      const ServerSnapshot& lagSnapshot = session.game()->snapshot();
      const LightningGunResult& beam =
        lagSnapshot.lightningGuns[localPlayerIndex];
      if (beam.hasRewindDebug) {
        char rewindText[192];
        std::snprintf(
          rewindText,
          sizeof(rewindText),
          "REWIND %u/%u%s TARGET TICK %u",
          beam.requestedRewindTicks,
          beam.appliedRewindTicks,
          beam.rewindClamped ? " CLAMP" : "",
          beam.rewindTargetTick
        );
        hud.topLeftLines.emplace_back(rewindText);
        std::snprintf(
          rewindText,
          sizeof(rewindText),
          "CURRENT %.2f %.2f %.2f | REWOUND %.2f %.2f %.2f",
          beam.currentTargetPosition.x,
          beam.currentTargetPosition.y,
          beam.currentTargetPosition.z,
          beam.rewoundTargetPosition.x,
          beam.rewoundTargetPosition.y,
          beam.rewoundTargetPosition.z
        );
        hud.topLeftLines.emplace_back(rewindText);
      } else {
        const Weapon selectedWeapon =
          lagSnapshot.selectedWeapons[localPlayerIndex];
        hud.topLeftLines.emplace_back(
          selectedWeapon == Weapon::LightningGun ||
            selectedWeapon == Weapon::FreezeGun
            ? "LAG COMPENSATION: NOT USED"
            : "LAG COMPENSATION: NOT USED BY THIS WEAPON"
        );
      }
    }
    if (
      scoreboardPressCount > 0 &&
      session.game() != nullptr &&
      session.game()->hasSnapshot()
    ) {
      populateScoreboard(
        hud,
        session.game()->snapshot(),
        hudSubjectPlayerIndex.value_or(kDuelPlayerCount)
      );
    }
    if (
      chatState.inputOpen ||
      chatHistoryPressCount > 0 ||
      Clock::now() < chatState.visibleUntil
    ) {
      for (const ClientChatState::Message& message : chatState.history) {
        hud.chatLines.push_back(HudRenderState::ChatLine{
          message.playerIndex,
          message.text,
          message.speakerName,
        });
      }
    }
    hud.chatInputOpen = chatState.inputOpen;
    hud.chatHistoryExpanded =
      chatState.inputOpen || chatHistoryPressCount > 0;
    hud.chatInput = chatState.input;
    hud.chatCursorIndex = chatState.cursorIndex;
    hud.chatHasSelection = hasSelection(chatState.selection);
    hud.chatSelectionAnchor = chatState.selection.anchor;
    hud.chatSelectionFocus = chatState.selection.focus;
    hud.chatHistoryHasSelection = chatState.hasHistorySelection;
    hud.chatHistorySelectionAnchor = chatState.historySelectionAnchor;
    hud.chatHistorySelectionFocus = chatState.historySelectionFocus;
    populateSettingsMenuRenderState(hud, settingsMenu);
    populateMiscMenuRenderState(hud, miscMenu, console);
    if (currentRenderSettings.showCollision != 0) {
      static constexpr std::array<std::string_view, 6> collisionModeLabels = {{
        "off",
        "all: blue solid | green playerclip | orange weapclip | purple trigger",
        "visible solids (blue)",
        "playerclip (green)",
        "weapclip (orange)",
        "triggers (purple)",
      }};
      const std::size_t collisionMode = static_cast<std::size_t>(
        std::clamp(currentRenderSettings.showCollision, 0, 5)
      );
      hud.topLeftLines.emplace_back(
        "collision: " + std::string(collisionModeLabels[collisionMode])
      );
    }
    if (console.getBool("r_perf")) {
      appendPerfHudLines(
        hud,
        perfTelemetry.summarize(),
        console.getBool("r_perf_detail"),
        console
      );
    }
    const CombatEffectsTuning frameEffectsTuning =
      combatEffectsTuning(console);
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      WeaponFireResult attachmentFire;
      attachmentFire.start = renderPlayer.position;
      combatEffects.setMuzzleAttachment(
        static_cast<std::uint8_t>(playerIndex),
        MuzzleAttachment::MachineGun,
        machineGunTracerSource(
          attachmentFire,
          renderPlayer,
          renderRemotePlayers,
          playerIndex,
          currentRenderSettings
        )
      );
      combatEffects.setMuzzleAttachment(
        static_cast<std::uint8_t>(playerIndex),
        MuzzleAttachment::RocketLauncher,
        rocketLauncherMuzzleSource(
          attachmentFire,
          renderPlayer,
          renderRemotePlayers,
          playerIndex,
          currentRenderSettings
        )
      );
      combatEffects.setMuzzleAttachment(
        static_cast<std::uint8_t>(playerIndex),
        MuzzleAttachment::FreezeGun,
        freezeGunMuzzleSource(
          attachmentFire.start,
          renderPlayer,
          renderRemotePlayers,
          playerIndex,
          currentRenderSettings
        )
      );
    }
    transientTracerStore.update(outerFrameElapsed.count());
    combatEffects.update(outerFrameElapsed.count(), frameEffectsTuning);
    LocalSurfaceImpactFrame localSurfaceImpact = {};
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      if (!freezeGunPulseDue[playerIndex]) {
        continue;
      }
      std::uint32_t& pulseSerial = freezeGunPulseSerials[playerIndex];
      ++pulseSerial;
      if (pulseSerial == 0U) {
        ++pulseSerial;
      }
      const bool localPlayer = playerIndex == renderLocalPlayerIndex;
      const LightningGunResult& beam = localPlayer
        ? renderLocalLightningGun
        : renderRemotePlayers[playerIndex].lightningGun;
      const std::uint32_t visualSeed =
        0xb5297a4dU * static_cast<std::uint32_t>(playerIndex + 1U) +
        pulseSerial * 0x68e31da4U;
      const bool spawnedWorldImpact = spawnFreezeGunPulse(
        combatEffects,
        renderArena,
        beam,
        freezeGunMuzzleSource(
          beam.start,
          renderPlayer,
          renderRemotePlayers,
          playerIndex,
          currentRenderSettings
        ),
        static_cast<std::uint8_t>(playerIndex),
        visualSeed,
        impactSurfaceMaterials,
        frameEffectsTuning
      );
      if (localPlayer && spawnedWorldImpact) {
        localSurfaceImpact = {true, Weapon::FreezeGun};
      }
    }
    consumeTracerWeaponFires(
      transientTracerStore,
      combatEffects,
      renderArena,
      renderPlayer,
      renderRemotePlayers,
      renderWeaponFires,
      localTracerAimHistory,
      currentRenderSettings,
      frameEffectsTuning,
      impactSurfaceMaterials,
      ownsPresentedSubject,
      localSurfaceImpact
    );
    consumeExplosionEvents(
      transientTracerStore,
      combatEffects,
      frameEffectsTuning,
      renderRocketExplosions
    );
    transientTracerStore.fillActive(
      activeTransientTracers,
      renderPlayer,
      renderRemotePlayers,
      currentRenderSettings
    );
    transientTracerStore.fillActiveEffects(activeTransientEffects);
    combatEffects.appendActive(activeTransientEffects);
    if (session.game() != nullptr && session.game()->hasSnapshot()) {
      const ServerSnapshot& objectiveSnapshot = session.game()->snapshot();
      if (objectiveSnapshot.gameMode == GameMode::McGuffin) {
        const auto addMarker = [&activeTransientEffects](
          Vec3 position,
          RenderColor color,
          float scale,
          std::uint32_t seed
        ) {
          activeTransientEffects.push_back({
            TransientEffectType::PlasmaExplosionCore,
            position,
            0.0F,
            1.0F,
            scale,
            scale,
            color,
            seed,
          });
        };
        const ArenaMcGuffinLayout& layout = renderArena.mcguffin;
        if (layout.hasRedBase) {
          addMarker((layout.redBase.min + layout.redBase.max) * 0.5F,
            {255, 64, 64, 150}, 0.55F, 0xA11CE001U);
        }
        if (layout.hasBlueBase) {
          addMarker((layout.blueBase.min + layout.blueBase.max) * 0.5F,
            {64, 128, 255, 150}, 0.55F, 0xA11CE002U);
        }
        const bool waitingToSpawn =
          objectiveSnapshot.mcguffin.state == McGuffinState::NeutralSpawn &&
          objectiveSnapshot.mcguffin.stateTicks <
            objectiveSnapshot.mcguffinConfig.initialSpawnTicks;
        if (!waitingToSpawn) {
          const RenderColor color = objectiveSnapshot.mcguffin.associatedTeam == Team::Red
            ? RenderColor{255, 72, 72, 235}
            : objectiveSnapshot.mcguffin.associatedTeam == Team::Blue
              ? RenderColor{72, 140, 255, 235}
              : RenderColor{255, 224, 96, 235};
          addMarker(objectiveSnapshot.mcguffin.position, color, 0.32F, 0xA11CE003U);
        }
      }
    }
    hud.chatScrollRows = chatState.scrollRows;
    std::optional<FrameCaptureRequest> frameCaptureRequest;
    FrameCaptureResult frameCaptureResult;
    dev::JsonValue captureFrameState;
    bool captureHideHud = false;
    bool captureHideOverlays = false;
    bool phaseFrameCapture = false;
    if (armedPhaseCapture.has_value() &&
        !armedPhaseCapture->result.has_value() &&
        armedPhaseCapture->error.empty()) {
      const WeaponFireResult& localFire =
        renderWeaponFires[renderLocalPlayerIndex];
      const bool hasLocalRocket = std::any_of(
        renderRockets.begin(),
        renderRockets.end(),
        [renderLocalPlayerIndex](const RocketProjectileSnapshot& rocket) {
          return rocket.active &&
            rocket.weapon == Weapon::RocketLauncher &&
            rocket.owner == renderLocalPlayerIndex;
        }
      );
      if (
        (
          armedPhaseCapture->phase == "local_rocket_launcher_muzzle" &&
          localFire.fired &&
          localFire.weapon == Weapon::RocketLauncher
        ) ||
        (
          armedPhaseCapture->phase == "local_rocket_launcher_projectile" &&
          !localFire.fired &&
          hasLocalRocket &&
          !renderRocketExplosions[renderLocalPlayerIndex].active &&
          activeTransientTracers.empty()
        ) ||
        (
          armedPhaseCapture->phase == "local_rocket_launcher_impact" &&
          renderRocketExplosions[renderLocalPlayerIndex].active &&
          renderRocketExplosions[renderLocalPlayerIndex].weapon ==
            Weapon::RocketLauncher
        ) ||
        (
          armedPhaseCapture->phase == "local_surface_impact" &&
          localSurfaceImpact.active
        )
      ) {
        captureHideHud = armedPhaseCapture->hideHud;
        captureHideOverlays = armedPhaseCapture->hideOverlays;
        frameCaptureRequest = FrameCaptureRequest{
          armedPhaseCapture->path.string(),
          captureHideHud,
          captureHideOverlays,
        };
        phaseFrameCapture = true;
      }
    }
    if (!frameCaptureRequest.has_value() &&
        activeControlOperation.has_value() &&
        (activeControlOperation->stage == ActiveControlOperation::Stage::CaptureReady ||
         activeControlOperation->stage == ActiveControlOperation::Stage::BenchmarkCaptureReady)) {
      ActiveControlOperation& active = *activeControlOperation;
      const dev::ControlRequest& request = active.queued.request;
      if (request.operation == dev::ControlOperation::RunBenchmark) {
        const benchmark::Screenshot& screenshot =
          request.benchmarkScenario.screenshots[active.benchmarkScreenshotIndex];
        const std::filesystem::path screenshotDirectory = repositoryRoot / "build" /
          "benchmarks" / request.benchmarkScenario.name / request.runGroup /
          request.runId / "screenshots";
        std::error_code screenshotDirectoryError;
        std::filesystem::create_directories(screenshotDirectory, screenshotDirectoryError);
        if (screenshotDirectoryError) {
          completeControlError("capture_directory_failed", screenshotDirectoryError.message());
        } else {
          active.pendingCapturePath = screenshotDirectory / (screenshot.name + ".png");
          captureHideHud = true;
          captureHideOverlays = true;
          frameCaptureRequest = FrameCaptureRequest{
            active.pendingCapturePath.string(), true, true
          };
        }
      } else {
      std::string captureName = active.captureStem;
      if (request.operation == dev::ControlOperation::CaptureMapViews) {
        const dev::CameraViewpoint& view = request.viewpoints[active.viewpointIndex];
        captureName += "-" + view.name;
        captureHideHud = view.hideHud;
        captureHideOverlays = view.hideOverlays;
      } else {
        captureHideHud = request.hideHud;
        captureHideOverlays = request.hideOverlays;
      }
      active.pendingCapturePath = captureDirectory / (captureName + ".png");
      frameCaptureRequest = FrameCaptureRequest{
        active.pendingCapturePath.string(),
        captureHideHud,
        captureHideOverlays,
      };
      }
    }
    if (frameCaptureRequest.has_value()) {
      // Bind phase evidence to the inputs of this exact render. GPU readback
      // may block long enough for later server snapshots to arrive, so a
      // control query made after capture cannot attest the saved PNG.
      captureFrameState = dev::JsonValue::objectValue();
      captureFrameState.object["rendered_frame_serial"] =
        dev::JsonValue::numberValue(
          static_cast<double>(renderedFrameSerial + 1U)
        );
      captureFrameState.object["local_player_index"] =
        dev::JsonValue::numberValue(renderLocalPlayerIndex);
      const ClientGame* captureGame = session.game();
      const bool captureHasSnapshot =
        captureGame != nullptr && captureGame->hasSnapshot();
      captureFrameState.object["latest_snapshot_tick"] = captureHasSnapshot
        ? dev::JsonValue::numberValue(captureGame->snapshot().serverTick)
        : dev::JsonValue{};
      captureFrameState.object["presentation_tick"] = captureHasSnapshot
        ? dev::JsonValue::numberValue(
            captureGame->interpolationDiagnostics().presentationTick
          )
        : dev::JsonValue{};
      captureFrameState.object["local_surface_impact_active"] =
        dev::JsonValue::booleanValue(localSurfaceImpact.active);
      captureFrameState.object["local_surface_impact_weapon"] =
        localSurfaceImpact.active
        ? dev::JsonValue::stringValue(
            std::string(surfaceImpactCaptureWeaponName(localSurfaceImpact.weapon))
          )
        : dev::JsonValue{};
      const std::uint32_t localSurfaceContactEffectCount =
        static_cast<std::uint32_t>(std::count_if(
          activeTransientEffects.begin(),
          activeTransientEffects.end(),
          [](const TransientEffect& effect) {
            return effect.type == TransientEffectType::BulletImpactFlash ||
              effect.type == TransientEffectType::BulletImpactSpark ||
              effect.type == TransientEffectType::BulletImpactDust ||
              effect.type == TransientEffectType::BulletDecal;
          }
      ));
      captureFrameState.object["local_surface_contact_effect_count"] =
        dev::JsonValue::numberValue(localSurfaceContactEffectCount);

      const WeaponFireResult& localFire =
        renderWeaponFires[renderLocalPlayerIndex];
      captureFrameState.object["local_rocket_launcher_fired"] =
        dev::JsonValue::booleanValue(
          localFire.fired && localFire.weapon == Weapon::RocketLauncher
        );
      std::uint32_t localRocketProjectiles = 0;
      std::uint32_t totalRocketProjectiles = 0;
      for (const RocketProjectileSnapshot& rocket : renderRockets) {
        if (!rocket.active || rocket.weapon != Weapon::RocketLauncher) {
          continue;
        }
        ++totalRocketProjectiles;
        if (rocket.owner == renderLocalPlayerIndex) {
          ++localRocketProjectiles;
        }
      }
      std::uint32_t localRocketExplosions = 0;
      std::uint32_t totalRocketExplosions = 0;
      for (std::size_t owner = 0; owner < renderRocketExplosions.size(); ++owner) {
        const RocketExplosionResult& explosion = renderRocketExplosions[owner];
        if (!explosion.active || explosion.weapon != Weapon::RocketLauncher) {
          continue;
        }
        ++totalRocketExplosions;
        if (owner == renderLocalPlayerIndex) {
          ++localRocketExplosions;
        }
      }
      captureFrameState.object["local_rocket_launcher_projectiles"] =
        dev::JsonValue::numberValue(localRocketProjectiles);
      captureFrameState.object["total_rocket_launcher_projectiles"] =
        dev::JsonValue::numberValue(totalRocketProjectiles);
      captureFrameState.object["local_rocket_launcher_explosions"] =
        dev::JsonValue::numberValue(localRocketExplosions);
      captureFrameState.object["total_rocket_launcher_explosions"] =
        dev::JsonValue::numberValue(totalRocketExplosions);
    }
    ConsoleRenderState renderedConsole = consoleRenderState(consoleState);
    renderedConsole.showCat = console.getBool("cl_show_console_cat");
    if (activeControlOperation.has_value() &&
        activeControlOperation->queued.request.operation == dev::ControlOperation::RunBenchmark) {
      const benchmark::Scenario& scenario =
        activeControlOperation->queued.request.benchmarkScenario;
      if (scenario.hideHud) hud = {};
      if (scenario.hideOverlays) {
        renderedConsole = {};
        currentRenderSettings.showRendererPerf = false;
        hud.topLeftLines.clear();
        hud.settingsOpen = false;
        hud.settingsItems.clear();
        hud.miscMenuOpen = false;
        hud.miscMenuItems.clear();
      }
    }
    if (frameCaptureRequest.has_value()) {
      if (captureHideHud) hud = {};
      if (captureHideOverlays) {
        renderedConsole = {};
        currentRenderSettings.showRendererPerf = false;
        hud.topLeftLines.clear();
        hud.settingsOpen = false;
        hud.settingsItems.clear();
        hud.miscMenuOpen = false;
        hud.miscMenuItems.clear();
      }
    }
    LateMouseSampleContext lateMouseSampleContext = {
      window,
      &presentationView,
      frameMouseAimSettings,
      earlyMouseDeltaX,
      earlyMouseDeltaY,
      outerFrameElapsed.count(),
      earlyMouseSampleNanoseconds,
      viewPitchBeforeEarlyMouseSample,
      &pendingLateViewModelMouseDeltaX,
      &pendingLateViewModelMouseDeltaY,
      earlyMouseViewApplied &&
        relativeMouseModeEnabled &&
        presentationViewGame != nullptr &&
        presentationViewGame == session.game() &&
        !session.spectator() &&
        !developmentCameraEnabled &&
        deathCamera.mode != DeathCameraMode::Teammate,
    };
    const LateViewSampler lateViewSampler = lateMouseSamplingEnabled
      ? LateViewSampler{&lateMouseSampleContext, sampleLateMouseView}
      : LateViewSampler{};
    renderer.render(
      renderArena,
      renderPlayer,
      renderRemotePlayers,
      renderLocalLightningGun,
      renderWeaponFires,
      renderRocketExplosions,
      renderRockets,
      renderIcePools,
      renderHealthPickupAvailable,
      activeTransientTracers,
      activeTransientEffects,
      transientTracerStore.explosionEventsConsumedThisFrame,
      currentRenderSettings,
      hud,
      renderedConsole,
      lateViewSampler,
      frameCaptureRequest.has_value() ? &*frameCaptureRequest : nullptr,
      frameCaptureRequest.has_value() ? &frameCaptureResult : nullptr
    );
    ++renderedFrameSerial;
    if (frameCaptureRequest.has_value() &&
        (phaseFrameCapture || activeControlOperation.has_value())) {
      const RendererFrameDiagnostics& captureRender =
        renderer.lastFrameDiagnostics();
      captureFrameState.object["renderer_rocket_instances"] =
        dev::JsonValue::numberValue(captureRender.rocketInstances);
      captureFrameState.object["renderer_tracer_instances"] =
        dev::JsonValue::numberValue(captureRender.tracerInstancesSubmitted);
      captureFrameState.object["renderer_explosion_instances"] =
        dev::JsonValue::numberValue(
          captureRender.explosionInstancesSubmitted
        );
      captureFrameState.object["renderer_sky_draw_calls"] =
        dev::JsonValue::numberValue(captureRender.skyDrawCalls);
      captureFrameState.object["renderer_sky_loaded_textures"] =
        dev::JsonValue::numberValue(captureRender.skyLoadedTextures);
      const std::filesystem::path& completedCapturePath = phaseFrameCapture
        ? armedPhaseCapture->path
        : activeControlOperation->pendingCapturePath;
      dev::JsonValue capture = dev::JsonValue::objectValue();
      capture.object["ok"] = dev::JsonValue::booleanValue(frameCaptureResult.ok);
      capture.object["path"] =
        dev::JsonValue::stringValue(completedCapturePath.string());
      capture.object["relative_path"] = dev::JsonValue::stringValue(
        captureRelativePath(completedCapturePath)
      );
      capture.object["width"] = dev::JsonValue::numberValue(frameCaptureResult.width);
      capture.object["height"] = dev::JsonValue::numberValue(frameCaptureResult.height);
      capture.object["map"] = dev::JsonValue::stringValue(currentMapName());
      capture.object["map_revision"] = dev::JsonValue::numberValue(currentMapRevision());
      capture.object["map_content_hash"] =
        dev::JsonValue::numberValue(currentMapContentHash());
      capture.object["renderer"] =
        dev::JsonValue::stringValue(std::string(renderer.backendName()));
      capture.object["camera"] = dev::cameraJson(currentControlCamera());
      capture.object["timestamp_ms"] =
        dev::JsonValue::numberValue(static_cast<double>(timestampMilliseconds()));
      capture.object["frame_state"] = std::move(captureFrameState);
      if (!frameCaptureResult.ok) {
        capture.object["error"] = dev::JsonValue::stringValue(frameCaptureResult.error);
        lastControlError = frameCaptureResult.error;
      }

      if (phaseFrameCapture) {
        if (frameCaptureResult.ok) {
          armedPhaseCapture->result = std::move(capture);
        } else {
          armedPhaseCapture->error = frameCaptureResult.error;
        }
      } else {
      ActiveControlOperation& active = *activeControlOperation;
      const dev::ControlRequest& request = active.queued.request;
      if (request.operation == dev::ControlOperation::RunBenchmark) {
        if (!frameCaptureResult.ok) {
          completeControlError("capture_failed", frameCaptureResult.error);
        } else {
          active.benchmarkScreenshotPaths.push_back(
            captureRelativePath(active.pendingCapturePath)
          );
          ++active.benchmarkScreenshotIndex;
          if (active.benchmarkScreenshotIndex >= request.benchmarkScenario.screenshots.size()) {
            active.stage = ActiveControlOperation::Stage::BenchmarkFinalize;
          } else {
            const benchmark::Screenshot& next =
              request.benchmarkScenario.screenshots[active.benchmarkScreenshotIndex];
            const double duration = request.benchmarkScenario.measuredSeconds.value_or(
              active.benchmarkSamples.empty() ? 0.0 : active.benchmarkSamples.back().elapsedSeconds
            );
            setBenchmarkCamera(benchmark::cameraAt(
              request.benchmarkScenario, duration * next.progress
            ));
            active.requiredRenderedFrame = renderedFrameSerial + 1U;
            active.deadline = Clock::now() + std::chrono::seconds(20);
            active.stage = ActiveControlOperation::Stage::BenchmarkWaitingForCameraFrame;
          }
        }
      } else if (request.operation == dev::ControlOperation::CaptureScreenshot) {
        if (frameCaptureResult.ok) {
          developerControl.complete(
            active.queued.token,
            dev::successResponse(request.id, std::move(capture))
          );
        } else {
          developerControl.complete(
            active.queued.token,
            dev::errorResponse(request.id, "capture_failed", frameCaptureResult.error)
          );
        }
        activeControlOperation.reset();
      } else {
        const dev::CameraViewpoint& completedView =
          request.viewpoints[active.viewpointIndex];
        capture.object["name"] = dev::JsonValue::stringValue(completedView.name);
        capture.object["label"] = dev::JsonValue::stringValue(completedView.label);
        active.viewResults.push_back(std::move(capture));
        ++active.viewpointIndex;
        if (active.viewpointIndex < request.viewpoints.size()) {
          developmentCamera = request.viewpoints[active.viewpointIndex].camera;
          developmentCameraEnabled = true;
          active.requiredRenderedFrame = renderedFrameSerial + 1U;
          active.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
          active.stage = ActiveControlOperation::Stage::WaitingForCameraFrame;
        } else {
          dev::JsonValue manifest = dev::JsonValue::objectValue();
          manifest.object["map"] = dev::JsonValue::stringValue(currentMapName());
          manifest.object["map_revision"] =
            dev::JsonValue::numberValue(currentMapRevision());
          manifest.object["map_content_hash"] =
            dev::JsonValue::numberValue(currentMapContentHash());
          manifest.object["renderer"] =
            dev::JsonValue::stringValue(std::string(renderer.backendName()));
          manifest.object["preset"] = dev::JsonValue::stringValue(request.presetName);
          manifest.object["timestamp_ms"] =
            dev::JsonValue::numberValue(static_cast<double>(timestampMilliseconds()));
          manifest.object["views"] = dev::JsonValue::arrayValue(active.viewResults);
          const std::filesystem::path manifestPath =
            captureDirectory / (active.captureStem + "-manifest.json");
          std::ofstream manifestFile(manifestPath, std::ios::binary | std::ios::trunc);
          manifestFile << dev::writeJson(manifest) << '\n';
          if (!manifestFile) {
            completeControlError(
              "manifest_write_failed",
              "could not write capture manifest '" + manifestPath.string() + "'"
            );
          } else {
            dev::JsonValue result = manifest;
            result.object["manifest_path"] =
              dev::JsonValue::stringValue(manifestPath.string());
            result.object["manifest_relative_path"] =
              dev::JsonValue::stringValue(captureRelativePath(manifestPath));
            developerControl.complete(
              active.queued.token,
              dev::successResponse(request.id, std::move(result))
            );
            restoreControlCvars();
            activeControlOperation.reset();
          }
        }
      }
      }
    }
    if (activeControlOperation.has_value() &&
        activeControlOperation->stage == ActiveControlOperation::Stage::BenchmarkFinalize) {
      ActiveControlOperation& active = *activeControlOperation;
      const dev::ControlRequest& request = active.queued.request;
      benchmark::ResultContext context;
      context.runId = request.runId;
      context.runGroup = request.runGroup;
      context.scenarioHash = request.scenarioHash;
      context.actualMap = currentMapName();
      context.renderer = std::string(renderer.backendName());
      context.actualMapContentHash = currentMapContentHash();
      SDL_GetWindowSizeInPixels(window, &context.actualWidth, &context.actualHeight);
      context.selectedPresentMode = renderer.lastFrameDiagnostics().selectedPresentModeName;
      context.graphicsProfile = request.benchmarkScenario.graphicsProfile;
      context.renderScale = request.benchmarkScenario.renderScale;
      const GpuTimingAvailability& gpuTiming = renderer.gpuTimingMetadata();
      context.gpuTimingAvailable = gpuTiming.available;
      context.gpuTimingBackend = gpuTiming.backend;
      context.gpuTimingUnavailableReason = gpuTiming.unavailableReason;
      if (gpuTiming.available) {
        context.gpuTimestampValidBits = gpuTiming.timestampValidBits;
        context.gpuTimestampPeriodNanoseconds =
          gpuTiming.timestampPeriodNanoseconds;
      }
      context.gpuTimingInstrumentationVersion =
        gpuTiming.instrumentationVersion;
      context.sdlBaseRevision = gpuTiming.sdlBaseRevision;
      context.sdlPatchIdentity = gpuTiming.sdlPatchIdentity;
      std::uint64_t readbackLatencyTotal = 0;
      std::size_t readbackLatencyCount = 0;
      std::uint32_t maximumReadbackLatencyFrames = 0;
      for (const benchmark::FrameSample& sample : active.benchmarkSamples) {
        if (!context.gpuTimingAvailable ||
            !sample.gpuTimingResultReceived) continue;
        maximumReadbackLatencyFrames = std::max(
          maximumReadbackLatencyFrames,
          sample.gpuTimingReadbackLatencyFrames
        );
        readbackLatencyTotal += sample.gpuTimingReadbackLatencyFrames;
        ++readbackLatencyCount;
      }
      if (readbackLatencyCount > 0U) {
        context.gpuTimingReadbackLatencyFrames =
          maximumReadbackLatencyFrames;
        context.gpuTimingMeanReadbackLatencyFrames =
          static_cast<double>(readbackLatencyTotal) /
          static_cast<double>(readbackLatencyCount);
      }
      context.completed = true;
      context.screenshotPaths = active.benchmarkScreenshotPaths;
      if (const ClientGame* game = session.game(); game != nullptr && game->hasSnapshot()) {
        context.actualActorCount = static_cast<std::uint32_t>(std::count(
          game->snapshot().participatingPlayers.begin(),
          game->snapshot().participatingPlayers.end(),
          true
        ));
      }
      if (context.actualActorCount !=
          static_cast<std::uint32_t>(request.benchmarkScenario.actors.expectedCount)) {
        context.warnings.push_back("observed actor count does not equal actors.expected_count");
      }
      if (request.benchmarkScenario.unsupportedEffectFixture) {
        context.warnings.push_back("requested effect fixture is unsupported by the native benchmark runner");
      }
      std::filesystem::path resultDirectory;
      std::string artifactError;
      if (!benchmark::writeArtifacts(
            repositoryRoot / "build" / "benchmarks",
            request.benchmarkScenario,
            context,
            active.benchmarkSamples,
            active.benchmarkTickSamples,
            resultDirectory,
            artifactError
          )) {
        completeControlError("artifact_write_failed", artifactError);
      } else {
        dev::JsonValue response = benchmark::resultJson(
          request.benchmarkScenario,
          context,
          active.benchmarkSamples,
          active.benchmarkTickSamples
        );
        response.object["result_directory"] =
          dev::JsonValue::stringValue(resultDirectory.string());
        response.object["result_path"] = dev::JsonValue::stringValue(
          (resultDirectory / "result.json").string()
        );
        response.object["frame_times_path"] = dev::JsonValue::stringValue(
          (resultDirectory / "frame-times.csv").string()
        );
        response.object["frame_timeline_path"] = dev::JsonValue::stringValue(
          (resultDirectory / "frame-timeline.json").string()
        );
        response.object["telemetry_path"] = dev::JsonValue::stringValue(
          (resultDirectory / "telemetry.csv").string()
        );
        response.object["simulation_ticks_path"] = dev::JsonValue::stringValue(
          (resultDirectory / "simulation-ticks.csv").string()
        );
        dev::JsonValue effectiveCvars = dev::JsonValue::objectValue();
        constexpr std::array<std::string_view, 9> kBenchmarkGraphicsContractCvars{{
          "r_antialiasing", "r_sun_shadows", "r_contact_shadows",
          "r_material_quality", "r_ambient_grounding", "r_player_rim",
          "r_atmosphere_grade", "r_bloom", "r_render_scale",
        }};
        for (const std::string_view name : kBenchmarkGraphicsContractCvars) {
          effectiveCvars.object[std::string(name)] =
            dev::JsonValue::stringValue(console.valueString(name));
        }
        response.object["effective_cvars"] = std::move(effectiveCvars);
        response.object["render_pass_diagnostics"] =
          benchmarkRenderPassDiagnostics(renderer.lastFrameDiagnostics());
        developerControl.complete(
          active.queued.token,
          dev::successResponse(request.id, std::move(response))
        );
        restoreBenchmarkState();
        activeControlOperation.reset();
      }
    }
    session.update();
    if (console.getBool("r_perf")) {
      const ClientGame* perfGame = session.game();
      perfTelemetry.push(
        perfSampleFromFrame(
          outerFrameMilliseconds,
          renderer.lastFrameDiagnostics(),
          perfGame != nullptr ? perfGame->snapshotDiagnostics() : SnapshotDiagnostics{}
        )
      );
    }
    if (benchmarkFrameTimingEnabled && activeControlOperation.has_value()) {
      activeControlOperation->lastBenchmarkFrameTiming =
        currentBenchmarkFrameTiming;
    }
    const int requestedMaxFps = std::max(0, console.getInt("r_maxfps"));
    if (requestedMaxFps != appliedMaxFps) {
      appliedMaxFps = requestedMaxFps;
      nextFrameDeadline = Clock::now();
    }
    if (appliedMaxFps > 0) {
      // r_maxfps controls CPU/render pacing. V-sync, Mailbox, and Immediate
      // still control how completed frames are presented by SDL/driver.
      const auto frameDuration = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(appliedMaxFps))
      );
      const auto limiterNow = Clock::now();
      if (nextFrameDeadline <= limiterNow) {
        nextFrameDeadline = limiterNow + frameDuration;
      }
      if (limiterNow < nextFrameDeadline) {
        const auto sleepDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(
          nextFrameDeadline - limiterNow
        );
        SDL_DelayPrecise(static_cast<Uint64>(sleepDuration.count()));
      }
      const auto afterSleep = Clock::now();
      nextFrameDeadline += frameDuration;
      if (afterSleep > nextFrameDeadline + (frameDuration * 2)) {
        nextFrameDeadline = afterSleep + frameDuration;
      }
    }
  }
  restoreBenchmarkState();
  restoreControlCvars();
  developerControl.stop();
  saveClientConfig(console, bindings, configPath);
  audio.shutdown();
  renderer.shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
#else
  std::cout << name() << " local playable input/rendering requires SDL3.\n";
  std::cout << "Install SDL3 and configure with -DLG_DUEL_REQUIRE_SDL3=ON to enable the playable app.\n";
#endif

  return 0;
}

std::string_view GameApp::name() const {
  return "LG Duel Client";
}

} // namespace lg
