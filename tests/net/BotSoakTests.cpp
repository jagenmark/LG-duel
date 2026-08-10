#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

struct SoakResult {
  bool valid = true;
  bool moved = false;
  bool acquired = false;
  bool lost = false;
  bool fired = false;
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

SoakResult runSoak() {
  lg::LoopbackTransport transport;
  lg::ServerGame server(transport);
  server.setMapDirectory("maps");
  SoakResult result;
  if (!server.loadRequestedMap("overkill_import") || !server.addBots(16U).ok) {
    result.valid = false;
    return result;
  }
  server.setBotAttackMode(lg::BotAttackMode::Hard);
  const auto start = server.snapshot().players;
  std::array<bool, lg::kDuelPlayerCount> hadTarget = {};
  for (std::size_t tick = 0; tick < 7500U; ++tick) {
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
      }
      result.fired = result.fired || snapshot.weaponFires[slot].fired ||
        snapshot.lightningGuns[slot].active;
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

int main() {
  const SoakResult first = runSoak();
  const SoakResult second = runSoak();
  int failures = 0;
  failures += expect(first.valid && second.valid, "soak should keep all bot simulation values finite");
  failures += expect(first.moved && first.acquired && first.fired,
    "soak should cover movement, acquisition, and ordinary weapon fire");
  failures += expect(!first.hiddenAttack, "bot commands should never attack without a visible target");
  failures += expect(first.hash == second.hash,
    "fixed-seed bot soak should reproduce server-local bot state exactly");
  std::cout << "bot soak hash=" << first.hash << " recoveries=" << first.recoveries
    << " acquired=" << first.acquired << " lost=" << first.lost
    << " fired=" << first.fired << '\n';
  return failures == 0 ? 0 : 1;
}
