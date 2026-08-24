#include "dev/DevJson.hpp"
#include "app/AimTrainerInput.hpp"
#include "render/OptionMenuLayout.hpp"
#include "shared/Constants.hpp"
#include "sim/Combat.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"
#include "sim/WeaponRuntime.hpp"
#include "trainer/AimTrainerEditor.hpp"
#include "trainer/AimTrainerPresentation.hpp"
#include "trainer/AimTrainerVideoSettings.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

[[nodiscard]] bool near(float left, float right, float epsilon = 0.001F) {
  return std::fabs(left - right) <= epsilon;
}

[[nodiscard]] std::filesystem::path temporaryRoot(std::string_view name) {
  return std::filesystem::temp_directory_path() /
    ("lg-duel-trainer-" + std::string(name) + "-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()
    ));
}

void removeTree(const std::filesystem::path& root) {
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

[[nodiscard]] std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  );
}

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::trunc);
  output << text;
}

lg::Arena trainerArena() {
  lg::Arena arena;
  arena.min = {-20.0F, -20.0F, -4.0F};
  arena.max = {20.0F, 20.0F, 10.0F};
  arena.spawnPositions[0] = {0.0F, 0.0F, 0.0F};
  arena.spawnCount = 1;
  return arena;
}

lg::AimScenario directOrbScenario() {
  lg::AimScenario scenario;
  scenario.name = "Direct orb";
  scenario.durationTicks = 30;
  scenario.weaponPolicy = lg::AimWeaponPolicy::Forced;
  scenario.forcedWeapon = lg::Weapon::Railgun;
  scenario.scoreMode = lg::AimScoreMode::Hit;
  scenario.groups[0].name = "Direct target";
  scenario.groups[0].visual = lg::AimTargetVisual::Orb;
  scenario.groups[0].life = lg::AimTargetLife::Invincible;
  scenario.groups[0].spawnMode = lg::AimSpawnMode::FixedList;
  scenario.groups[0].fixedSpawns = {{5.0F, 0.0F, 1.55F}};
  scenario.groups[0].radius = 1.0F;
  scenario.groups[0].count = 1;
  return scenario;
}

lg::UserCommand attackCommand(
  lg::Weapon weapon = lg::Weapon::Railgun,
  std::uint32_t sequence = 1U
) {
  lg::UserCommand command;
  command.sequence = sequence;
  command.attack = true;
  command.weapon = weapon;
  command.planarAim = false;
  return command;
}

[[nodiscard]] const lg::AimScenario* presetNamed(
  const std::vector<lg::AimScenario>& presets,
  std::string_view name
) {
  const auto found = std::find_if(
    presets.begin(),
    presets.end(),
    [name](const lg::AimScenario& preset) { return preset.name == name; }
  );
  return found == presets.end() ? nullptr : &*found;
}

[[nodiscard]] std::size_t editorRow(
  const lg::AimTrainerEditor& editor,
  lg::AimTrainerEditorField field,
  std::uint8_t component = 0U
) {
  const std::vector<lg::AimTrainerEditorRow> rows = editor.rows();
  const auto found = std::find_if(
    rows.begin(),
    rows.end(),
    [field, component](const lg::AimTrainerEditorRow& row) {
      return row.field == field && row.component == component;
    }
  );
  return found == rows.end()
    ? rows.size()
    : static_cast<std::size_t>(found - rows.begin());
}

bool replaceEditorText(
  lg::AimTrainerEditor& editor,
  lg::AimTrainerEditorField field,
  std::string_view value,
  std::uint8_t component = 0U
) {
  const std::size_t row = editorRow(editor, field, component);
  if (row >= editor.rows().size()) return false;
  editor.selectRow(row);
  if (!editor.activateSelected()) return false;
  while (!editor.textInput().empty()) editor.backspace();
  editor.insertText(value);
  return editor.commitText();
}

[[nodiscard]] float weaponEffectiveRange(
  const lg::BalanceConfig& balance,
  lg::Weapon weapon
) {
  switch (weapon) {
  case lg::Weapon::LightningGun: return balance.lightningGun.range;
  case lg::Weapon::FreezeGun: return balance.freezeGun.range;
  case lg::Weapon::Railgun: return balance.railgun.range;
  case lg::Weapon::Revolver: return balance.revolver.range;
  case lg::Weapon::MachineGun: return balance.machineGun.range;
  case lg::Weapon::Shotgun: return balance.shotgun.range;
  case lg::Weapon::RocketLauncher:
    return balance.rocketLauncher.speed * balance.rocketLauncher.maxLifetimeTicks *
      lg::kFixedTickSeconds;
  case lg::Weapon::GrenadeLauncher:
    return balance.grenadeLauncher.speed * balance.grenadeLauncher.fuseTicks *
      lg::kFixedTickSeconds;
  case lg::Weapon::PlasmaGun:
    return balance.plasmaGun.speed * balance.plasmaGun.maxLifetimeTicks *
      lg::kFixedTickSeconds;
  }
  return 0.0F;
}

