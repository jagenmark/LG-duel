#include "app/AimTrainerApp.hpp"

#include "render/Renderer.hpp"
#include "shared/Constants.hpp"
#include "shared/FixedTick.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/MapRegistry.hpp"
#include "trainer/AimTrainerMenu.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace lg {
namespace {

[[nodiscard]] std::uint32_t trainerBalanceIdentity(const BalanceConfig& balance) {
  // This small identity changes when the common scoring-relevant weapon
  // balance changes. It keeps old scores off a new balance board.
  std::uint32_t result = std::bit_cast<std::uint32_t>(balance.lightningGun.fireHz);
  result ^= std::bit_cast<std::uint32_t>(balance.lightningGun.damagePerSecond) * 16777619U;
  result ^= static_cast<std::uint32_t>(balance.railgun.damage) * 2166136261U;
  result ^= static_cast<std::uint32_t>(balance.shotgun.damagePerPellet) << 11U;
  result ^= static_cast<std::uint32_t>(balance.machineGun.damage) << 19U;
  return result;
}

void stampScenario(AimTrainerMenu& menu, std::uint32_t mapIdentity, std::uint32_t balanceIdentity) {
  AimScenario draft = menu.draft();
  draft.mapName = "aim_trainer";
  draft.mapIdentity = mapIdentity;
  draft.balanceIdentity = balanceIdentity;
  menu.edit(std::move(draft));
}

void addTrainerHud(HudRenderState& hud, const AimTrainerMenu& menu) {
  const AimTrainerFrame& frame = menu.frame();
  const AimScenario& draft = menu.draft();
  hud.topLeftLines.push_back("AIM TRAINER  |  " + draft.name);
  hud.topLeftLines.push_back(
    "TIME " + std::to_string(frame.remainingTicks / kFixedTickRate) + "s  SCORE " +
    std::to_string(frame.stats.score) + "  ACC " +
    std::to_string(static_cast<int>(std::lround(frame.stats.accuracyPercent()))) + "%"
  );
  hud.topLeftLines.push_back(
    "DAMAGE " + std::to_string(frame.stats.damage) + "  CLEARS " +
    std::to_string(frame.stats.clears) + "  HITS " + std::to_string(frame.stats.hits) +
    "/" + std::to_string(frame.stats.attempts) + "  SPM " +
    std::to_string(static_cast<int>(std::lround(frame.stats.scorePerMinute(frame.elapsedTicks))))
  );
  hud.topRightLines.push_back(
    std::string("TARGET ") + (draft.groups[0].visual == AimTargetVisual::Orb ? "ORB" : "WORKER") +
    "  R " + std::to_string(draft.groups[0].radius)
  );
  hud.topRightLines.push_back(
    std::string("MOVE ") + (draft.playerMovement == AimPlayerMovement::Locked ? "LOCKED" : "NORMAL") +
    "  WEAPONS " + (draft.weaponPolicy == AimWeaponPolicy::All ? "ALL" : "FORCED")
  );
  hud.topRightLines.push_back(
    "LIFE " + std::to_string(static_cast<int>(draft.groups[0].life)) +
    "  SPAWN " + std::to_string(static_cast<int>(draft.groups[0].spawnMode)) +
    "  MOTION " + std::to_string(static_cast<int>(draft.groups[0].motion)) +
    "  GROUPS " + std::to_string(draft.groups.size())
  );
  hud.centerLines.push_back(
    frame.phase == AimTrainerPhase::Running ? "TRAINING" :
      frame.phase == AimTrainerPhase::Results ? frame.message : "ENTER: START"
  );
  hud.bottomCenterLines.push_back("UP/DOWN preset  ENTER start/repeat  0 all weapons  1-9 force weapon");
  hud.bottomCenterLines.push_back("O orb  W worker  L move  T life  P spawn  G motion  C colour  I ammo  K score  +/- duration  [/] radius");
  hud.bottomCenterLines.push_back("F3/F4 group +/-  H health  R respawn  ;/' speed  F2 save-as  F5 overwrite  DEL delete  BKSP abort");
  if (!menu.warning().empty()) hud.bottomCenterLines.push_back("STORAGE: " + menu.warning());
}

void mapTrainerVisuals(
  const AimTrainerFrame& frame,
  std::array<RemotePlayerView, kDuelPlayerCount>& workers,
  std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& projectiles,
  std::vector<TransientEffect>& orbEffects
) {
  workers = {};
  projectiles = {};
  orbEffects.clear();
  std::size_t workerIndex = 0;
  std::size_t projectileIndex = 0;
  for (const AimTargetView& target : frame.targets) {
    if (!target.active) continue;
    if (target.visual == AimTargetVisual::Worker && workerIndex < workers.size()) {
      // This adapter is rendering-only. The trainer owns its independent
      // Worker hit body; no remote player or network state exists here.
      RemotePlayerView& worker = workers[workerIndex++];
      worker.player = target.worker;
      worker.visible = true;
      worker.teammate = false;
      worker.name = "Worker target";
      continue;
    }
    if (target.visual == AimTargetVisual::Orb) {
      // The shared core mesh is a true 3D sphere. Its scale and RGB come
      // straight from the same target adapter that owns sphere hit tests.
      orbEffects.push_back({
        TransientEffectType::PlasmaExplosionCore,
        target.position,
        0.0F,
        1.0F,
        target.radius,
        target.radius,
        {target.color.red, target.color.green, target.color.blue, 255U},
        target.id,
      });
    }
  }
  for (const AimTrainerProjectileView& projectile : frame.projectiles) {
    if (projectileIndex >= projectiles.size()) break;
    projectiles[projectileIndex++] = {
      projectile.active, 0U, projectile.weapon, projectile.position,
      projectile.velocity, projectile.radius
    };
  }
}

} // namespace

