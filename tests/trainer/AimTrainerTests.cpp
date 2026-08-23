#include "shared/Constants.hpp"
#include "sim/WeaponRuntime.hpp"
#include "trainer/AimTrainerMenu.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
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
  scenario.durationTicks = 3;
  scenario.weaponPolicy = lg::AimWeaponPolicy::Forced;
  scenario.forcedWeapon = lg::Weapon::Railgun;
  scenario.scoreMode = lg::AimScoreMode::Hit;
  scenario.groups[0].visual = lg::AimTargetVisual::Orb;
  scenario.groups[0].life = lg::AimTargetLife::Invincible;
  scenario.groups[0].spawnMode = lg::AimSpawnMode::FixedList;
  scenario.groups[0].fixedSpawns = {{5.0F, 0.0F, 1.55F}};
  scenario.groups[0].radius = 1.0F;
  scenario.groups[0].count = 1;
  return scenario;
}

lg::UserCommand attackCommand(std::uint32_t sequence = 1U) {
  lg::UserCommand command;
  command.sequence = sequence;
  command.attack = true;
  command.weapon = lg::Weapon::Railgun;
  return command;
}

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena = trainerArena();
  const lg::BalanceConfig balance;

  {
    lg::AimTrainer trainer(arena, balance);
    lg::AimScenario scenario = directOrbScenario();
    failures += expect(trainer.arm(scenario).ok, "valid scenario should arm");
    failures += expect(trainer.start(), "armed scenario should start");
    const lg::AimTrainerFrame& first = trainer.tick(attackCommand());
    failures += expect(first.stats.attempts == 1U && first.stats.hits == 1U,
      "instant weapon accuracy should count accepted shot and hit");
    failures += expect(first.stats.score == 1U && first.targets[0].active,
      "invincible targets should score and remain active");
    (void)trainer.tick({});
    const lg::AimTrainerFrame& result = trainer.tick({});
    failures += expect(result.naturalCompletion && result.result.ranked &&
        result.result.durationTicks == 3U,
      "duration must end on exact 125 Hz ticks and rank once");
    const lg::AimTrainerResult firstResult = result.result;
    (void)trainer.tick({});
    failures += expect(trainer.view().result.score == firstResult.score,
      "viewing after completion must not record or alter a result");
  }

  {
    lg::AimTrainer trainer(arena, balance);
    lg::AimScenario scenario = directOrbScenario();
    scenario.durationTicks = 20U;
    scenario.forcedWeapon = lg::Weapon::Shotgun;
    scenario.groups[0].life = lg::AimTargetLife::Health;
    scenario.groups[0].health = 1;
    scenario.groups[0].respawnDelayTicks = 1U;
    failures += expect(trainer.arm(scenario).ok && trainer.start(), "health scenario should start");
    lg::UserCommand shot = attackCommand();
    shot.weapon = lg::Weapon::Shotgun;
    const lg::AimTrainerFrame& frame = trainer.tick(shot);
    failures += expect(frame.stats.pelletAttempts == balance.shotgun.pelletCount &&
        frame.stats.pelletHits > 0U && frame.stats.clears == 1U && !frame.targets[0].active,
      "shotgun resolves its complete target batch before a health target respawns");
    (void)trainer.tick({});
    failures += expect(trainer.view().targets[0].active,
      "a cleared target respawns only after its configured delay");
  }

  {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
      ("lg-duel-trainer-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    lg::AimTrainerStore store(root);
    lg::AimScenario scenario = directOrbScenario();
    scenario.mapIdentity = 77U;
    scenario.balanceIdentity = 99U;
    scenario.seed = 12U;
    failures += expect(store.savePreset(scenario, false).ok, "save-as should write a local preset");
    const lg::AimTrainerPresetList presets = store.loadPresets();
    failures += expect(presets.presets.size() >= 3U, "load should retain built-ins and saved preset");
    const std::uint64_t fingerprint = lg::AimTrainer::scenarioFingerprint(scenario);
    lg::AimTrainerResult result{fingerprint, 9U, 100U, 2U, 10U, 8U, 7500U, scenario.seed, true};
    failures += expect(store.recordNaturalResult(result).ok, "natural result should store locally");
    failures += expect(store.leaderboard(fingerprint).size() == 1U,
      "leaderboard should match the exact scenario fingerprint");
    result.ranked = false;
    failures += expect(store.recordNaturalResult(result).ok && store.leaderboard(fingerprint).size() == 1U,
      "aborted results are not ranked");
    {
      std::ofstream corrupt(store.directory() / "presets.json", std::ios::trunc);
      corrupt << "{not json";
    }
    const lg::AimTrainerPresetList recovered = store.loadPresets();
    failures += expect(!recovered.warning.empty() && recovered.presets.size() == 2U,
      "corrupt presets must warn while built-ins remain usable");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
      ("lg-duel-trainer-menu-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    lg::AimTrainer trainer(arena, balance);
    lg::AimTrainerStore store(root);
    lg::AimTrainerMenu menu(trainer, store);
    lg::AimScenario scenario = directOrbScenario();
    scenario.durationTicks = 1U;
    scenario.mapIdentity = 7U;
    scenario.balanceIdentity = 8U;
    menu.edit(scenario);
    failures += expect(menu.start().ok, "menu should arm and start its draft");
    menu.tick(attackCommand());
    const std::size_t recorded = menu.leaderboard().size();
    menu.tick({});
    failures += expect(recorded == 1U && menu.leaderboard().size() == recorded,
      "menu stores a natural result once and repeat viewing has no write effect");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  {
    std::array<std::uint32_t, 3> cooldowns = {2U, 1U, 0U};
    lg::advanceWeaponRuntimeCooldowns(cooldowns);
    failures += expect(cooldowns == std::array<std::uint32_t, 3>{1U, 0U, 0U},
      "shared cooldown seam must preserve online cooldown decrement behavior");
  }

  return failures == 0 ? 0 : 1;
}