[[nodiscard]] float weaponEyeHeight(
  const lg::BalanceConfig& balance,
  lg::Weapon weapon
) {
  switch (weapon) {
  case lg::Weapon::LightningGun: return balance.lightningGun.eyeHeight;
  case lg::Weapon::FreezeGun: return balance.freezeGun.eyeHeight;
  case lg::Weapon::Railgun: return balance.railgun.eyeHeight;
  case lg::Weapon::Revolver: return balance.revolver.eyeHeight;
  case lg::Weapon::MachineGun: return balance.machineGun.eyeHeight;
  case lg::Weapon::Shotgun: return balance.shotgun.eyeHeight;
  case lg::Weapon::RocketLauncher: return balance.rocketLauncher.eyeHeight;
  case lg::Weapon::GrenadeLauncher: return balance.grenadeLauncher.eyeHeight;
  case lg::Weapon::PlasmaGun: return balance.plasmaGun.eyeHeight;
  }
  return 0.65F;
}

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena = trainerArena();
  const lg::BalanceConfig balance;

  {
    const lg::AimTrainerViewAngles moved =
      lg::applyAimTrainerMouseMotion({}, 10.0F, 0.0F);
    failures += expect(
      moved.yaw < 0.0F,
      "rightward trainer mouse motion should turn right like the normal client"
    );
  }

  failures += expect(
    lg::aimTrainerEscapeAction(true, false) ==
      lg::AimTrainerEscapeAction::CloseScenarios,
    "Escape should close the idle scenario menu instead of quitting"
  );

  failures += expect(
    lg::shouldHandleAimTrainerMenuKeyDown(true, true, true),
    "held scenario-menu arrow keys should keep moving through rows"
  );
  failures += expect(
    !lg::shouldHandleAimTrainerMenuKeyDown(true, true, false),
    "held command keys should not trigger menu commands again"
  );
  failures += expect(
    lg::aimTrainerUsesCompactHud(580) &&
      !lg::aimTrainerUsesCompactHud(1200),
    "trainer HUD should stack status text in narrow tiled windows"
  );
  {
    const lg::AimTrainerWheelInput wheel =
      lg::accumulateAimTrainerWheel(0.0F, 0.1F);
    failures += expect(
      wheel.rowDelta == 0 && near(wheel.remainder, 0.1F),
      "small trackpad deltas should accumulate instead of jumping three rows"
    );
    const lg::AimTrainerWheelInput accumulated =
      lg::accumulateAimTrainerWheel(0.6F, 0.5F);
    failures += expect(
      accumulated.rowDelta == -1 && near(accumulated.remainder, 0.1F),
      "trackpad deltas should move one row after they add up to a full step"
    );
  }

  {
    const std::filesystem::path root = temporaryRoot("video-settings");
    const std::filesystem::path path = root / "video.cfg";
    lg::AimTrainerVideoSettings saved;
    saved.displayMode = 2;
    saved.resolutionWidth = 1920;
    saved.resolutionHeight = 1200;
    saved.textureFilter = 2;
    saved.textureAnisotropy = 16;
    saved.displayGamma = 1.1F;
    saved.bloom = false;
    saved.antiAliasing = 2;
    saved.sunShadows = 2;
    saved.pointLights = 2;
    std::string error;
    const bool wrote = lg::saveAimTrainerVideoSettings(path, saved, error);
    const lg::AimTrainerVideoSettingsLoadResult loaded =
      lg::loadAimTrainerVideoSettings(path);
    failures += expect(
      wrote && loaded.loaded && loaded.warning.empty() &&
        loaded.settings.displayMode == saved.displayMode &&
        loaded.settings.resolutionWidth == saved.resolutionWidth &&
        loaded.settings.resolutionHeight == saved.resolutionHeight &&
        loaded.settings.textureFilter == saved.textureFilter &&
        loaded.settings.textureAnisotropy == saved.textureAnisotropy &&
        near(loaded.settings.displayGamma, saved.displayGamma) &&
        loaded.settings.bloom == saved.bloom &&
        loaded.settings.antiAliasing == saved.antiAliasing &&
        loaded.settings.sunShadows == saved.sunShadows &&
        loaded.settings.pointLights == saved.pointLights,
      "applied trainer video settings should survive a client restart"
    );
    removeTree(root);
  }

  // Exact default duration, one natural result, and no second write.
  {
    const std::filesystem::path root = temporaryRoot("exact-duration");
    lg::AimTrainer trainer(arena, balance);
    lg::AimTrainerStore store(root);
    lg::AimTrainerMenu menu(trainer, store);
    lg::AimScenario scenario = directOrbScenario();
    scenario.name = "Exact 60 seconds";
    scenario.durationTicks = 7500U;
    menu.setRuntimeIdentity("aim_trainer", 77U, 99U);
    menu.edit(scenario);
    failures += expect(menu.start().ok, "7,500-tick scenario should start");
    for (std::uint32_t tick = 0; tick < 7499U; ++tick) {
      menu.tick({});
      failures += expect(
        menu.frame().phase == lg::AimTrainerPhase::Running,
        "run must stay active before tick 7,500"
      );
    }
    menu.tick({});
    failures += expect(
      menu.frame().phase == lg::AimTrainerPhase::Results &&
      menu.frame().elapsedTicks == 7500U &&
      menu.frame().result.durationTicks == 7500U &&
      menu.frame().naturalCompletion,
      "run must finish on exactly tick 7,500"
    );
    const std::size_t resultCount = menu.leaderboard().size();
    menu.tick({});
    menu.tick({});
    failures += expect(
      resultCount == 1U && menu.leaderboard().size() == resultCount,
      "natural completion must record one result only"
    );
    failures += expect(
      menu.frame().result.scenarioFingerprint != 0U,
      "result identity must be stamped before start"
    );
    removeTree(root);
  }

  // Trainer shortcuts can start and restart the current draft without the editor row.
  {
    const std::filesystem::path root = temporaryRoot("shortcut-restart");
    lg::AimTrainer trainer(arena, balance);
    lg::AimTrainerStore store(root);
    lg::AimTrainerMenu menu(trainer, store);
    lg::AimScenario scenario = directOrbScenario();
    scenario.durationTicks = 100U;
    menu.edit(scenario);
    failures += expect(menu.start().ok, "F3 start command should start the draft");
    lg::AimTrainerEditor runningEditor(menu);
    const std::vector<lg::AimTrainerEditorRow> runningRows = runningEditor.rows();
    const bool runningHasAbort = std::any_of(
      runningRows.begin(),
      runningRows.end(),
      [](const lg::AimTrainerEditorRow& row) {
        return row.field == lg::AimTrainerEditorField::Abort;
      }
    );
    const bool runningHasStart = std::any_of(
      runningRows.begin(),
      runningRows.end(),
      [](const lg::AimTrainerEditorRow& row) {
        return row.field == lg::AimTrainerEditorField::Start;
      }
    );
    failures += expect(
      runningHasAbort && !runningHasStart,
      "a running scenario menu should offer Abort instead of a Start command that cannot work"
    );
    for (std::uint32_t tick = 0; tick < 12U; ++tick) menu.tick({});
    failures += expect(
      menu.frame().phase == lg::AimTrainerPhase::Running &&
      menu.frame().elapsedTicks == 12U,
      "shortcut fixture should advance before restart"
    );
    failures += expect(menu.restart().ok, "F5 restart command should restart the draft");
    failures += expect(
      menu.frame().phase == lg::AimTrainerPhase::Running &&
      menu.frame().elapsedTicks == 0U &&
      menu.frame().remainingTicks == scenario.durationTicks,
      "restart should reset the active run to its first tick"
    );
    removeTree(root);
  }

  // Event retention and the continuous beam presentation seam.
  {
    lg::AimTrainer trainer(arena, balance);
    lg::AimScenario scenario = directOrbScenario();
    scenario.durationTicks = 100U;
    failures += expect(trainer.arm(scenario).ok && trainer.start(),
      "event scenario should start");
    (void)trainer.tick(attackCommand());
    failures += expect(
      trainer.view().fireEventPending && trainer.view().latestFire.fired &&
      trainer.view().pendingFires.size() == 1U,
      "accepted hitscan event should be pending"
    );
    failures += expect(
      trainer.view().hitConfirmPending &&
        trainer.view().pendingHitConfirmDamage ==
          static_cast<std::uint32_t>(balance.railgun.damage),
      "a trainer target hit should expose damage for one hit-confirm sound"
    );
    (void)trainer.tick({});
    (void)trainer.tick({});
    failures += expect(
      trainer.view().fireEventPending && trainer.view().latestFire.fired,
      "later fixed ticks must retain an unconsumed hitscan event"
    );
    trainer.consumePresentationEvents();
    failures += expect(
      !trainer.view().fireEventPending && !trainer.view().latestFire.fired &&
      trainer.view().pendingFires.empty() &&
      !trainer.view().hitConfirmPending &&
      trainer.view().pendingHitConfirmDamage == 0U,
      "render consumption should clear fire and hit-confirm events"
    );
    trainer.abort();

    lg::BalanceConfig rapid = balance;
    rapid.machineGunCooldownTicks = 1U;
    lg::AimScenario rapidScenario = directOrbScenario();
    rapidScenario.forcedWeapon = lg::Weapon::MachineGun;
    lg::AimTrainer rapidTrainer(arena, rapid);
    failures += expect(rapidTrainer.arm(rapidScenario).ok && rapidTrainer.start(),
      "rapid event scenario should start");
    for (std::uint32_t tick = 0U; tick < 3U; ++tick) {
      (void)rapidTrainer.tick(attackCommand(lg::Weapon::MachineGun, tick + 1U));
    }
    failures += expect(
      rapidTrainer.view().pendingFires.size() == 3U,
      "each discrete fire event must remain queued until render consumption"
    );

    lg::AimScenario beam = directOrbScenario();
    beam.forcedWeapon = lg::Weapon::LightningGun;
    failures += expect(trainer.arm(beam).ok && trainer.start(),
      "beam scenario should start");
    (void)trainer.tick(attackCommand(lg::Weapon::LightningGun));
    failures += expect(
      trainer.view().latestBeam.active && trainer.view().latestBeam.hit,
      "beam result should expose active and hit presentation data"
    );
  }

  // Live all-mode switching, QL pullout/cooldown, forced mode, and finite beam ammo.
  {
    lg::AimScenario scenario = directOrbScenario();
    scenario.weaponPolicy = lg::AimWeaponPolicy::All;
    scenario.durationTicks = 400U;
    lg::AimTrainer trainer(arena, balance);
    failures += expect(trainer.arm(scenario).ok && trainer.start(),
      "all-weapons scenario should start");
    (void)trainer.tick(attackCommand(lg::Weapon::Railgun));
    failures += expect(
      trainer.view().selectedWeapon == lg::Weapon::Railgun &&
      trainer.view().stats.attempts == 0U,
      "active all-mode switch should select Railgun and enforce pullout"
    );
    for (std::uint32_t tick = 1U; tick < balance.weaponPulloutTicks; ++tick) {
      (void)trainer.tick(attackCommand(lg::Weapon::Railgun, tick + 1U));
    }
    failures += expect(
      trainer.view().stats.attempts == 0U,
      "pullout must block fire for its full duration"
    );
    (void)trainer.tick(attackCommand(lg::Weapon::Railgun, 100U));
    failures += expect(
      trainer.view().stats.attempts == 1U,
      "weapon should fire when pullout reaches zero"
    );
    (void)trainer.tick(attackCommand(lg::Weapon::Railgun, 101U));
    failures += expect(
      trainer.view().stats.attempts == 1U,
      "weapon cooldown should reject the next tick"
    );

    lg::AimScenario forced = directOrbScenario();
    forced.forcedWeapon = lg::Weapon::Shotgun;
    lg::AimTrainer forcedTrainer(arena, balance);
    failures += expect(forcedTrainer.arm(forced).ok && forcedTrainer.start(),
      "forced scenario should start");
    (void)forcedTrainer.tick(attackCommand(lg::Weapon::RocketLauncher));
    failures += expect(
      forcedTrainer.view().selectedWeapon == lg::Weapon::Shotgun &&
      forcedTrainer.view().stats.pelletAttempts == balance.shotgun.pelletCount,
      "forced mode must ignore a live switch request"
    );

    lg::BalanceConfig finiteBalance = balance;
    finiteBalance.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::LightningGun)] = 2;
    lg::AimScenario finite = directOrbScenario();
    finite.forcedWeapon = lg::Weapon::LightningGun;
    finite.infiniteAmmo = false;
    lg::AimTrainer beamTrainer(arena, finiteBalance);
    failures += expect(beamTrainer.arm(finite).ok && beamTrainer.start(),
      "finite beam scenario should start");
    bool sawBeam = false;
    for (std::uint32_t tick = 0; tick < 20U; ++tick) {
      (void)beamTrainer.tick(attackCommand(lg::Weapon::LightningGun, tick + 1U));
      sawBeam = sawBeam || beamTrainer.view().latestBeam.active;
    }
    failures += expect(
      sawBeam && beamTrainer.view().ammo[lg::weaponIndex(lg::Weapon::LightningGun)] == 0,
      "beam hold should consume finite ammo at shared beam timing"
    );
    (void)beamTrainer.tick(attackCommand(lg::Weapon::LightningGun, 30U));
    failures += expect(!beamTrainer.view().latestBeam.active,
      "empty beam ammo should stop the beam");
  }

  // Beam hit score uses the same fixed-pulse unit as displayed hits.
  {
    lg::AimScenario tracking = directOrbScenario();
    tracking.forcedWeapon = lg::Weapon::LightningGun;
    tracking.hitScore = 3U;
    tracking.durationTicks = 40U;
    lg::AimTrainer trainer(arena, balance);
    failures += expect(trainer.arm(tracking).ok && trainer.start(),
      "sustained beam scenario should start");
    constexpr std::uint32_t kTrackingTicks = 20U;
    for (std::uint32_t tick = 0U; tick < kTrackingTicks; ++tick) {
      (void)trainer.tick(attackCommand(lg::Weapon::LightningGun, tick + 1U));
    }
    failures += expect(
      trainer.view().stats.beamAttempts == kTrackingTicks &&
      trainer.view().stats.beamHits == kTrackingTicks &&
      trainer.view().stats.attempts == kTrackingTicks &&
      trainer.view().stats.hits == kTrackingTicks &&
      near(trainer.view().stats.accuracyPercent(), 100.0F) &&
      trainer.view().stats.score ==
        static_cast<std::uint64_t>(kTrackingTicks) * tracking.hitScore &&
      trainer.view().stats.damage == 24U,
      "beam hits, hit score, accuracy, and shot-credit damage should stay consistent"
    );
  }

  // Normal movement actions reach the local movement simulation.
  {
    lg::AimScenario scenario = directOrbScenario();
    scenario.playerMovement = lg::AimPlayerMovement::Normal;
    scenario.durationTicks = 50U;
    lg::AimTrainer trainer(arena, balance);
    failures += expect(trainer.arm(scenario).ok && trainer.start(),
      "normal movement scenario should start");
    lg::UserCommand movement;
    movement.jump = true;
    movement.dash = true;
    movement.crouch = true;
    movement.sneak = true;
    movement.forwardMove = 1.0F;
    (void)trainer.tick(movement);
    failures += expect(
      trainer.view().player.velocity.z > 0.0F &&
      std::hypot(
        trainer.view().player.velocity.x,
        trainer.view().player.velocity.y
      ) > 0.0F,
      "jump, dash, and normal move input should affect the player"
    );
  }

  // A prior projectile hit and a shotgun batch in one tick both count for accuracy.
  {
    lg::BalanceConfig fast = balance;
    fast.weaponPulloutTicks = 0U;
    fast.rocketLauncher.speed = 100.0F;
    fast.rocketLauncherCooldownTicks = 1U;
    fast.shotgun.spreadRadians = 0.0F;
    lg::AimScenario scenario = directOrbScenario();
    scenario.weaponPolicy = lg::AimWeaponPolicy::All;
    scenario.groups[0].fixedSpawns = {{1.65F, 0.0F, 1.55F}};
    scenario.groups[0].radius = 0.1F;
    scenario.durationTicks = 20U;
    lg::AimTrainer trainer(arena, fast);
    failures += expect(trainer.arm(scenario).ok && trainer.start(),
      "mixed-hit scenario should start");
    (void)trainer.tick(attackCommand(lg::Weapon::RocketLauncher, 1U));
    (void)trainer.tick(attackCommand(lg::Weapon::Shotgun, 2U));
    if (
      trainer.view().stats.projectileHits != 1U ||
      trainer.view().stats.pelletHits != fast.shotgun.pelletCount
    ) {
      std::cerr << "mixed counters projectile=" << trainer.view().stats.projectileHits
        << " pellets=" << trainer.view().stats.pelletHits
        << " hits=" << trainer.view().stats.hits << '\n';
    }
    failures += expect(
      trainer.view().stats.projectileHits == 1U &&
      trainer.view().stats.pelletHits == fast.shotgun.pelletCount &&
      trainer.view().stats.hits ==
        trainer.view().stats.projectileHits + trainer.view().stats.pelletHits &&
      trainer.view().stats.score ==
        trainer.view().stats.projectileHits + trainer.view().stats.pelletHits,
      "projectile and pellet hits in one tick must accumulate independently"
    );
  }

  // Projectile accuracy counts each damaging launch once, not each splash target.
  {
    lg::BalanceConfig splashBalance = balance;
    splashBalance.rocketLauncher.speed = 100.0F;
    splashBalance.rocketLauncherCooldownTicks = 20U;
    lg::AimScenario splash = directOrbScenario();
    splash.forcedWeapon = lg::Weapon::RocketLauncher;
    splash.groups[0].count = 2U;
    splash.groups[0].radius = 0.1F;
    splash.groups[0].life = lg::AimTargetLife::OneHit;
    splash.groups[0].respawnDelayTicks = 10U;
    splash.groups[0].fixedSpawns = {
      {1.65F, 0.0F, 1.55F},
      {1.65F, 0.4F, 1.55F},
    };
    lg::AimTrainer trainer(arena, splashBalance);
    failures += expect(trainer.arm(splash).ok && trainer.start(),
      "multi-target splash scenario should start");
    (void)trainer.tick(attackCommand(lg::Weapon::RocketLauncher, 1U));
    lg::UserCommand coast;
    coast.weapon = lg::Weapon::RocketLauncher;
    (void)trainer.tick(coast);
    failures += expect(
      trainer.view().stats.projectileAttempts == 1U &&
      trainer.view().stats.projectileHits == 1U &&
      trainer.view().stats.attempts == 1U &&
      trainer.view().stats.hits == 1U &&
      near(trainer.view().stats.accuracyPercent(), 100.0F) &&
      trainer.view().stats.score == 2U &&
      trainer.view().stats.clears == 2U &&
      trainer.view().stats.damage > static_cast<std::uint64_t>(
        splashBalance.rocketLauncher.directDamage
      ),
      "one splash launch should be one accuracy hit but score, damage, and clear both targets"
    );

    lg::WeaponRuntimeConfig runtimeConfig{
      splashBalance,
      true,
      lg::WeaponRuntimeSwitchingMode::Crazy,
    };
    runtimeConfig.balance.rocketLauncher.radius = 0.2F;
    lg::WeaponRuntimeState state = lg::makeWeaponRuntimeState(
      runtimeConfig,
      lg::Weapon::RocketLauncher
    );
    for (std::uint32_t index = 0U; index < 2U; ++index) {
      lg::RocketProjectile projectile;
      projectile.active = true;
      projectile.sequence = index + 1U;
      projectile.weapon = lg::Weapon::RocketLauncher;
      projectile.position = {0.5F, index == 0U ? -2.0F : 2.0F, 1.55F};
      projectile.previousPosition = projectile.position;
      projectile.velocity = {100.0F, 0.0F, 0.0F};
      state.projectiles.push_back(projectile);
    }
    std::array<lg::WeaponRuntimeTarget, 2> targets = {};
    for (std::size_t index = 0U; index < targets.size(); ++index) {
      targets[index].id = static_cast<std::uint32_t>(index + 1U);
      targets[index].shape = lg::WeaponRuntimeTargetShape::Sphere;
      targets[index].center = {1.0F, index == 0U ? -2.0F : 2.0F, 1.55F};
      targets[index].radius = 0.1F;
      targets[index].active = true;
    }
    lg::PlayerState attacker;
    attacker.position = {0.0F, 0.0F, 0.8F};
    attacker.health = 100;
    lg::UserCommand idle;
    idle.weapon = lg::Weapon::RocketLauncher;
    const lg::WeaponRuntimeTick twoHits = lg::tickWeaponRuntime(
      state,
      runtimeConfig,
      attacker,
      idle,
      arena,
      targets,
      lg::kFixedTickSeconds
    );
    failures += expect(
      twoHits.damagingProjectileHits == 2U && twoHits.hits.size() == 2U,
      "two projectiles contacting on one tick should each count once"
    );
  }

  // Fixed-list distribution, later-group edits, and full validated target output.
  {
    lg::AimScenario fixed = directOrbScenario();
    fixed.groups[0].count = 3U;
    fixed.groups[0].fixedSpawns = {
      {3.0F, -1.0F, 1.5F},
      {3.0F, 1.0F, 1.5F},
    };
    lg::AimTargetGroup second = fixed.groups[0];
    second.name = "second";
    second.count = 2U;
    second.fixedSpawns = {{6.0F, -2.0F, 1.5F}, {6.0F, 2.0F, 1.5F}};
    fixed.groups.push_back(second);
    lg::AimTrainer trainer(arena, balance);
    failures += expect(trainer.arm(fixed).ok && trainer.start(),
      "multi-group fixed scenario should start");
    failures += expect(
      near(trainer.view().targets[0].position.y, -1.0F) &&
      near(trainer.view().targets[1].position.y, 1.0F) &&
      near(trainer.view().targets[2].position.y, -1.0F) &&
      trainer.view().targets[3].groupIndex == 1U &&
      near(trainer.view().targets[3].position.y, -2.0F) &&
      near(trainer.view().targets[4].position.y, 2.0F),
      "fixed targets should distribute by slot within each group"
    );

    lg::AimScenario full = directOrbScenario();
    full.groups.clear();
    for (std::size_t group = 0; group < lg::AimScenario::kMaxGroups; ++group) {
      lg::AimTargetGroup target;
      target.name = "worker " + std::to_string(group);
      target.visual = lg::AimTargetVisual::Worker;
      target.life = lg::AimTargetLife::Invincible;
      target.spawnMode = lg::AimSpawnMode::FixedList;
      target.fixedSpawns = {{5.0F, 0.0F, 1.5F}};
      target.count = lg::AimScenario::kMaxTargetsPerGroup;
      full.groups.push_back(std::move(target));
    }
    lg::AimTrainer fullTrainer(arena, balance);
    failures += expect(fullTrainer.arm(full).ok && fullTrainer.start(),
      "maximum validated Worker scenario should start");
    const lg::AimTrainerPresentation presentation =
      lg::buildAimTrainerPresentation(fullTrainer.view());
    failures += expect(
      fullTrainer.view().targets.size() == lg::AimScenario::kMaxTargets &&
      presentation.workerCount == lg::AimScenario::kMaxTargets &&
      presentation.targetEffects.size() == lg::AimScenario::kMaxTargets &&
      std::all_of(
        presentation.targetEffects.begin(),
        presentation.targetEffects.end(),
        [](const lg::TransientEffect& effect) {
          return effect.type == lg::TransientEffectType::TrainerWorkerTarget;
        }
      ),
      "every valid live Worker must have a dedicated trainer render item"
    );
    full.groups.push_back(full.groups.back());
    lg::AimTrainer overCap(arena, balance);
    failures += expect(!overCap.arm(full).ok,
      "scenario validation must reject target groups above the shown cap");
  }

  // Worker targets are grounded bot-shaped targets with real locomotion state.
  {
    lg::AimScenario worker = directOrbScenario();
    worker.groups[0].visual = lg::AimTargetVisual::Worker;
    worker.groups[0].life = lg::AimTargetLife::Invincible;
    worker.groups[0].spawnMode = lg::AimSpawnMode::FixedList;
    worker.groups[0].fixedSpawns = {{5.0F, 0.0F, 4.0F}};
    worker.groups[0].motion = lg::AimTargetMotion::Strafe;
    worker.groups[0].strafeDirection = {0.0F, 1.0F, 0.0F};
    worker.groups[0].strafeSpeed = 2.0F;
    worker.durationTicks = 100U;
    lg::AimTrainer trainer(arena, balance);
    failures += expect(
      trainer.arm(worker).ok && trainer.start(),
      "grounded Worker scenario should start"
    );
    (void)trainer.tick({});
    const lg::AimTargetView& target = trainer.view().targets[0];
    const lg::AimTrainerPresentation presentation =
      lg::buildAimTrainerPresentation(trainer.view());
    failures += expect(
      near(target.position.z, target.worker.bounds.halfHeight) &&
        near(target.worker.position.z, target.worker.bounds.halfHeight) &&
        near(target.worker.velocity.y, worker.groups[0].strafeSpeed) &&
        near(target.worker.viewYawRadians, 1.57079632679F) &&
        presentation.targetEffects.size() == 1U &&
        near(presentation.targetEffects[0].velocity.y, worker.groups[0].strafeSpeed) &&
        near(presentation.targetEffects[0].rotationRadians, 1.57079632679F),
      "Worker targets should stand on the floor and carry travel speed and facing to rendering"
    );
  }

  // Freeze changes Worker state and projectile helpers preserve bounce/fuse behavior.
  {
    lg::AimScenario freeze = directOrbScenario();
    freeze.forcedWeapon = lg::Weapon::FreezeGun;
    freeze.groups[0].visual = lg::AimTargetVisual::Worker;
    freeze.groups[0].motion = lg::AimTargetMotion::Strafe;
    freeze.groups[0].strafeSpeed = 2.0F;
    freeze.durationTicks = 100U;
    lg::AimTrainer trainer(arena, balance);
    failures += expect(trainer.arm(freeze).ok && trainer.start(),
      "freeze scenario should start");
    float peakFreeze = 0.0F;
    float peakBeamFreeze = 0.0F;
    for (std::uint32_t tick = 0; tick < 20U; ++tick) {
      (void)trainer.tick(attackCommand(lg::Weapon::FreezeGun, tick + 1U));
      peakFreeze = std::max(peakFreeze, trainer.view().targets[0].worker.freezeLevel);
      peakBeamFreeze = std::max(
        peakBeamFreeze,
        trainer.view().latestBeam.freezeApplied
      );
    }
    if (peakFreeze <= 0.0F || peakBeamFreeze <= 0.0F) {
      std::cerr << "freeze peak=" << peakFreeze
        << " beam=" << peakBeamFreeze
        << " active=" << trainer.view().latestBeam.active
        << " hit=" << trainer.view().latestBeam.hit << '\n';
    }
    failures += expect(
      peakFreeze > 0.0F && peakBeamFreeze > 0.0F,
      "Freeze Gun should apply shared freeze state to Worker targets"
    );
    lg::AimTrainer poolTrainer(arena, balance);
    failures += expect(poolTrainer.arm(freeze).ok && poolTrainer.start(),
      "freeze pool scenario should start");
    lg::UserCommand floorBeam = attackCommand(lg::Weapon::FreezeGun, 50U);
    floorBeam.viewPitchRadians = -1.2F;
    (void)poolTrainer.tick(floorBeam);
    failures += expect(
      std::any_of(
        poolTrainer.view().icePools.begin(),
        poolTrainer.view().icePools.end(),
        [](const lg::IcePool& pool) { return pool.active && pool.radius > 0.0F; }
      ),
      "Freeze Gun floor hits should grow the shared local ice-pool state"
    );
    const lg::WeaponRuntimeGrenadeBounce bounce = lg::bounceWeaponRuntimeGrenade(
      {2.0F, 0.0F, -8.0F},
      {0.0F, 0.0F, 1.0F},
      balance.grenadeLauncher
    );
    failures += expect(
      bounce.velocity.z > 0.0F && bounce.impactSpeed >= 8.0F,
      "grenade collision should use the shared damped bounce rule"
    );

    lg::WeaponRuntimeConfig config{balance, true, lg::WeaponRuntimeSwitchingMode::Crazy};
    config.balance.grenadeLauncher.fuseTicks = 3U;
    lg::WeaponRuntimeState state = lg::makeWeaponRuntimeState(
      config,
      lg::Weapon::GrenadeLauncher
    );
    lg::PlayerState attacker;
    attacker.position = {0.0F, 0.0F, 0.9F};
    attacker.health = 100;
    lg::UserCommand launch = attackCommand(lg::Weapon::GrenadeLauncher);
    (void)lg::tickWeaponRuntime(
      state, config, attacker, launch, arena, {}, lg::kFixedTickSeconds
    );
    for (std::uint32_t tick = 0; tick < 3U && !state.projectiles.empty(); ++tick) {
      launch.attack = false;
      launch.sequence++;
      (void)lg::tickWeaponRuntime(
        state, config, attacker, launch, arena, {}, lg::kFixedTickSeconds
      );
    }
    failures += expect(state.projectiles.empty(),
      "grenade projectile should end at the shared fuse tick");
  }

  // Full GUI model and input flow, including later groups, named save, and results.
  {
    const std::filesystem::path root = temporaryRoot("editor");
    lg::AimTrainer trainer(arena, balance);
    lg::AimTrainerStore store(root);
    lg::AimTrainerMenu menu(trainer, store);
    menu.setRuntimeIdentity("aim_trainer", 0x123456789abcdef0ULL, 0xfedcba9876543210ULL);
    menu.edit(directOrbScenario());
    lg::AimTrainerEditor editor(menu);
    const std::vector<lg::AimTrainerEditorRow> rows = editor.rows();
    std::set<lg::AimTrainerEditorField> fields;
    for (const lg::AimTrainerEditorRow& row : rows) fields.insert(row.field);
    const std::array requiredFields = {
      lg::AimTrainerEditorField::Preset,
      lg::AimTrainerEditorField::PresetName,
      lg::AimTrainerEditorField::SaveAs,
      lg::AimTrainerEditorField::Overwrite,
      lg::AimTrainerEditorField::DeletePreset,
      lg::AimTrainerEditorField::Duration,
      lg::AimTrainerEditorField::PlayerMovement,
      lg::AimTrainerEditorField::WeaponPolicy,
      lg::AimTrainerEditorField::ForcedWeapon,
      lg::AimTrainerEditorField::InfiniteAmmo,
      lg::AimTrainerEditorField::ScoreMode,
      lg::AimTrainerEditorField::HitScore,
      lg::AimTrainerEditorField::DamageScore,
      lg::AimTrainerEditorField::ClearScore,
      lg::AimTrainerEditorField::Seed,
      lg::AimTrainerEditorField::AllowedWeapon,
      lg::AimTrainerEditorField::Group,
      lg::AimTrainerEditorField::AddGroup,
      lg::AimTrainerEditorField::RemoveGroup,
      lg::AimTrainerEditorField::GroupName,
      lg::AimTrainerEditorField::Visual,
      lg::AimTrainerEditorField::Count,
      lg::AimTrainerEditorField::Radius,
      lg::AimTrainerEditorField::Color,
      lg::AimTrainerEditorField::Life,
      lg::AimTrainerEditorField::Health,
      lg::AimTrainerEditorField::RespawnDelay,
      lg::AimTrainerEditorField::SpawnMode,
      lg::AimTrainerEditorField::FixedSpawns,
      lg::AimTrainerEditorField::RandomMinimum,
      lg::AimTrainerEditorField::RandomMaximum,
      lg::AimTrainerEditorField::Motion,
      lg::AimTrainerEditorField::StrafeSpeed,
      lg::AimTrainerEditorField::StrafeDirection,
      lg::AimTrainerEditorField::WaypointInterval,
      lg::AimTrainerEditorField::Result,
      lg::AimTrainerEditorField::Leaderboard,
    };
    failures += expect(
      std::all_of(
        requiredFields.begin(),
        requiredFields.end(),
        [&fields](lg::AimTrainerEditorField field) { return fields.contains(field); }
      ),
      "GUI must show every scenario, group, result, and leaderboard field"
    );
    const std::size_t orbRadiusRow = editorRow(
      editor,
      lg::AimTrainerEditorField::Radius
    );
    failures += expect(
      orbRadiusRow < editor.rows().size() && editor.rows()[orbRadiusRow].editable &&
      replaceEditorText(editor, lg::AimTrainerEditorField::Radius, "1.75") &&
      near(menu.draft().groups[0].radius, 1.75F),
      "Orb radius should remain visible and editable"
    );
    const std::size_t visualRow = editorRow(editor, lg::AimTrainerEditorField::Visual);
    editor.selectRow(visualRow);
    failures += expect(
      editor.adjustSelected(1) &&
      menu.draft().groups[0].visual == lg::AimTargetVisual::Worker,
      "GUI should switch the selected group to Worker"
    );
    const std::size_t workerRadiusRow = editorRow(
      editor,
      lg::AimTrainerEditorField::Radius
    );
    editor.selectRow(workerRadiusRow);
    const float storedOrbRadius = menu.draft().groups[0].radius;
    failures += expect(
      workerRadiusRow < editor.rows().size() &&
      !editor.rows()[workerRadiusRow].editable &&
      editor.rows()[workerRadiusRow].value.find("fixed") != std::string::npos &&
      !editor.adjustSelected(1) &&
      near(menu.draft().groups[0].radius, storedOrbRadius),
      "Worker radius should be fixed and reject edits without losing the stored Orb radius"
    );
    lg::AimTrainer workerRuntime(arena, balance);
    failures += expect(
      workerRuntime.arm(menu.draft()).ok && workerRuntime.start(),
      "fixed Worker presentation scenario should start"
    );
    const lg::AimTrainerPresentation workerPresentation =
      lg::buildAimTrainerPresentation(workerRuntime.view());
    failures += expect(
      workerRuntime.view().targets[0].radius == lg::CollisionBounds{}.radius &&
      !workerPresentation.targetEffects.empty() &&
      near(workerPresentation.targetEffects[0].initialScale, 1.0F) &&
      near(workerPresentation.targetEffects[0].finalScale, 1.0F),
      "Worker collision and presentation size should use fixed model bounds"
    );
    editor.selectRow(visualRow);
    failures += expect(
      editor.adjustSelected(1) &&
      menu.draft().groups[0].visual == lg::AimTargetVisual::Orb &&
      near(menu.draft().groups[0].radius, storedOrbRadius) &&
      editor.rows()[editorRow(editor, lg::AimTrainerEditorField::Radius)].editable,
      "switching back to Orb should restore its editable stored radius"
    );
    const std::size_t addRow = editorRow(editor, lg::AimTrainerEditorField::AddGroup);
    editor.selectRow(addRow);
    failures += expect(editor.activateSelected() && menu.selectedGroupIndex() == 1U,
      "GUI add-group command should select the new later group");
    failures += expect(
      replaceEditorText(editor, lg::AimTrainerEditorField::GroupName, "Second group") &&
      replaceEditorText(editor, lg::AimTrainerEditorField::Color, "17", 0U) &&
      replaceEditorText(editor, lg::AimTrainerEditorField::Color, "34", 1U) &&
      replaceEditorText(editor, lg::AimTrainerEditorField::Color, "51", 2U),
      "GUI text input should edit the selected later group and full RGB"
    );
    const std::size_t groupRow = editorRow(editor, lg::AimTrainerEditorField::Group);
    editor.selectRow(groupRow);
    failures += expect(
      editor.adjustSelected(-1) && menu.selectedGroupIndex() == 0U &&
      editor.adjustSelected(1) && menu.selectedGroupIndex() == 1U,
      "GUI group selector should reach both earlier and later groups"
    );
    failures += expect(
      replaceEditorText(editor, lg::AimTrainerEditorField::PresetName, "Named GUI preset"),
      "GUI should accept a caller-chosen preset name"
    );
    const std::size_t saveRow = editorRow(editor, lg::AimTrainerEditorField::SaveAs);
    editor.selectRow(saveRow);
    failures += expect(editor.activateSelected(), "GUI named-save command should persist the draft");
    const lg::AimScenario* saved = presetNamed(menu.presets(), "Named GUI preset");
    failures += expect(
      saved != nullptr && saved->groups.size() == 2U &&
      saved->groups[1].name == "Second group" &&
      saved->groups[1].color.red == 17U &&
      saved->mapIdentity == 0x123456789abcdef0ULL &&
      saved->balanceIdentity == 0xfedcba9876543210ULL,
      "GUI save should keep later-group edits and full identity stamps"
    );
    lg::AimScenario shortRun = menu.draft();
    shortRun.durationTicks = 1U;
    menu.edit(shortRun);
    const std::size_t startRow = editorRow(editor, lg::AimTrainerEditorField::Start);
    editor.selectRow(startRow);
    failures += expect(editor.activateSelected(), "GUI start command should start the draft");
    menu.tick({});
    editor.setOpen(true);
    failures += expect(
      editorRow(editor, lg::AimTrainerEditorField::Result) < editor.rows().size() &&
      editorRow(editor, lg::AimTrainerEditorField::Leaderboard) < editor.rows().size() &&
      !menu.leaderboard().empty(),
      "GUI should show the result and local leaderboard after a run"
    );
    const lg::OptionMenuLayout layout = lg::buildOptionMenuLayout(
      1280,
      720,
      editor.rows().size(),
      editor.scrollRows()
    );
    failures += expect(
      lg::optionMenuRowAt(
        layout,
        editor.scrollRows(),
        editor.rows().size(),
        layout.firstRowY + 1.0F
      ) >= 0,
      "mouse layout should map a visible menu row"
    );
    removeTree(root);
  }

  // Store overwrite, repeat writes, recovery, corrupt-entry safety, and 64-bit values.
  {
    const std::filesystem::path root = temporaryRoot("store");
    lg::AimTrainerStore store(root);
    lg::AimScenario scenario = directOrbScenario();
    scenario.name = "64-bit custom";
    scenario.seed = 0xfedcba9876543210ULL;
    scenario.mapIdentity = 0x123456789abcdef0ULL;
    scenario.balanceIdentity = 0xf123456789abcdefULL;
    failures += expect(store.savePreset(scenario, false).ok,
      "first preset write should succeed");
    scenario.durationTicks = 111U;
    failures += expect(store.savePreset(scenario, true).ok,
      "first preset update should replace safely");
    scenario.durationTicks = 222U;
    failures += expect(store.savePreset(scenario, true).ok,
      "second preset update should replace safely");
    const lg::AimTrainerPresetList loaded = store.loadPresets();
    const lg::AimScenario* roundTrip = presetNamed(loaded.presets, scenario.name);
    failures += expect(
      roundTrip != nullptr && roundTrip->durationTicks == 222U &&
      roundTrip->seed == scenario.seed &&
      roundTrip->mapIdentity == scenario.mapIdentity &&
      roundTrip->balanceIdentity == scenario.balanceIdentity &&
      std::filesystem::exists(
        (store.directory() / "presets.json").string() + ".bak"
      ),
      "repeat replacement should keep the latest preset and exact uint64 values"
    );
    const std::uint64_t fingerprint = lg::AimTrainer::scenarioFingerprint(scenario);
    lg::AimTrainerResult first{
      fingerprint, 9U, 100U, 2U, 10U, 8U, 7500U,
      0xefffffffffffffffULL, true
    };
    lg::AimTrainerResult second = first;
    second.score = 12U;
    second.seed = 0xdfffffffffffffffULL;
    failures += expect(store.recordNaturalResult(first).ok,
      "first result append should succeed");
    failures += expect(store.recordNaturalResult(second).ok,
      "second result append should succeed");
    const std::vector<lg::AimTrainerResult> leaderboard = store.leaderboard(fingerprint);
    failures += expect(
      leaderboard.size() == 2U && leaderboard[0].score == 12U &&
      leaderboard[0].scenarioFingerprint == fingerprint &&
      leaderboard[0].seed == second.seed && leaderboard[1].seed == first.seed,
      "repeat result appends should keep both exact uint64 records"
    );

    lg::AimScenario builtIn = lg::AimTrainerStore::builtInPresets()[0];
    builtIn.durationTicks = 321U;
    const lg::AimTrainerStoreReply overrideReply = store.savePreset(builtIn, true);
    const lg::AimTrainerPresetList overridden = store.loadPresets();
    const lg::AimScenario* override = presetNamed(overridden.presets, builtIn.name);
    failures += expect(
      overrideReply.ok && !overrideReply.warning.empty() && override != nullptr &&
      override->durationTicks == 321U,
      "editing a built-in should save a clear local override"
    );

    const std::filesystem::path presetsPath = store.directory() / "presets.json";
    lg::dev::JsonParseResult parsed = lg::dev::parseJson(readText(presetsPath));
    failures += expect(parsed.ok, "saved preset document should parse");
    if (parsed.ok) {
      parsed.value.object["presets"].array.push_back(
        lg::dev::JsonValue::stringValue("corrupt entry")
      );
      writeText(presetsPath, lg::dev::writeJson(parsed.value));
    }
    const lg::AimTrainerPresetList partial = store.loadPresets();
    failures += expect(
      presetNamed(partial.presets, scenario.name) != nullptr &&
      !partial.warning.empty() && !partial.safeToWrite,
      "one corrupt entry should not hide valid custom presets"
    );
    lg::AimScenario refused = scenario;
    refused.name = "must not erase";
    failures += expect(!store.savePreset(refused, false).ok,
      "save after corrupt input should refuse to erase recoverable data");

    writeText(presetsPath, "{not json");
    const lg::AimTrainerPresetList recovered = store.loadPresets();
    failures += expect(
      !recovered.warning.empty() &&
      presetNamed(recovered.presets, scenario.name) != nullptr,
      "malformed primary should recover the last replace-safe backup"
    );
    removeTree(root);
  }

  // Empty stored groups cannot reach groups[0] UI access.
  {
    const std::filesystem::path root = temporaryRoot("empty-corrupt");
    lg::AimTrainerStore store(root);
    lg::AimScenario custom = directOrbScenario();
    custom.name = "empty corrupt";
    failures += expect(store.savePreset(custom, false).ok,
      "empty-corrupt fixture should save first");
    const std::filesystem::path presetsPath = store.directory() / "presets.json";
    lg::dev::JsonParseResult parsed = lg::dev::parseJson(readText(presetsPath));
    if (parsed.ok && !parsed.value.object["presets"].array.empty()) {
      parsed.value.object["presets"].array[0].object["groups"] =
        lg::dev::JsonValue::arrayValue();
      writeText(presetsPath, lg::dev::writeJson(parsed.value));
    }
    lg::AimTrainer trainer(arena, balance);
    lg::AimTrainerMenu menu(trainer, store);
    menu.setRuntimeIdentity("aim_trainer", 1U, 2U);
    lg::AimTrainerEditor editor(menu);
    failures += expect(
      !menu.draft().groups.empty() && !editor.rows().empty() &&
      !store.loadPresets().safeToWrite,
      "empty corrupt preset should be skipped before UI group access"
    );
    removeTree(root);
  }

  // Loaded authored map scale, player clearance, and target reach.
  {
    const lg::LocalMapLoadResult loaded = lg::loadLocalMap("aim_trainer");
    failures += expect(loaded.ok, "authored aim trainer map should load");
    if (loaded.ok) {
      const lg::Vec3 extent = loaded.arena.max - loaded.arena.min;
      failures += expect(
        extent.x >= 24.0F && extent.y >= 16.0F && extent.z >= 8.0F,
        "loaded aim trainer room must retain authored Quake-unit scale"
      );
      failures += expect(loaded.arena.wallCount >= 6U && loaded.arena.spawnCount >= 1U,
        "loaded aim trainer map should be a closed room with a spawn");
      lg::PlayerState player;
      player.position = loaded.arena.spawnPositions[0];
      player.position.z += player.bounds.halfHeight;
      player.onGround = true;
      const lg::WorldTrace ceiling = lg::traceWorld(
        loaded.arena,
        player.position,
        {0.0F, 0.0F, 1.0F},
        10.0F
      );
      const lg::WorldTrace side = lg::traceWorld(
        loaded.arena,
        player.position,
        {0.0F, 1.0F, 0.0F},
        20.0F
      );
      failures += expect(
        ceiling.distance > player.bounds.halfHeight * 2.0F &&
        side.distance > player.bounds.radius * 2.0F,
        "spawn should retain player clearance inside the closed room"
      );

      const std::filesystem::path balancePath =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
        "config" / "balance.cfg";
      const lg::BalanceConfigLoadResult actualBalance =
        lg::loadBalanceConfigFromFile(balancePath.string());
      failures += expect(actualBalance.ok,
        "reachability regression should load the normal balance config");
      if (actualBalance.ok) {
        for (const lg::AimScenario& preset : lg::AimTrainerStore::builtInPresets()) {
          float shortestRange = std::numeric_limits<float>::max();
          lg::Weapon shortestWeapon = preset.forcedWeapon;
          for (std::size_t weaponIndex = 0U; weaponIndex < lg::kWeaponCount; ++weaponIndex) {
            const lg::Weapon weapon = static_cast<lg::Weapon>(weaponIndex);
            const bool enabled = preset.weaponPolicy == lg::AimWeaponPolicy::Forced
              ? weapon == preset.forcedWeapon
              : preset.allowedWeapons[weaponIndex];
            if (!enabled) continue;
            const float range = weaponEffectiveRange(actualBalance.config, weapon);
            if (range < shortestRange) {
              shortestRange = range;
              shortestWeapon = weapon;
            }
          }
          const lg::Vec3 muzzle = lg::weaponMuzzlePosition(
            player,
            weaponEyeHeight(actualBalance.config, shortestWeapon)
          );
          for (const lg::AimTargetGroup& group : preset.groups) {
            std::vector<lg::Vec3> extremes;
            if (group.spawnMode == lg::AimSpawnMode::FixedList) {
              extremes = group.fixedSpawns;
            } else {
              for (int x = 0; x < 2; ++x) {
                for (int y = 0; y < 2; ++y) {
                  for (int z = 0; z < 2; ++z) {
                    extremes.push_back({
                      x == 0 ? group.randomMinimum.x : group.randomMaximum.x,
                      y == 0 ? group.randomMinimum.y : group.randomMaximum.y,
                      z == 0 ? group.randomMinimum.z : group.randomMaximum.z,
                    });
                  }
                }
              }
            }
            for (const lg::Vec3 extreme : extremes) {
              const lg::Vec3 segment = extreme - muzzle;
              const float distance = lg::length(segment);
              const lg::WorldTrace reach = lg::traceWorld(
                loaded.arena,
                muzzle,
                segment / distance,
                distance
              );
              failures += expect(
                distance <= shortestRange + 0.001F &&
                reach.distance >= distance - 0.01F,
                "every built-in spawn extreme must be clear and within its shortest enabled weapon range"
              );
            }
          }
        }
      }
      const lg::Vec3 before = player.position;
      lg::simulateMovement(
        player,
        {},
        loaded.arena,
        {},
        lg::kFixedTickSeconds
      );
      failures += expect(
        near(player.position.x, before.x) && near(player.position.y, before.y) &&
        player.position.z >= loaded.arena.min.z + player.bounds.halfHeight,
        "loaded spawn should remain in a usable collision position"
      );
    }
  }

  // Fingerprints cover score and actual balance inputs.
  {
    lg::AimScenario scenario = directOrbScenario();
    scenario.mapIdentity = 0x123456789abcdef0ULL;
    scenario.balanceIdentity = 0xfedcba9876543210ULL;
    const std::uint64_t base = lg::AimTrainer::scenarioFingerprint(scenario);
    scenario.damageScorePerPoint++;
    failures += expect(lg::AimTrainer::scenarioFingerprint(scenario) != base,
      "scenario fingerprint should include score tuning");
    lg::BalanceConfig changed = balance;
    const std::uint64_t balanceBase = lg::AimTrainer::balanceFingerprint(balance);
    changed.rocketLauncher.splashDamage++;
    failures += expect(lg::AimTrainer::balanceFingerprint(changed) != balanceBase,
      "balance fingerprint should include projectile damage tuning");
    changed = balance;
    changed.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::FreezeGun)]++;
    failures += expect(lg::AimTrainer::balanceFingerprint(changed) != balanceBase,
      "balance fingerprint should include ammo tuning");
  }

  std::array<std::uint32_t, 3> cooldowns = {2U, 1U, 0U};
  lg::advanceWeaponRuntimeCooldowns(cooldowns);
  failures += expect(
    cooldowns == std::array<std::uint32_t, 3>{1U, 0U, 0U},
    "shared cooldown seam must preserve authoritative decrement behavior"
  );

  return failures == 0 ? 0 : 1;
}
