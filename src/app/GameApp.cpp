#include "app/GameApp.hpp"

#include "app/ClientAudio.hpp"
#include "app/ClientChat.hpp"
#include "app/ClientCvars.hpp"
#include "app/ConsoleInput.hpp"
#include "app/HudPresentation.hpp"
#include "app/PerfTelemetry.hpp"
#include "app/Scoreboard.hpp"
#include "app/TextInput.hpp"
#include "client/ClientSession.hpp"
#include "client/HitConfirmAudio.hpp"
#include "client/LocalHitFeedback.hpp"
#include "console/ConsoleConfig.hpp"
#include "console/ConsoleSystem.hpp"
#include "input/InputBindings.hpp"
#include "input/MouseAim.hpp"
#include "render/ConsoleLayout.hpp"
#include "render/ChatLayout.hpp"
#include "render/GltfSkinnedModel.hpp"
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
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"
#include "sim/WeaponCatalog.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
constexpr float kDegreesToRadians = 0.01745329252F;
constexpr float kRadiansToDegrees = 57.2957795131F;
constexpr float kQ3RunRoll = 0.005F;
constexpr float kQuakeUnitsPerProjectUnit = 40.0F;
constexpr std::uint32_t kClientRailgunCooldownTicks = 188;
constexpr float kRailgunBeamLingerSeconds = 0.5F;
constexpr float kTwoPi = 6.28318530718F;
constexpr std::size_t kMaxTransientTracers = 128;
constexpr std::size_t kMaxTransientEffects = 192;
constexpr std::size_t kMaxConsumedTracerEvents = 64;
constexpr std::size_t kMaxConsumedExplosionEvents = 64;
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

