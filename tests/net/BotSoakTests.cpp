#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

struct SoakResult {
  bool valid = true;
  bool legalDamage = true;
  bool hiddenAttack = false;
  std::uint32_t movedBots = 0;
  std::uint32_t acquiredBots = 0;
  std::uint32_t lostBots = 0;
  std::uint32_t firingBots = 0;
  std::uint32_t damagedBots = 0;
  std::uint32_t navigationBots = 0;
  std::uint32_t weaponChangedBots = 0;
  std::uint32_t recoveryBots = 0;
  std::uint32_t recoveryEvents = 0;
  std::uint32_t maximumNoProgressTicks = 0;
  std::uint64_t hash = 1469598103934665603ULL;
};

void mix(std::uint64_t& hash, std::uint32_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
}

void mix(std::uint64_t& hash, float value) {
  mix(hash, std::bit_cast<std::uint32_t>(value));
}

lg::Arena makeModerateBotArena() {
  lg::Arena arena;
  arena.min = {-16.0F, -12.0F, 0.0F};
  arena.max = {16.0F, 12.0F, 6.0F};
  arena.spawnCount = lg::kDuelPlayerCount;
  constexpr std::array<lg::Vec3, lg::kDuelPlayerCount> spawns = {{
    {-13.0F, -9.0F, 0.0F}, {-9.0F, -9.0F, 0.0F}, {-5.0F, -9.0F, 0.0F},
    {5.0F, -9.0F, 0.0F}, {9.0F, -9.0F, 0.0F}, {13.0F, -9.0F, 0.0F},
    {-13.0F, 9.0F, 0.0F}, {-9.0F, 9.0F, 0.0F}, {-5.0F, 9.0F, 0.0F},
    {5.0F, 9.0F, 0.0F}, {9.0F, 9.0F, 0.0F}, {13.0F, 9.0F, 0.0F},
    {-13.0F, 0.0F, 0.0F}, {-9.0F, 0.0F, 0.0F}, {9.0F, 0.0F, 0.0F},
    {13.0F, 0.0F, 0.0F},
  }};
  for (std::size_t index = 0; index < spawns.size(); ++index) {
    arena.spawnPositions[index] = spawns[index];
  }
  arena.walls[0].min = {-1.0F, -10.0F, 0.0F};
  arena.walls[0].max = {1.0F, -2.0F, 3.0F};
  arena.walls[1].min = {-1.0F, 2.0F, 0.0F};
  arena.walls[1].max = {1.0F, 10.0F, 3.0F};
  arena.walls[2].min = {-8.0F, -1.0F, 0.0F};
  arena.walls[2].max = {-3.0F, 1.0F, 2.5F};
  arena.walls[3].min = {3.0F, -1.0F, 0.0F};
  arena.walls[3].max = {8.0F, 1.0F, 2.5F};
  arena.wallCount = 4;
  arena.healthPickups[0] = {{-6.0F, 3.5F, 0.0F}, lg::HealthPickupType::Large};
  arena.healthPickups[1] = {{6.0F, -3.5F, 0.0F}, lg::HealthPickupType::Large};
  arena.healthPickupCount = 2;
  return arena;
}