int AimTrainerApp::run() const {
#if LG_DUEL_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("LG Duel - Aim Trainer", 1280, 720, SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }
  (void)SDL_SetWindowRelativeMouseMode(window, true);
  Renderer renderer;
  if (!renderer.initialize(window)) {
    std::cerr << "Renderer initialization failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  const LocalMapLoadResult map = loadLocalMap("aim_trainer");
  if (!map.ok) {
    std::cerr << "Aim trainer map failed: " << map.error << '\n';
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  BalanceConfig balance;
  AimTrainer trainer(map.arena, balance);
  char* preferencePath = SDL_GetPrefPath("LG Duel", "LG Duel");
  const std::filesystem::path preferences = preferencePath != nullptr
    ? std::filesystem::path(preferencePath) : std::filesystem::path(".");
  if (preferencePath != nullptr) SDL_free(preferencePath);
  AimTrainerStore store(preferences);
  AimTrainerMenu menu(trainer, store);
  const std::uint32_t balanceIdentity = trainerBalanceIdentity(balance);
  stampScenario(menu, map.descriptor.contentHash, balanceIdentity);

  bool running = true;
  bool forward = false;
  bool backward = false;
  bool left = false;
  bool right = false;
  bool attack = false;
  float yaw = 0.0F;
  float pitch = 0.0F;
  std::uint32_t commandSequence = 0;
  float accumulator = 0.0F;
  auto previous = std::chrono::steady_clock::now();
  RenderSettings settings;
  settings.playerModel = 1;
  settings.drawRemoteWeapons = false;
  settings.showOwnWeapons = true;
  settings.localSelectedWeapon = Weapon::LightningGun;
  std::array<bool, Arena::kHealthPickupCount> pickups = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> explosions = {};
  std::array<WeaponFireResult, kDuelPlayerCount> fires = {};
  std::array<RemotePlayerView, kDuelPlayerCount> workers = {};
  std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> projectiles = {};
  IcePoolArray icePools = {};
  std::vector<TransientEffect> orbEffects;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        yaw += event.motion.xrel * 0.0025F;
        pitch = std::clamp(pitch - event.motion.yrel * 0.0025F, -1.5F, 1.5F);
      }
      if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) continue;
      const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
      switch (event.key.scancode) {
      case SDL_SCANCODE_ESCAPE: if (pressed) running = false; break;
      case SDL_SCANCODE_W: forward = pressed; break;
      case SDL_SCANCODE_S: backward = pressed; break;
      case SDL_SCANCODE_A: left = pressed; break;
      case SDL_SCANCODE_D: right = pressed; break;
      case SDL_SCANCODE_SPACE: attack = pressed; break;
      default: break;
      }
      if (!pressed || event.key.repeat) continue;
      if (event.key.scancode == SDL_SCANCODE_UP) {
        const std::size_t count = menu.presets().size();
        if (count > 0U) (void)menu.selectPreset((menu.selectedPresetIndex() + count - 1U) % count);
        stampScenario(menu, map.descriptor.contentHash, balanceIdentity);
      } else if (event.key.scancode == SDL_SCANCODE_DOWN) {
        const std::size_t count = menu.presets().size();
        if (count > 0U) (void)menu.selectPreset((menu.selectedPresetIndex() + 1U) % count);
        stampScenario(menu, map.descriptor.contentHash, balanceIdentity);
      } else if (event.key.scancode == SDL_SCANCODE_RETURN) {
        (void)(menu.frame().phase == AimTrainerPhase::Results ? menu.repeat() : menu.start());
      } else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
        menu.abort();
      } else if (event.key.scancode == SDL_SCANCODE_F2) {
        (void)menu.saveAs("Aim preset " + std::to_string(menu.presets().size() + 1U));
      } else if (event.key.scancode == SDL_SCANCODE_F5) {
        (void)menu.overwrite();
      } else if (event.key.scancode == SDL_SCANCODE_DELETE) {
        (void)menu.deleteSelected();
      } else {
        AimScenario draft = menu.draft();
        AimTargetGroup& group = draft.groups[0];
        if (event.key.scancode == SDL_SCANCODE_O) group.visual = AimTargetVisual::Orb;
        else if (event.key.scancode == SDL_SCANCODE_W) group.visual = AimTargetVisual::Worker;
        else if (event.key.scancode == SDL_SCANCODE_L) draft.playerMovement =
          draft.playerMovement == AimPlayerMovement::Locked ? AimPlayerMovement::Normal : AimPlayerMovement::Locked;
        else if (event.key.scancode == SDL_SCANCODE_T) group.life =
          static_cast<AimTargetLife>((static_cast<unsigned>(group.life) + 1U) % 3U);
        else if (event.key.scancode == SDL_SCANCODE_P) {
          group.spawnMode = group.spawnMode == AimSpawnMode::FixedList
            ? AimSpawnMode::BoundedRandom : AimSpawnMode::FixedList;
          if (group.spawnMode == AimSpawnMode::FixedList && group.fixedSpawns.empty()) {
            group.fixedSpawns.push_back({5.0F, 0.0F, 1.5F});
          }
        }
        else if (event.key.scancode == SDL_SCANCODE_G) group.motion =
          static_cast<AimTargetMotion>((static_cast<unsigned>(group.motion) + 1U) % 3U);
        else if (event.key.scancode == SDL_SCANCODE_C) {
          const AimColor previous = group.color;
          group.color = previous.red > previous.green ? AimColor{80, 220, 255} : AimColor{255, 120, 80};
        } else if (event.key.scancode == SDL_SCANCODE_I) draft.infiniteAmmo = !draft.infiniteAmmo;
        else if (event.key.scancode == SDL_SCANCODE_K) draft.scoreMode =
          static_cast<AimScoreMode>((static_cast<unsigned>(draft.scoreMode) + 1U) % 3U);
        else if (event.key.scancode == SDL_SCANCODE_H) group.health =
          group.health >= 200 ? 1 : group.health + 99;
        else if (event.key.scancode == SDL_SCANCODE_R) group.respawnDelayTicks =
          group.respawnDelayTicks == 0U ? kFixedTickRate : 0U;
        else if (event.key.scancode == SDL_SCANCODE_SEMICOLON) group.strafeSpeed =
          std::max(0.0F, group.strafeSpeed - 0.5F);
        else if (event.key.scancode == SDL_SCANCODE_APOSTROPHE) group.strafeSpeed =
          std::min(20.0F, group.strafeSpeed + 0.5F);
        else if (event.key.scancode == SDL_SCANCODE_F3 && draft.groups.size() < 64U) {
          AimTargetGroup added = group;
          added.name = "target " + std::to_string(draft.groups.size() + 1U);
          draft.groups.push_back(std::move(added));
        } else if (event.key.scancode == SDL_SCANCODE_F4 && draft.groups.size() > 1U) {
          draft.groups.pop_back();
        }
        else if (event.key.scancode == SDL_SCANCODE_EQUALS) draft.durationTicks += 5U * kFixedTickRate;
        else if (event.key.scancode == SDL_SCANCODE_MINUS && draft.durationTicks > 5U * kFixedTickRate) draft.durationTicks -= 5U * kFixedTickRate;
        else if (event.key.scancode == SDL_SCANCODE_LEFTBRACKET) group.radius = std::max(0.05F, group.radius - 0.05F);
        else if (event.key.scancode == SDL_SCANCODE_RIGHTBRACKET) group.radius = std::min(5.0F, group.radius + 0.05F);
        else if (event.key.scancode >= SDL_SCANCODE_1 && event.key.scancode <= SDL_SCANCODE_9) {
          draft.weaponPolicy = AimWeaponPolicy::Forced;
          draft.forcedWeapon = static_cast<Weapon>(event.key.scancode - SDL_SCANCODE_1);
        } else if (event.key.scancode == SDL_SCANCODE_0) draft.weaponPolicy = AimWeaponPolicy::All;
        else continue;
        menu.edit(std::move(draft));
      }
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(now - previous).count();
    previous = now;
    const FixedTickFrame plan = planFixedTicks(accumulator, elapsed, kFixedTickSeconds, 8);
    for (int index = 0; index < plan.tickCount; ++index) {
      UserCommand command;
      command.sequence = ++commandSequence;
      command.viewYawRadians = yaw;
      command.viewPitchRadians = pitch;
      command.forwardMove = (forward ? 1.0F : 0.0F) - (backward ? 1.0F : 0.0F);
      command.rightMove = (right ? 1.0F : 0.0F) - (left ? 1.0F : 0.0F);
      command.attack = attack;
      command.weapon = menu.frame().selectedWeapon;
      menu.tick(command);
    }
    mapTrainerVisuals(menu.frame(), workers, projectiles, orbEffects);
    fires = {};
    fires[0] = menu.frame().latestFire;
    settings.localSelectedWeapon = menu.frame().selectedWeapon;
    HudRenderState hud;
    addTrainerHud(hud, menu);
    ConsoleRenderState console;
    renderer.render(map.arena, menu.frame().player, workers, {}, fires, explosions, projectiles,
      icePools, pickups, {}, orbEffects, 0U, settings, hud, console);
  }
  renderer.shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
#else
  std::cout << "Aim trainer requires SDL3. Build with SDL3 enabled.\n";
  return 1;
#endif
}

} // namespace lg
