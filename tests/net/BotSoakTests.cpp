#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct SoakResult {
  bool valid = true;
  bool moved = false;
  bool acquired = false;
  bool lost = false;
  bool fired = false;
  bool damaged = false;
  bool usedNavigation = false;
  bool changedWeapon = false;
  bool hiddenAttack = false;
  std::uint32_t recoveries = 0;
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
  const auto start = server.snapshot().players;
  const auto initialWeapons = server.snapshot().selectedWeapons;
  std::array<bool, lg::kDuelPlayerCount> hadTarget = {};
  for (std::size_t tick = 0; tick < tickCount; ++tick) {
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot& snapshot = server.snapshot();
    mix(result.hash, snapshot.serverTick);
    mix(result.hash, static_cast<std::uint32_t>(server.botDeterminismHash()));
    mix(result.hash, static_cast<std::uint32_t>(server.botDeterminismHash() >> 32U));
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
      if (tick % 25U == 0U) {
        const std::string debug = server.botDebugString(slot);
        const bool target = debug.find("target=none") == std::string::npos;
        result.acquired = result.acquired || (!hadTarget[slot] && target);
        result.lost = result.lost || (hadTarget[slot] && !target);
        hadTarget[slot] = target;
        result.recoveries += debug.find("recovery=1") != std::string::npos ? 1U : 0U;
        result.usedNavigation = result.usedNavigation ||
          debug.find("waypoint=none") == std::string::npos;
      }
      result.fired = result.fired || snapshot.weaponFires[slot].fired ||
        snapshot.lightningGuns[slot].active;
      result.damaged = result.damaged || player.health < 100;
      result.changedWeapon = result.changedWeapon ||
        snapshot.selectedWeapons[slot] != initialWeapons[slot];
    }
  }
  result.hiddenAttack = server.botHiddenAttackInvariantCount() != 0U;
  for (std::size_t slot = 0; slot < lg::kDuelPlayerCount; ++slot) {
    const lg::Vec3 delta = server.snapshot().players[slot].position - start[slot].position;
    result.moved = result.moved || std::hypot(delta.x, delta.y) > 0.50F;
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
  failures += expect(first.moved && first.acquired && first.lost && first.fired,
    "soak should cover movement, acquisition, loss, and ordinary weapon fire");
  failures += expect(first.damaged && first.usedNavigation && first.changedWeapon,
    "soak should cover legal damage, navigation, and utility weapon switching");
  failures += expect(first.recoveries <= 64U,
    "soak should keep unresolved navigation recovery bounded");
  failures += expect(!first.hiddenAttack, "bot commands should never attack without a visible target");
  failures += expect(first.hash == second.hash,
    "fixed-seed bot soak should reproduce server-local bot state exactly");
  std::cout << "bot soak ticks=" << tickCount << " hash=" << first.hash
    << " recoveries=" << first.recoveries
    << " acquired=" << first.acquired << " lost=" << first.lost
    << " fired=" << first.fired << " damaged=" << first.damaged
    << " nav=" << first.usedNavigation << " weapon_change=" << first.changedWeapon << '\n';
  return failures == 0 ? 0 : 1;
}