SoakResult runSoak(std::size_t tickCount) {
  lg::LoopbackTransport transport;
  lg::ServerGame server(transport);
  SoakResult result;
  server.setArena(makeModerateBotArena());
  server.setConnectedPlayers({});
  const lg::BotRosterChange added = server.addBots(lg::kDuelPlayerCount);
  if (!added.ok || added.changed != lg::kDuelPlayerCount) {
    result.valid = false;
    return result;
  }
  server.setBotAttackMode(lg::BotAttackMode::Hard);
  const auto initialWeapons = server.snapshot().selectedWeapons;
  auto previousPlayers = server.snapshot().players;
  lg::BotRuntimeStats previousStats = server.botRuntimeStats();
  std::array<float, lg::kDuelPlayerCount> movementDistance = {};
  std::array<std::uint32_t, lg::kDuelPlayerCount> noProgressTicks = {};
  std::array<std::uint32_t, lg::kDuelPlayerCount> maximumNoProgressTicks = {};
  std::array<std::uint32_t, lg::kDuelPlayerCount> lastLegalFireTick = {};
  lastLegalFireTick.fill(std::numeric_limits<std::uint32_t>::max());
  for (std::size_t tick = 0; tick < tickCount; ++tick) {
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot& snapshot = server.snapshot();
    const lg::BotRuntimeStats& stats = server.botRuntimeStats();
    mix(result.hash, snapshot.serverTick);
    mix(result.hash, static_cast<std::uint32_t>(server.botDeterminismHash()));
    mix(result.hash, static_cast<std::uint32_t>(server.botDeterminismHash() >> 32U));
    bool legalFireThisTick = false;
    for (std::size_t slot = 0; slot < lg::kDuelPlayerCount; ++slot) {
      const lg::PlayerState& player = snapshot.players[slot];
      result.valid = result.valid && std::isfinite(player.position.x) &&
        std::isfinite(player.position.y) && std::isfinite(player.position.z) &&
        std::isfinite(player.velocity.x) && std::isfinite(player.velocity.y) &&
        std::isfinite(player.velocity.z);
      mix(result.hash, player.position.x);
      mix(result.hash, player.position.y);
      mix(result.hash, player.position.z);
      mix(result.hash, player.velocity.x);
      mix(result.hash, player.velocity.y);
      mix(result.hash, player.velocity.z);
      const bool fired = snapshot.weaponFires[slot].fired ||
        snapshot.lightningGuns[slot].active;
      legalFireThisTick = legalFireThisTick || fired;
      if (fired) lastLegalFireTick[slot] = static_cast<std::uint32_t>(tick);
    }
    bool recentLegalFire = false;
    for (std::uint32_t fireTick : lastLegalFireTick) {
      recentLegalFire = recentLegalFire ||
        (fireTick != std::numeric_limits<std::uint32_t>::max() &&
          static_cast<std::uint32_t>(tick) - fireTick <= 512U);
    }
    (void)legalFireThisTick;
    for (std::size_t slot = 0; slot < lg::kDuelPlayerCount; ++slot) {
      const lg::PlayerState& player = snapshot.players[slot];
      const float distance = std::hypot(player.position.x - previousPlayers[slot].position.x,
        player.position.y - previousPlayers[slot].position.y);
      movementDistance[slot] += distance;
      const bool active = snapshot.participatingPlayers[slot] && player.health > 0;
      const bool intendedMovement = stats.movementIntentTicks[slot] >
        previousStats.movementIntentTicks[slot];
      if (active && intendedMovement && distance < 0.003F) {
        ++noProgressTicks[slot];
        maximumNoProgressTicks[slot] = std::max(maximumNoProgressTicks[slot],
          noProgressTicks[slot]);
      } else {
        noProgressTicks[slot] = 0U;
      }
      if (player.health < previousPlayers[slot].health) {
        result.legalDamage = result.legalDamage && recentLegalFire;
      }
      previousPlayers[slot] = player;
    }
    previousStats = stats;
  }
  result.hiddenAttack = server.botHiddenAttackInvariantCount() != 0U;
  const lg::BotRuntimeStats& stats = server.botRuntimeStats();
  for (std::size_t slot = 0; slot < lg::kDuelPlayerCount; ++slot) {
    result.movedBots += movementDistance[slot] > 0.50F ? 1U : 0U;
    result.acquiredBots += stats.acquisitions[slot] > 0U ? 1U : 0U;
    result.lostBots += stats.losses[slot] > 0U ? 1U : 0U;
    result.firingBots += stats.attackCommandTicks[slot] > 0U ? 1U : 0U;
    result.navigationBots += stats.navigationCommandTicks[slot] > 0U ? 1U : 0U;
    result.weaponChangedBots += server.snapshot().selectedWeapons[slot] != initialWeapons[slot]
      ? 1U : 0U;
    result.recoveryBots += stats.recoveryEvents[slot] > 0U ? 1U : 0U;
    result.recoveryEvents += stats.recoveryEvents[slot];
    result.damagedBots += server.snapshot().players[slot].health < 100 ? 1U : 0U;
    result.maximumNoProgressTicks = std::max(result.maximumNoProgressTicks,
      maximumNoProgressTicks[slot]);
  }
  return result;
}

int expect(bool condition, const char* message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main(int argc, char** argv) {
  constexpr std::size_t kNormalTicks = 1250U;
  constexpr std::size_t kLongTicks = 7500U;
  const bool longSoak = argc == 2 && std::string_view(argv[1]) == "--long";
  if (argc > 1 && !longSoak) {
    std::cerr << "usage: lg_duel_bot_soak_tests [--long]\n";
    return 2;
  }
  const std::size_t tickCount = longSoak ? kLongTicks : kNormalTicks;
  const SoakResult first = runSoak(tickCount);
  const SoakResult second = runSoak(tickCount);
  int failures = 0;
  failures += expect(first.valid && second.valid, "soak should keep all bot simulation values finite");
  constexpr std::uint32_t kMeaningfulBots = lg::kDuelPlayerCount / 4U;
  failures += expect(first.movedBots >= kMeaningfulBots && first.acquiredBots >= kMeaningfulBots &&
    first.lostBots >= kMeaningfulBots && first.firingBots >= kMeaningfulBots,
    "soak should cover movement, acquire/loss, and legal fire for a meaningful bot fraction");
  failures += expect(first.damagedBots > 0U && first.navigationBots >= kMeaningfulBots &&
    first.weaponChangedBots > 0U && first.legalDamage,
    "soak should tie damage to legal fire and cover navigation and utility switching");
  failures += expect(first.recoveryEvents <= 64U && first.maximumNoProgressTicks <= 500U,
    "soak should bound recovery events and every active bot's no-progress run");
  failures += expect(!first.hiddenAttack,
    "bot commands should never attack a target not currently inside LOS and FOV");
  failures += expect(first.hash == second.hash,
    "fixed-seed bot soak should reproduce server-local bot state exactly");
  std::cout << "bot soak ticks=" << tickCount << " hash=" << first.hash
    << " moved_bots=" << first.movedBots
    << " acquired_bots=" << first.acquiredBots << " lost_bots=" << first.lostBots
    << " firing_bots=" << first.firingBots << " damaged_bots=" << first.damagedBots
    << " nav_bots=" << first.navigationBots << " weapon_changed_bots=" << first.weaponChangedBots
    << " recovery_bots=" << first.recoveryBots << " recovery_events=" << first.recoveryEvents
    << " max_no_progress_ticks=" << first.maximumNoProgressTicks
    << " legal_damage=" << first.legalDamage << " hidden_attack=" << first.hiddenAttack << '\n';
  return failures == 0 ? 0 : 1;
}