#if LG_DUEL_HAS_SDL3
[[nodiscard]] bool isClipboardPasteKey(const SDL_KeyboardEvent& event) {
  return event.key == SDLK_V && (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

[[nodiscard]] bool isClipboardCopyKey(const SDL_KeyboardEvent& event) {
  return event.key == SDLK_C && (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

void pasteClipboardTextIntoConsole(std::string& input, std::size_t& cursorIndex) {
  char* clipboardText = SDL_GetClipboardText();
  if (clipboardText == nullptr) {
    return;
  }
  appendConsolePasteText(input, cursorIndex, clipboardText);
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

[[nodiscard]] Vec3 viewmodelMuzzlePosition(
  const PlayerState& player,
  Weapon weapon,
  int weaponPosition
) {
  constexpr CollisionBounds defaultBounds = {};
  const float eyeHeight =
    0.65F * (player.bounds.halfHeight / defaultBounds.halfHeight);
  const Vec3 eyePosition =
    player.position + Vec3{0.0F, 0.0F, eyeHeight};
  const bool revolver = weapon == Weapon::Revolver;
  return eyePosition +
    cameraForward(player.viewYawRadians, player.viewPitchRadians) *
      (revolver ? 0.75F : 0.55F) -
    cameraUp(player.viewYawRadians, player.viewPitchRadians) *
      (revolver ? 0.318F : 0.32F) +
    yawRight(player.viewYawRadians) * firstPersonWeaponSideOffset(weaponPosition);
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

struct ConsumedTracerEvent {
  std::uint8_t playerIndex = 0;
  Weapon weapon = Weapon::LightningGun;
  std::uint32_t visualSeed = 0;
  bool active = false;
};

struct ConsumedExplosionEvent {
  std::uint8_t ownerIndex = 0;
  std::uint32_t sequence = 0;
  bool active = false;
};

struct TransientTracerStore {
  std::array<TransientTracer, kMaxTransientTracers> tracers = {};
  std::array<bool, kMaxTransientTracers> active = {};
  std::array<bool, kMaxTransientTracers> followLocalMuzzle = {};
  std::array<Weapon, kMaxTransientTracers> followWeapon = {};
  std::array<std::uint32_t, kMaxTransientTracers> followSeed = {};
  std::array<TransientEffect, kMaxTransientEffects> effects = {};
  std::array<bool, kMaxTransientEffects> effectActive = {};
  std::array<ConsumedTracerEvent, kMaxConsumedTracerEvents> consumedEvents = {};
  std::array<ConsumedExplosionEvent, kMaxConsumedExplosionEvents> consumedExplosionEvents = {};
  std::array<bool, kDuelPlayerCount> hasLastExplosionSequence = {};
  std::array<std::uint32_t, kDuelPlayerCount> lastExplosionSequence = {};
  std::uint32_t nextConsumedEvent = 0;
  std::uint32_t nextConsumedExplosionEvent = 0;
  std::uint32_t explosionEventsConsumedThisFrame = 0;

  void update(float dt) {
    explosionEventsConsumedThisFrame = 0;
    const float elapsed = std::max(0.0F, dt);
    for (std::size_t index = 0; index < tracers.size(); ++index) {
      if (!active[index]) {
        continue;
      }
      tracers[index].ageSeconds += elapsed;
      if (tracers[index].ageSeconds >= tracers[index].lifetimeSeconds) {
        active[index] = false;
      }
    }
    for (std::size_t index = 0; index < effects.size(); ++index) {
      if (!effectActive[index]) {
        continue;
      }
      effects[index].ageSeconds += elapsed;
      if (effects[index].ageSeconds >= effects[index].lifetimeSeconds) {
        effectActive[index] = false;
      }
    }
  }

  [[nodiscard]] bool consumed(
    std::uint8_t playerIndex,
    Weapon weapon,
    std::uint32_t visualSeed
  ) const {
    for (const ConsumedTracerEvent& event : consumedEvents) {
      if (
        event.active &&
        event.playerIndex == playerIndex &&
        event.weapon == weapon &&
        event.visualSeed == visualSeed
      ) {
        return true;
      }
    }
    return false;
  }

  void remember(std::uint8_t playerIndex, Weapon weapon, std::uint32_t visualSeed) {
    consumedEvents[nextConsumedEvent % consumedEvents.size()] = {
      playerIndex,
      weapon,
      visualSeed,
      true,
    };
    ++nextConsumedEvent;
  }

  [[nodiscard]] bool consumedExplosion(
    std::uint8_t ownerIndex,
    std::uint32_t sequence
  ) const {
    if (ownerIndex >= kDuelPlayerCount) {
      return true;
    }
    if (
      hasLastExplosionSequence[ownerIndex] &&
      !isSequenceNewer(sequence, lastExplosionSequence[ownerIndex])
    ) {
      return true;
    }
    for (const ConsumedExplosionEvent& event : consumedExplosionEvents) {
      if (
        event.active &&
        event.ownerIndex == ownerIndex &&
        event.sequence == sequence
      ) {
        return true;
      }
    }
    return false;
  }

  void rememberExplosion(std::uint8_t ownerIndex, std::uint32_t sequence) {
    if (ownerIndex >= kDuelPlayerCount) {
      return;
    }
    consumedExplosionEvents[
      nextConsumedExplosionEvent % consumedExplosionEvents.size()
    ] = {
      ownerIndex,
      sequence,
      true,
    };
    ++nextConsumedExplosionEvent;
    lastExplosionSequence[ownerIndex] = sequence;
    hasLastExplosionSequence[ownerIndex] = true;
    ++explosionEventsConsumedThisFrame;
  }

  void add(
    const TransientTracer& tracer,
    bool followMuzzle = false,
    Weapon weapon = Weapon::LightningGun,
    std::uint32_t seed = 0
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
    followLocalMuzzle[slot] = followMuzzle;
    followWeapon[slot] = weapon;
    followSeed[slot] = seed;
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
  }

  void fillActive(
    std::vector<TransientTracer>& result,
    const PlayerState& localPlayer,
    const RenderSettings& settings
  ) const {
    result.clear();
    result.reserve(tracers.size());
    for (std::size_t index = 0; index < tracers.size(); ++index) {
      if (active[index]) {
        TransientTracer tracer = tracers[index];
        if (followLocalMuzzle[index]) {
          const Vec3 oldStart = tracer.start;
          if (!settings.showOwnWeapons) {
            tracer.start = hiddenWeaponVisualOrigin(localPlayer);
          } else if (followWeapon[index] == Weapon::MachineGun) {
            tracer.start = firstPersonMachineGunMuzzlePosition(
              localPlayer,
              settings
            );
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
            tracer.start = firstPersonRocketLauncherMuzzlePosition(
              localPlayer,
              settings
            );
          } else if (followWeapon[index] == Weapon::Revolver) {
            tracer.start = firstPersonRevolverMuzzlePosition(
              localPlayer,
              settings
            );
          }
          tracer.end += tracer.start - oldStart;
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
    return fireDistance;
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
  bool followLocalMuzzle
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
  }, followLocalMuzzle, Weapon::MachineGun, fire.visualSeed);
  // The flash shares the tracer's deduplicated fire event and muzzle-follow
  // metadata, so it cannot repeat when the same snapshot is rendered twice.
  store.add({
    visualStart,
    visualStart + direction * 0.16F,
    0.0F,
    0.045F,
    0.045F,
    {255, 188, 76, 235},
    fire.visualSeed,
    TracerStyle::MachineGunMuzzleFlash,
  }, followLocalMuzzle, Weapon::MachineGun, fire.visualSeed);
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
  const Vec3 muzzle = rocketLauncherMuzzleSocket();
  const float mechanicalAmount = playerIndex < remotePlayers.size()
    ? remotePlayers[playerIndex].rocketLauncherMechanicalAmount
    : 0.0F;
  return remoteWeaponPresentationPoint(
    fire.start,
    remotePlayers,
    playerIndex,
    Weapon::RocketLauncher,
    settings,
    muzzle.x - 0.052F * mechanicalAmount,
    muzzle.y,
    muzzle.z
  );
}

void spawnShotgunTracers(
  TransientTracerStore& store,
  const Arena& arena,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  bool followLocalMuzzle
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
    }, followLocalMuzzle, Weapon::Shotgun, fire.visualSeed);
  }
}

void spawnRocketLauncherMuzzleFlash(
  TransientTracerStore& store,
  const WeaponFireResult& fire,
  Vec3 visualStart,
  bool followLocalMuzzle
) {
  const Vec3 direction = normalize(fire.end - fire.start);
  if (length(direction) <= 0.0001F) {
    return;
  }
  store.add({
    visualStart,
    visualStart + direction * 0.28F,
    0.0F,
    0.095F,
    0.090F,
    {255, 112, 28, 245},
    fire.visualSeed,
    TracerStyle::RocketLauncherMuzzleFlash,
  }, followLocalMuzzle, Weapon::RocketLauncher, fire.visualSeed);
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
  bool followLocalMuzzle
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
  }, followLocalMuzzle, Weapon::Revolver, fire.visualSeed);
}

void consumeTracerWeaponFires(
  TransientTracerStore& store,
  const Arena& arena,
  const PlayerState& localPlayer,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const LocalTracerAimHistory& localAimHistory,
  const RenderSettings& settings
) {
  for (std::size_t playerIndex = 0; playerIndex < weaponFires.size(); ++playerIndex) {
    const WeaponFireResult& fire = weaponFires[playerIndex];
    if (
      !fire.fired ||
      (
        fire.weapon != Weapon::MachineGun &&
        fire.weapon != Weapon::Shotgun &&
        fire.weapon != Weapon::Revolver &&
        fire.weapon != Weapon::RocketLauncher
      )
    ) {
      continue;
    }
    const std::uint8_t eventPlayer = static_cast<std::uint8_t>(playerIndex);
    if (store.consumed(eventPlayer, fire.weapon, fire.visualSeed)) {
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
      const WeaponFireResult visualFire = localEvent
        ? localPerspectiveTracerFire(
            arena,
            fire,
            visualStart,
            localPlayer,
            localAimHistory
          )
        : fire;
      spawnMachineGunTracer(
        store,
        visualFire,
        visualStart,
        localEvent
      );
    } else if (fire.weapon == Weapon::Shotgun) {
      const Vec3 visualStart = shotgunTracerSource(
        fire,
        localPlayer,
        remotePlayers,
        playerIndex,
        settings
      );
      const WeaponFireResult visualFire = localEvent
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
        localEvent
      );
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
        localEvent
      );
    } else {
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
        localEvent
      );
    }
    store.remember(eventPlayer, fire.weapon, fire.visualSeed);
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
  store.addEffect({TransientEffectType::RocketExplosionFlash, explosion.position, 0.0F, 0.05F, radius * 0.28F, radius * 0.68F, {255, 228, 132, 230}, seed});
  store.addEffect({TransientEffectType::RocketExplosionCore, explosion.position, 0.0F, 0.18F, radius * 0.26F, radius * 1.08F, {255, 112, 44, 200}, seed + 1U});
  store.addEffect({TransientEffectType::RocketExplosionHalo, explosion.position, 0.0F, 0.12F, radius * 0.70F, radius * 1.45F, {255, 72, 28, 82}, seed + 2U});
}

void consumeExplosionEvents(
  TransientTracerStore& store,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& explosions
) {
  for (std::size_t owner = 0; owner < explosions.size(); ++owner) {
    const RocketExplosionResult& explosion = explosions[owner];
    if (!explosion.active) {
      continue;
    }
    const std::uint8_t eventOwner = static_cast<std::uint8_t>(owner);
    if (store.consumedExplosion(eventOwner, explosion.sequence)) {
      continue;
    }
    spawnExplosionEffects(store, explosion);
    store.rememberExplosion(eventOwner, explosion.sequence);
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
  sample.dynamicOpaqueVertices = renderDiagnostics.dynamicOpaqueVertices;
  sample.dynamicTranslucentVertices =
    renderDiagnostics.dynamicTranslucentVertices;
  sample.totalUploadedVertices = renderDiagnostics.totalUploadedVertices;
  sample.dynamicTriangles = renderDiagnostics.dynamicTriangles;
  sample.worldSourceTriangles = renderDiagnostics.worldSourceTriangles;
  sample.worldRenderedTriangles = renderDiagnostics.worldRenderedTriangles;
  sample.worldDuplicateTrianglesCulled =
    renderDiagnostics.worldDuplicateTrianglesCulled;
  sample.worldVertexCount = renderDiagnostics.worldVertexCount;
  sample.worldDrawCalls = renderDiagnostics.worldDrawCalls;
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
    "dynamic: %u vertices | %u triangles",
    latest.dynamicOpaqueVertices + latest.dynamicTranslucentVertices,
    latest.dynamicTriangles
  );
  hud.topLeftLines.emplace_back(text);
  std::snprintf(
    text,
    sizeof(text),
    "world: tris %u->%u | vertices %u | draws %u | depth %u-bit | dup culled %u",
    latest.worldSourceTriangles,
    latest.worldRenderedTriangles,
    latest.worldVertexCount,
    latest.worldDrawCalls,
    latest.gpuDepthBits,
    latest.worldDuplicateTrianglesCulled
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
  std::string pendingMessage;
  std::deque<Message> history;
  std::uint32_t lastSequence = 0;
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

struct SettingsMenuState {
  bool open = false;
  int selectedRow = 0;
  VideoSettings pendingVideo = {};
  int pendingMaxFps = 0;
  VideoSettings originalVideo = {};
  int originalMaxFps = 0;
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
    menu.pendingMaxFps != menu.originalMaxFps;
}

void syncSettingsMenuFromConsole(SettingsMenuState& menu, const ConsoleSystem& console) {
  menu.pendingVideo = videoSettingsFromConsole(console);
  menu.pendingMaxFps = console.getInt("r_maxfps");
  menu.originalVideo = menu.pendingVideo;
  menu.originalMaxFps = menu.pendingMaxFps;
  menu.selectedRow = std::clamp(menu.selectedRow, 0, 7);
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
  menu.originalVideo = menu.pendingVideo;
  menu.originalMaxFps = menu.pendingMaxFps;
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
    settingsMenuItem(
      menu,
      6,
      "Apply changes",
      settingsChanged(menu) ? "Enter" : "No changes",
      settingsChanged(menu),
      true
    ),
    settingsMenuItem(
      menu,
      7,
      "Close / Revert draft",
      "Esc",
      false,
      true
    ),
  };
  hud.settingsFooter =
    "Up/Down select   Left/Right change   Enter apply   Esc close";
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

RenderSettings renderSettings(const ConsoleSystem& console) {
  RenderSettings settings;
  settings.fieldOfView = console.getFloat("cl_fov");
  settings.healthTextScale = console.getFloat("cl_health_size");
  settings.healthStyle = console.getInt("cl_health_style");
  settings.speedTextScale = console.getFloat("cl_speed_size");
  settings.weaponBarScale = console.getFloat("cl_weapon_bar_size");
  settings.fpsTextScale = console.getFloat("cl_showfps_size");
  settings.uiFont = console.getString("r_ui_font");
  settings.frustumCullRemotePlayers = console.getBool("r_frustum_cull");
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
  settings.playerOutlineStyle = static_cast<PlayerOutlineStyle>(
    console.getInt("r_player_outline_style")
  );
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

float zoomSensitivityMultiplier(
  float baseFieldOfView,
  float zoomFieldOfView,
  float manualMultiplier
) {
  if (manualMultiplier > 0.0F) {
    return manualMultiplier;
  }

  constexpr float sensRatio = 1.0F;
  const float baseHalfAngle = baseFieldOfView * 0.5F * kDegreesToRadians;
  const float zoomHalfAngle = zoomFieldOfView * 0.5F * kDegreesToRadians;
  const float baseTangent = std::tan(baseHalfAngle);
  if (std::fabs(baseTangent) <= 0.0001F) {
    return 1.0F;
  }
  return (1.0F / sensRatio) * (std::tan(zoomHalfAngle) / baseTangent);
}

MouseAimSettings mouseAimSettingsFromConsole(
  const ConsoleSystem& console,
  bool zoomHeld
) {
  return {
    console.getFloat("sensitivity"),
    zoomHeld
      ? zoomSensitivityMultiplier(
          console.getFloat("cl_fov"),
          console.getFloat("cl_zoom_fov"),
          console.getFloat("cl_zoom_sensitivity")
        )
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
  return renderState;
}

void clearConsoleSelection(ClientConsoleState& state) {
  state.hasSelection = false;
  state.selecting = false;
  state.selectionAnchor = 0;
  state.selectionFocus = 0;
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

std::string consoleClipboardTextForWindow(
  SDL_Window* window,
  const ClientConsoleState& state
) {
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
  hud.chatInputOpen = state.inputOpen;
  hud.chatInput = state.input;
  hud.chatCursorIndex = state.cursorIndex;
  hud.chatHasSelection = hasSelection(state.selection);
  hud.chatSelectionAnchor = state.selection.anchor;
  hud.chatSelectionFocus = state.selection.focus;
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

std::string chatClipboardText(const ClientChatState& state) {
  if (hasSelection(state.selection)) {
    return selectedText(state.input, state.selection);
  }
  return state.input;
}

void pasteClipboardTextIntoChat(ClientChatState& state) {
  char* clipboardText = SDL_GetClipboardText();
  if (clipboardText == nullptr) {
    return;
  }
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
  const std::size_t offset = chatInputOffsetAt(layout, state.input, x, y);
  state.cursorIndex = offset;
  state.selection.active = true;
  state.selection.anchor = offset;
  state.selection.focus = offset;
  state.selecting = true;
}

void updateChatSelection(
  SDL_Window* window,
  ClientChatState& state,
  float x,
  float y
) {
  if (!state.selecting) {
    return;
  }
  const ChatTextLayout layout = chatLayoutForWindow(window, state);
  state.selection.focus = chatInputOffsetAt(layout, state.input, x, y);
  state.cursorIndex = state.selection.focus;
}

void beginConsoleSelection(
  SDL_Window* window,
  ClientConsoleState& state,
  float x,
  float y
) {
  const ConsoleTextLayout layout = consoleLayoutForWindow(window, state);
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
  if (!state.selecting) {
    return;
  }
  state.selectionFocus =
    consoleTextOffsetAt(consoleLayoutForWindow(window, state), x, y);
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
  (void)bindings.bind("z", "showchat");
  (void)bindings.bind("tab", "+scores");
  (void)bindings.bind("f10", "settings");
  (void)bindings.bind("f12", "quit");
}

std::string gameModeName(GameMode gameMode) {
  switch (gameMode) {
  case GameMode::Duel:
    return "DUEL";
  case GameMode::ClanArena:
    return "CLAN ARENA";
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

HudRenderState buildHud(const ClientSession& session, bool showAliveCounts) {
  HudRenderState hud;
  hud.centerLines.push_back(session.statusMessage());
  if (!session.readyForPlay()) {
    return hud;
  }

  const ClientGame& client = *session.game();
  const ServerSnapshot& snapshot = client.snapshot();
  const std::size_t localPlayerIndex = session.playerIndex();
  const std::size_t remotePlayerIndex =
    opponentPlayerIndex(snapshot, localPlayerIndex);
  std::size_t occupiedCount = 0;
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (snapshot.connectedPlayers[index] || snapshot.botPlayers[index]) {
      ++occupiedCount;
    }
  }

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
  hud.centerLines.clear();
  hud.bottomCenterLines.push_back(
    "HEALTH " + std::to_string(snapshot.players[localPlayerIndex].health)
  );
  hud.topLeftLines.push_back(
    "PLAYERS " + std::to_string(occupiedCount) + '/' +
    std::to_string(kDuelPlayerCount)
  );
  if (snapshot.matchPhase != MatchPhase::Live) {
    hud.topLeftLines.push_back("MODE " + gameModeName(snapshot.gameMode));
    if (snapshot.gameMode == GameMode::ClanArena) {
      hud.topLeftLines.push_back(
        "TEAM " + teamName(snapshot.teams[localPlayerIndex])
      );
    }
  }
  if (showAliveCounts && snapshot.gameMode == GameMode::ClanArena) {
    hud.topRightLines.push_back(aliveCountLine(snapshot));
  }
  hud.topCenterLines.push_back(hudScoreLine(snapshot, localPlayerIndex));
  if (snapshot.matchRules.timeLimitMinutes > 0) {
    const std::uint32_t limitTicks =
      static_cast<std::uint32_t>(snapshot.matchRules.timeLimitMinutes) * 60U * 125U;
    const std::uint32_t remainingTicks =
      snapshot.liveTicksElapsed < limitTicks
      ? limitTicks - snapshot.liveTicksElapsed
      : 0U;
    const std::uint32_t remainingSeconds = remainingTicks / 125U;
    hud.topRightLines.push_back(
      "TIME " + std::to_string(remainingSeconds / 60U) + ':' +
      (remainingSeconds % 60U < 10U ? "0" : "") +
      std::to_string(remainingSeconds % 60U)
    );
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
  Weapon weapon
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
  command.weapon = weapon;
  return command;
}

[[nodiscard]] UserCommand buildCommandWithViewAngles(
  const LocalInputState& input,
  std::uint32_t sequence,
  std::uint32_t clientTick,
  float yawRadians,
  float pitchRadians,
  Weapon weapon
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
  command.weapon = weapon;
  return command;
}
#endif

} // namespace

GameApp::GameApp(std::string serverHost, std::uint16_t serverPort)
  : serverHost_(std::move(serverHost)), serverPort_(serverPort) {}

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
  bool quitRequested = false;
  bool clearRequested = false;
  bool writeConfigRequested = false;
  bool toggleConsoleRequested = false;
  bool settingsMenuRequested = false;
  bool openChatRequested = false;
  bool showChatRequested = false;
  bool requestGameModePending = false;
  bool requestTeamPending = false;
  GameMode requestedGameMode = GameMode::Duel;
  Team requestedTeam = Team::None;
  int scoreboardPressCount = 0;
  int zoomPressCount = 0;
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
  registerButtonCommand("zoom", zoomPressCount);

  console.registerCommand(
    "weapon",
    "Select weapon: weapon <mg|sg|gl|rl|lg|rg|pg|fg|1..8>.",
    [&selectedWeapon](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: weapon <mg|sg|gl|rl|lg|rg|pg|fg|1..8>");
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
    "gamemode",
    "Select the active gamemode: gamemode <duel|ca|clanarena>.",
    [&requestGameModePending, &requestedGameMode](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: gamemode <duel|ca|clanarena>");
      }
      std::string value = arguments[1];
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (value == "duel") {
        requestedGameMode = GameMode::Duel;
      } else if (value == "ca" || value == "clanarena" || value == "clan_arena") {
        requestedGameMode = GameMode::ClanArena;
      } else {
        return std::string("usage: gamemode <duel|ca|clanarena>");
      }
      requestGameModePending = true;
      return std::string("gamemode = ") + gameModeName(requestedGameMode);
    }
  );
  console.registerCommand(
    "team",
    "Select your Clan Arena team: team <red|blue|none>.",
    [&requestTeamPending, &requestedTeam](const std::vector<std::string>& arguments) {
      if (arguments.size() != 2) {
        return std::string("usage: team <red|blue|none>");
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
      } else {
        return std::string("usage: team <red|blue|none>");
      }
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
    [](const std::vector<std::string>&) {
      return std::string(
        "+forward\n"
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
        "+zoom\n"
        "weapon\n"
        "map\n"
        "player\n"
        "resetmatch\n"
        "ready\n"
        "gamemode\n"
        "team\n"
        "bot_add\n"
        "bot_kick\n"
        "bot_attack\n"
        "bot_dodge\n"
        "bot_dodge_min_ms\n"
        "bot_dodge_max_ms\n"
        "bot_stare\n"
        "bot_standstill\n"

        "settings\n"
        "messagemode\n"
        "showchat\n"
        "toggleconsole\n"
        "quit"
      );
    }
  );
  console.registerCommand(
    "net_stats",
    "Print current connection diagnostics.",
    [&session](const std::vector<std::string>&) {
      const ClientNetworkSimulationConfig config = session.networkSimulationConfig();
      const ClientNetworkSimulationStats stats = session.networkSimulationStats();
      const SnapshotInterpolation::Diagnostics interpolation =
        session.game() != nullptr
          ? session.game()->interpolationDiagnostics()
          : SnapshotInterpolation::Diagnostics{};
      char text[512];
      std::snprintf(
        text,
        sizeof(text),
        "state=%d host=%s port=%u player=%zu ping=%.1fms sim={lat=%dms jit=%dms loss=%d%% reorder=%d%% seed=%u qout=%zu qin=%zu drop=%llu/%llu reorder=%llu/%llu} interp={lead=%.2fms error=%.2fms rate=%.3f started=%d underrun=%d/%u hard=%u buffered=%zu tick=%.3f newest=%.0f}",
        static_cast<int>(session.state()),
        std::string(session.host()).c_str(),
        static_cast<unsigned int>(session.port()),
        session.playerIndex() + 1U,
        session.pingMilliseconds(),
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
        interpolation.timelineErrorTicks * 1000.0 / static_cast<double>(kFixedTickRate),
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
  if (std::filesystem::exists(defaultConfigPath)) {
    loadClientConfig(console, defaultConfigPath.string());
  } else {
    installDefaultBindings(bindings);
    std::cerr << "Config warning: config/default_client.cfg not found; using code default binds\n";
  }
  loadClientConfig(console, configPath);
  loadSoundMixerConfigs(console, assetBasePath);
  if (console.getInt("cl_config_version") < 7) {
    (void)bindings.bind("f3", "ready");
    (void)bindings.bind("t", "messagemode");
    (void)bindings.bind("z", "showchat");
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
  (void)session.connect(serverHost_, serverPort_);
  ClientConsoleState consoleState;
  SettingsMenuState settingsMenu;
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
        clearChatSelection(chatState);
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
  const auto setSettingsOpen =
    [&bindings, &console, &settingsMenu, &input, window](bool open) {
      if (settingsMenu.open == open) {
        return;
      }
      for (const std::string& command : bindings.releaseAll()) {
        (void)console.execute(command);
      }
      settingsMenu.open = open;
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
      if (open) {
        syncSettingsMenuFromConsole(settingsMenu, console);
        SDL_SetWindowRelativeMouseMode(window, false);
      } else {
        SDL_SetWindowRelativeMouseMode(window, true);
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
  std::array<PlayerPresentationState, kDuelPlayerCount> playerPresentationStates = {};
  ViewModelPresentationController viewModelPresentation;
  ClientGame* presentationViewGame = nullptr;
  bool previousFrameUsedPresentationView = false;
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
  std::array<std::uint32_t, kMaxRocketProjectiles> lastPlayedGrenadeBounceAudioSequences = {};
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
  std::array<WeaponFireResult, kDuelPlayerCount> lastRocketLauncherResponseFire = {};
  std::array<bool, kDuelPlayerCount> hasLastRocketLauncherResponseFire = {};
  std::array<PlasmaGunFiringResponseState, kDuelPlayerCount>
    plasmaGunFiringResponse = {};
  std::array<WeaponFireResult, kDuelPlayerCount> lastPlasmaGunResponseFire = {};
  std::array<bool, kDuelPlayerCount> hasLastPlasmaGunResponseFire = {};
  KillFeedState killFeedState;
  TransientTracerStore transientTracerStore;
  LocalTracerAimHistory localTracerAimHistory;
  std::vector<TransientTracer> activeTransientTracers;
  std::vector<TransientEffect> activeTransientEffects;
  activeTransientTracers.reserve(kMaxTransientTracers);
  activeTransientEffects.reserve(kMaxTransientEffects);
  std::array<FootstepAudioState, kDuelPlayerCount> footstepAudioStates = {};

  while (running) {
    const auto outerFrameStart = Clock::now();
    const auto outerFrameElapsed =
      std::chrono::duration<float>(outerFrameStart - previousOuterFrameStart);
    previousOuterFrameStart = outerFrameStart;
    const float outerFrameMilliseconds = outerFrameElapsed.count() * 1000.0F;
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
            selectAll(chatState.input, chatState.selection);
            chatState.cursorIndex = chatState.input.size();
          } else if (isClipboardPasteKey(event.key)) {
            pasteClipboardTextIntoChat(chatState);
          } else if (isClipboardCopyKey(event.key)) {
            copyTextToClipboard(chatClipboardText(chatState));
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            chatState.input.clear();
            chatState.cursorIndex = 0U;
            clearChatSelection(chatState);
            setChatOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            backspaceSelectionOrText(
              chatState.input,
              chatState.cursorIndex,
              chatState.selection
            );
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            clearChatSelection(chatState);
            moveCursorLeft(chatState.input, chatState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            clearChatSelection(chatState);
            moveCursorRight(chatState.input, chatState.cursorIndex);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (!chatState.input.empty()) {
              chatState.pendingMessage = chatState.input;
            }
            chatState.input.clear();
            chatState.cursorIndex = 0U;
            clearChatSelection(chatState);
            setChatOpen(false);
          }
          break;
        }
        if (settingsMenu.open) {
          if (!pressed) {
            break;
          }
          if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setSettingsOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_UP) {
            settingsMenu.selectedRow = (settingsMenu.selectedRow + 7) % 8;
          } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
            settingsMenu.selectedRow = (settingsMenu.selectedRow + 1) % 8;
          } else if (event.key.scancode == SDL_SCANCODE_LEFT) {
            adjustSettingsMenuValue(settingsMenu, -1);
          } else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
            adjustSettingsMenuValue(settingsMenu, 1);
          } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
            if (settingsMenu.selectedRow == 6) {
              applySettingsMenu(console, settingsMenu);
            } else if (settingsMenu.selectedRow == 7) {
              setSettingsOpen(false);
            } else {
              adjustSettingsMenuValue(settingsMenu, 1);
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
          if (isClipboardPasteKey(event.key)) {
            clearConsoleSelection(consoleState);
            pasteClipboardTextIntoConsole(consoleState.input, consoleState.cursorIndex);
          } else if (isClipboardCopyKey(event.key)) {
            copyTextToClipboard(consoleClipboardTextForWindow(window, consoleState));
          } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            setConsoleOpen(false);
          } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (!consoleState.input.empty()) {
              clearConsoleSelection(consoleState);
              backspaceConsoleInput(consoleState.input, consoleState.cursorIndex);
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
          } else if (event.key.scancode == SDL_SCANCODE_UP && !consoleState.history.empty()) {
            if (consoleState.historyIndex > 0) {
              --consoleState.historyIndex;
            }
            clearConsoleSelection(consoleState);
            consoleState.input = consoleState.history[consoleState.historyIndex];
            consoleState.cursorIndex = consoleState.input.size();
          } else if (event.key.scancode == SDL_SCANCODE_DOWN && !consoleState.history.empty()) {
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
        if (openChatRequested && !consoleState.open && !settingsMenu.open) {
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
          clearConsoleSelection(consoleState);
          insertConsoleText(
            consoleState.input,
            consoleState.cursorIndex,
            event.text.text
          );
        } else if (chatState.inputOpen) {
          suppressNextTextInput = false;
          replaceSelectionOrInsert(
            chatState.input,
            chatState.cursorIndex,
            chatState.selection,
            event.text.text,
            TextInputFilter::Chat,
            kMaxChatMessageBytes
          );
        } else if (settingsMenu.open) {
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
            if (consoleState.selectionAnchor == consoleState.selectionFocus) {
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
            if (chatState.selection.anchor == chatState.selection.focus) {
              clearChatSelection(chatState);
            }
          }
        } else if (settingsMenu.open) {
          break;
        } else if (!consoleState.open && !chatState.inputOpen) {
          executeBindingCommands(bindings.handleKey(key, pressed));
          applyConsoleToggle();
          applySettingsMenuToggle();
        } else if (!pressed) {
          executeBindingCommands(bindings.handleKey(key, false));
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION:
        if (settingsMenu.open) {
          input.mouseDeltaX = 0.0F;
          input.mouseDeltaY = 0.0F;
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
        } else {
          input.mouseDeltaX += event.motion.xrel;
          input.mouseDeltaY += event.motion.yrel;
        }
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        executeBindingCommands(bindings.releaseAll());
        consoleState.selecting = false;
        chatState.selecting = false;
        input.mouseDeltaX = 0.0F;
        input.mouseDeltaY = 0.0F;
        break;
      default:
        break;
      }
    }

    if (clearRequested) {
      consoleState.output.clear();
      clearRequested = false;
    }
    if (showChatRequested) {
      chatState.visibleUntil = Clock::now() + std::chrono::seconds(5);
      showChatRequested = false;
    }
    if (settingsMenuRequested) {
      if (consoleState.open) {
        setConsoleOpen(false);
      }
      if (chatState.inputOpen) {
        setChatOpen(false);
      }
      applySettingsMenuToggle();
    }
    if (const ClientGame* chatGame = session.game();
        chatGame != nullptr && chatGame->hasSnapshot()) {
      const ServerSnapshot& snapshot = chatGame->snapshot();
      if (
        snapshot.chatSequence != 0U &&
        snapshot.chatSequence != chatState.lastSequence
      ) {
        chatState.lastSequence = snapshot.chatSequence;
        chatState.history.push_back(ClientChatState::Message{
          snapshot.chatPlayerIndex,
          snapshot.chatMessage,
          chatPlayerDisplayName(snapshot, snapshot.chatPlayerIndex),
        });
        while (chatState.history.size() > 8U) {
          chatState.history.pop_front();
        }
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
    session.setNetworkSimulationConfig(networkSimulationConfigFromConsole(console));
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
    const bool gameInputControlsView =
      usePresentationView && !consoleState.open && !chatState.inputOpen &&
      !settingsMenu.open;
    const bool wantsRelativeMouse =
      !consoleState.open && !chatState.inputOpen && !settingsMenu.open;

    if (wantsRelativeMouse != relativeMouseModeEnabled) {
      SDL_SetWindowRelativeMouseMode(window, wantsRelativeMouse);
      relativeMouseModeEnabled = wantsRelativeMouse;
    }
    ClientGame* currentPresentationGame = session.game();
    if (currentPresentationGame == nullptr) {
      presentationView = {};
      presentationViewGame = nullptr;
      previousFrameUsedPresentationView = usePresentationView;
    } else if (currentPresentationGame != presentationViewGame) {
      // A new ClientGame represents a new connection/prediction timeline; do
      // not carry view initialization or mouse state across that authority reset.
      presentationView = {};
      playerPresentationStates = {};
      viewModelPresentation.reset();
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
    const float viewModelMouseDeltaX = gameInputControlsView ? input.mouseDeltaX : 0.0F;
    const float viewModelMouseDeltaY = gameInputControlsView ? input.mouseDeltaY : 0.0F;
    if (gameInputControlsView && presentationView.initialized) {
      const MouseAimSettings mouseAimSettings =
        mouseAimSettingsFromConsole(console, zoomPressCount > 0);
      const MouseAimDelta mouseAimDelta = quakeLiveMouseAimDelta(
        input.mouseDeltaX,
        input.mouseDeltaY,
        outerFrameElapsed.count(),
        mouseAimSettings
      );
      presentationView.yawRadians -= mouseAimDelta.yawRadians;
      presentationView.pitchRadians = clamp(
        presentationView.pitchRadians - mouseAimDelta.pitchRadians,
        -kMaxPitchRadians,
        kMaxPitchRadians
      );
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
    if (const ClientGame* weaponGame = session.game();
        weaponGame != nullptr && weaponGame->hasSnapshot()) {
      displayedSelectedWeapon =
        weaponGame->snapshot().selectedWeapons[session.playerIndex()];
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
      LocalInputState tickInput = input;
      if (consumedMouseForTick) {
        // SDL reports one mouse delta per rendered frame. Apply it to only the
        // first catch-up command or low frame rates would multiply the turn.
        tickInput.mouseDeltaX = 0.0F;
        tickInput.mouseDeltaY = 0.0F;
      }
      const PlayerState& predictedPlayer = client->predictedPlayer();

      const MouseAimSettings mouseAimSettings =
        mouseAimSettingsFromConsole(console, zoomPressCount > 0);

      const UserCommand command =
        usePresentationView && presentationView.initialized
          ? buildCommandWithViewAngles(
              tickInput,
              commandSequence++,
              clientTick++,
              presentationView.yawRadians,
              presentationView.pitchRadians,
              selectedWeapon
            )
          : buildCommand(
              tickInput,
              predictedPlayer,
              commandSequence++,
              clientTick++,
              mouseAimSettings,
              elapsed.count(),
              selectedWeapon
            );
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
        botCommandForPacket.maxIntervalMs
      );
      if (!sentPlayerName.empty()) {
        lastSentPlayerName = sentPlayerName;
      }
      chatState.pendingMessage.clear();
      pendingPlayerName.clear();
      pendingMapName.clear();
      resetRequested = false;
      readyRequested = false;
      requestGameModePending = false;
      requestTeamPending = false;
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
      consumedMouseForTick = true;
    }
    if (consumedMouseForTick) {
      input.mouseDeltaX = 0.0F;
      input.mouseDeltaY = 0.0F;
    }

    const ClientGame* currentAudioGame = session.game();
    if (currentAudioGame != audioGame) {
      audioGame = currentAudioGame;
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
      lastRocketLauncherResponseFire = {};
      hasLastRocketLauncherResponseFire = {};
      plasmaGunFiringResponse = {};
      lastPlasmaGunResponseFire = {};
      hasLastPlasmaGunResponseFire = {};
      resetKillFeedState(killFeedState);
      transientTracerStore = TransientTracerStore{};
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
      currentAudioGame->hasSnapshot()
    ) {
      const ServerSnapshot& audioSnapshot = currentAudioGame->snapshot();
      if (
        !audioStateInitialized ||
        audioSnapshot.serverTick != lastAudioServerTick
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const bool localPlayerAlive =
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
              lastDamageNumberFeedbackSequences[localPlayerIndex];
            const std::uint32_t previousFeedbackSequence =
              newestFeedbackSequence;
            const bool hadFeedbackSequence =
              hasLastDamageNumberFeedbackSequence[localPlayerIndex];
            bool consumedFeedback = false;
            for (
              const LocalHitFeedbackEvent& feedback :
              audioSnapshot.localHitFeedbackEvents[localPlayerIndex]
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
                static_cast<std::uint8_t>(localPlayerIndex),
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
                !hasLastDamageNumberFeedbackSequence[localPlayerIndex] ||
                isSequenceNewer(feedback.sequence, newestFeedbackSequence)
              ) {
                newestFeedbackSequence = feedback.sequence;
              }
            }
            lastDamageNumberFeedbackSequences[localPlayerIndex] =
              newestFeedbackSequence;
            hasLastDamageNumberFeedbackSequence[localPlayerIndex] =
              hadFeedbackSequence || consumedFeedback;
          } else {
            bool foundFeedbackSequence = false;
            std::uint32_t newestFeedbackSequence = 0;
            for (
              const LocalHitFeedbackEvent& feedback :
              audioSnapshot.localHitFeedbackEvents[localPlayerIndex]
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
              lastDamageNumberFeedbackSequences[localPlayerIndex] =
                newestFeedbackSequence;
              hasLastDamageNumberFeedbackSequence[localPlayerIndex] = true;
            }
          }
          damageNumberStateInitialized = true;
          lastDamageNumberServerTick = audioSnapshot.serverTick;
        }

        const bool localHit =
          audioSnapshot.lightningGuns[localPlayerIndex].hit;
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
          const SpatialAudio painAudio = playerIndex == localPlayerIndex
            ? SpatialAudio{painVolume, 0.0F}
            : worldAudio(painVolume, player.position, currentAudioGame->predictedPlayer());
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
            audioSnapshot.lightningGuns[localPlayerIndex].damageApplied,
            audioSnapshot.lightningGuns[localPlayerIndex].headshot
          );
          lastHitSoundServerTick = audioSnapshot.serverTick;
        }
        if (audioStateInitialized) {
          updateFootstepAudio(
            footstepAudioStates[localPlayerIndex],
            currentAudioGame->predictedPlayer(),
            currentAudioGame->predictedPlayer(),
            true,
            soundEnabled ? footstepVolume : 0.0F,
            audio
          );
          if (soundEnabled) {
            for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
              if (playerIndex == localPlayerIndex) {
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
                  currentAudioGame->predictedPlayer()
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
                  currentAudioGame->predictedPlayer()
                );
              audio.playGrenadeBounce(spatial.volume * 0.5F, spatial.pan);
              lastPlayedGrenadeBounceAudioSequences[eventIndex] = event.sequence;
            }
          }
        }
        if (soundEnabled && audioStateInitialized) {
          const FragEvent& localFrag = audioSnapshot.fragEvents[localPlayerIndex];
          if (
            localFrag.active &&
            localFrag.targetPlayerIndex != localPlayerIndex &&
            shouldPlaySnapshotAudioEvent(
              hasLastPlayedFragEvent[localPlayerIndex],
              sameFragEvent(localFrag, lastPlayedFragEvents[localPlayerIndex]),
              audioSnapshot.serverTick,
              lastPlayedFragAudioTicks[localPlayerIndex],
              kTransientAudioEventTicks
            )
          ) {
            audio.playFrag(fragVolume);
            lastPlayedFragEvents[localPlayerIndex] = localFrag;
            lastPlayedFragAudioTicks[localPlayerIndex] = audioSnapshot.serverTick;
            hasLastPlayedFragEvent[localPlayerIndex] = true;
          }

          for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {

            const WeaponFireResult& fire = audioSnapshot.weaponFires[playerIndex];
            const bool localWeaponEvent = playerIndex == localPlayerIndex;
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
              if (fireAudio.cue == WeaponFireAudioCue::Railgun) {
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
              const SpatialAudio weaponFireAudio = localWeaponEvent
                ? SpatialAudio{weaponFireVolume, 0.0F}
                : worldAudio(
                  weaponFireVolume,
                  fire.start,
                  currentAudioGame->predictedPlayer()
                );
              if (fireAudio.cue == WeaponFireAudioCue::Railgun) {
                audio.playRailFire(weaponFireAudio.volume, weaponFireAudio.pan);
                if (fireAudio.startsLocalRailCooldown) {
                  lastLocalRailFireTick = audioSnapshot.serverTick;
                  hasLocalRailFireTick = true;
                  localRailReadySoundPlayed = false;
                }
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
                  currentAudioGame->predictedPlayer()
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
            (displayedSelectedWeapon == Weapon::Railgun ||
             displayedSelectedWeapon == Weapon::Revolver) &&
            audioSnapshot.serverTick - lastLocalRailFireTick >=
              kClientRailgunCooldownTicks
          ) {
            audio.playRailReady(soundVolume("s_rg_ready_volume"));
            localRailReadySoundPlayed = true;
          }
        }
        if (
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
        currentAudioGame->predictedPlayer().health > 0
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
        const ServerSnapshot& snapshot = currentAudioGame->snapshot();
        const float masterVolume =
          console.getFloat("s_volume") * console.getFloat("s_lg_fire_volume");
        if (snapshot.lightningGuns[localPlayerIndex].active) {
          lightningGunVolume = masterVolume;
          lightningGunPan = 0.0F;
        }
        for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
          if (
            playerIndex == localPlayerIndex ||
            !snapshot.lightningGuns[playerIndex].active ||
            snapshot.players[playerIndex].health <= 0
          ) {
            continue;
          }
          const SpatialAudio spatial = worldAudio(
              masterVolume,
              snapshot.players[playerIndex].position,
              currentAudioGame->predictedPlayer()
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
      interpolationGame->advanceInterpolation(
        elapsed.count(),
        console.getFloat("cl_interp")
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
      if (
        console.getBool("cl_show_net") &&
        titleClient != nullptr &&
        titleClient->hasSnapshot()
      ) {
        const std::size_t localPlayerIndex = session.playerIndex();
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
    std::size_t renderLocalPlayerIndex = 0;
    if (const ClientGame* renderClient = session.game();
        renderClient != nullptr && renderClient->hasSnapshot()) {
      const std::size_t localPlayerIndex = session.playerIndex();
      renderLocalPlayerIndex = localPlayerIndex;
      renderPlayer = renderClient->predictedPlayer();
      const ServerSnapshot& renderSnapshot = renderClient->snapshot();
      if (
        localRenderPredictionSeconds > 0.0F &&
        renderPlayer.health > 0
      ) {
        const MouseAimSettings mouseAimSettings =
          mouseAimSettingsFromConsole(console, zoomPressCount > 0);
        const UserCommand visualCommand =
          usePresentationView && presentationView.initialized
            ? buildCommandWithViewAngles(
                input,
                commandSequence,
                clientTick,
                presentationView.yawRadians,
                presentationView.pitchRadians,
                selectedWeapon
              )
            : buildCommand(
                input,
                renderPlayer,
                commandSequence,
                clientTick,
                mouseAimSettings,
                elapsed.count(),
                selectedWeapon
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
        if (playerIndex == localPlayerIndex) {
          playerPresentationStates[playerIndex] = {};
          continue;
        }
        if (!renderSnapshot.participatingPlayers[playerIndex]) {
          playerPresentationStates[playerIndex] = {};
          continue;
        }
        if (
          renderSnapshot.gameMode == GameMode::ClanArena &&
          renderSnapshot.players[playerIndex].health <= 0
        ) {
          playerPresentationStates[playerIndex] = {};
          continue;
        }
        const bool teammate = playerPresentedAsTeammate(
          renderSnapshot,
          localPlayerIndex,
          playerIndex
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
        renderRemotePlayers[playerIndex].presentation = updatePlayerPresentation(
          playerPresentationStates[playerIndex],
          renderRemotePlayers[playerIndex].player,
          elapsed.count(),
          static_cast<std::uint32_t>(playerIndex),
          presentationConfig
        );
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
        renderSnapshot.lightningGuns[localPlayerIndex];
      renderWeaponFires = renderSnapshot.weaponFires;
      renderRocketExplosions = renderSnapshot.rocketExplosions;
      renderRockets = renderSnapshot.rockets;
      renderIcePools = renderSnapshot.icePools;
      const LocalHitFeedbackBatch hitFeedback =
        consumeLocalHitFeedbackEvents(
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
    if (usePresentationView && presentationView.initialized) {
      renderPlayer.viewYawRadians = presentationView.yawRadians;
      renderPlayer.viewPitchRadians = presentationView.pitchRadians;
    }
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      const bool localPlayer = playerIndex == renderLocalPlayerIndex;
      const bool motorDriven = localPlayer
        ? (
            input.attack > 0 &&
            displayedSelectedWeapon == Weapon::MachineGun &&
            renderPlayer.health > 0
          )
        : (
            renderRemotePlayers[playerIndex].visible &&
            renderWeaponFires[playerIndex].fired &&
            renderWeaponFires[playerIndex].weapon == Weapon::MachineGun
          );
      machineGunBarrelSpin[playerIndex].update(motorDriven, elapsed.count());
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
      machineGunBarrelSpin[renderLocalPlayerIndex].normalizedSpeed()
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
    RenderSettings currentRenderSettings = renderSettings(console);
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
        machineGunBarrelSpin[renderLocalPlayerIndex].normalizedSpeed()
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
        const bool newRailEvent =
          !lingeringRailBeam.active ||
          !sameWeaponFireEvent(sourceFire, lingeringRailBeam.sourceFire);
        if (newRailEvent) {
          if (localPerspectiveRail) {
            currentFire.start = currentRenderSettings.showOwnWeapons
              ? viewmodelMuzzlePosition(
                  renderPlayer,
                  currentFire.weapon,
                  currentRenderSettings.weaponPosition
                )
              : hiddenWeaponVisualOrigin(renderPlayer);
          } else {
            const bool revolver = currentFire.weapon == Weapon::Revolver;
            const Vec3 revolverMuzzle = revolverMuzzleSocket();
            currentFire.start = remoteWeaponPresentationPoint(
              sourceFire.start,
              renderRemotePlayers,
              playerIndex,
              currentFire.weapon,
              currentRenderSettings,
              revolver ? revolverMuzzle.x : 0.78F,
              revolver ? revolverMuzzle.y : 0.0F,
              revolver ? revolverMuzzle.z : 0.09F
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
            : kRailgunBeamLingerSeconds;
          lingeringRailBeam.expiresAt =
            now + std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<float>(lingerSeconds)
            );
        } else {
          currentFire = lingeringRailBeam.fire;
        }
      } else if (
        !currentFire.fired &&
        lingeringRailBeam.active &&
        now < lingeringRailBeam.expiresAt
      ) {
        currentFire = lingeringRailBeam.fire;
      } else {
        if (lingeringRailBeam.active && now >= lingeringRailBeam.expiresAt) {
          lingeringRailBeam.active = false;
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
        const float ageSeconds = std::chrono::duration<float>(
          now - lingeringRailBeam.startedAt
        ).count();
        if (revolverTracerPresentation(ageSeconds).followMuzzle) {
          const Vec3 followedStart = currentRenderSettings.showOwnWeapons
            ? firstPersonRevolverMuzzlePosition(
                renderPlayer,
                currentRenderSettings
              )
            : hiddenWeaponVisualOrigin(renderPlayer);
          lingeringRailBeam.fire.start = followedStart;
          currentFire.start = followedStart;
        }
      }
    }
    if (zoomPressCount > 0) {
      currentRenderSettings.fieldOfView = console.getFloat("cl_zoom_fov");
    }
    constexpr float kBeamPulseRadiansPerSecond = 31.4159265359F;
    const double presentationSeconds =
      std::chrono::duration<double>(now.time_since_epoch()).count();
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
    HudRenderState hud = buildHud(session, console.getBool("cl_show_alive_counts"));
    hud.selectedWeapon = displayedSelectedWeapon;
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
          " B | pose " +
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
        if (console.getInt("r_player_model") == 1) {
          std::string loadedAnimations = "gltf clips:";
          for (const std::string& name : duelistMaleModel().animationNames()) {
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
      session.game()->hasSnapshot()
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
        session.playerIndex()
      );
    }
    if (chatState.inputOpen || Clock::now() < chatState.visibleUntil) {
      for (const ClientChatState::Message& message : chatState.history) {
        hud.chatLines.push_back(HudRenderState::ChatLine{
          message.playerIndex,
          message.text,
          message.speakerName,
        });
      }
    }
    hud.chatInputOpen = chatState.inputOpen;
    hud.chatInput = chatState.input;
    hud.chatCursorIndex = chatState.cursorIndex;
    hud.chatHasSelection = hasSelection(chatState.selection);
    hud.chatSelectionAnchor = chatState.selection.anchor;
    hud.chatSelectionFocus = chatState.selection.focus;
    populateSettingsMenuRenderState(hud, settingsMenu);
    if (console.getBool("r_perf")) {
      appendPerfHudLines(
        hud,
        perfTelemetry.summarize(),
        console.getBool("r_perf_detail"),
        console
      );
    }
    transientTracerStore.update(outerFrameElapsed.count());
    consumeTracerWeaponFires(
      transientTracerStore,
      renderArena,
      renderPlayer,
      renderRemotePlayers,
      renderWeaponFires,
      localTracerAimHistory,
      currentRenderSettings
    );
    consumeExplosionEvents(transientTracerStore, renderRocketExplosions);
    transientTracerStore.fillActive(
      activeTransientTracers,
      renderPlayer,
      currentRenderSettings
    );
    transientTracerStore.fillActiveEffects(activeTransientEffects);
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
      consoleRenderState(consoleState)
    );
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
