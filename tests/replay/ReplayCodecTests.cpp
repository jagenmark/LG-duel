#include "replay/ReplayCodec.hpp"

#include "net/NetCodec.hpp"
#include "sim/Arena.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::replay::ReplayDemo validDemo() {
  lg::replay::ReplayDemo demo;
  demo.metadata.protocolRevision = lg::kProtocolVersion;
  demo.metadata.buildFingerprint = 0x1122334455667788ULL;
  demo.metadata.initialServerTick = 100U;
  demo.metadata.mapRevision = 7U;
  demo.metadata.mapName = "replay_test";
  demo.metadata.mapContentHash = 0x12345678U;
  demo.metadata.gameMode = lg::GameMode::Duel;
  demo.metadata.visibility = lg::replay::ReplayVisibility::DuelOnly;
  demo.metadata.players[0] = {0U, true, false, lg::Team::None, "ALPHA"};
  demo.metadata.players[1] = {1U, true, true, lg::Team::None, "BOT"};
  for (std::size_t index = 2U; index < demo.metadata.players.size(); ++index) {
    demo.metadata.players[index].slot = static_cast<std::uint8_t>(index);
  }

  lg::replay::ReplayTickInput input;
  input.tick = 100U;
  input.slots[0].present = true;
  input.slots[0].hasCommand = true;
  input.slots[0].receivedThisTick = true;
  input.slots[0].command.sequence = 12U;
  input.slots[0].command.viewYawRadians = 1.0F;
  input.slots[0].command.viewPitchRadians = -0.25F;
  input.slots[0].command.forwardMove = 1.0F;
  input.slots[0].command.attack = true;
  input.slots[0].command.weapon = lg::Weapon::RocketLauncher;
  input.slots[0].viewedServerTick = 98U;
  input.slots[0].consumedActionEdges.attack = 5U;
  input.slots[0].consumedActionEdges.attackYawRadians = 1.0F;
  input.slots[0].consumedActionEdges.attackPitchRadians = -0.25F;
  input.slots[0].consumedActionEdges.attackViewedServerTick = 98U;
  input.slots[0].consumedActionEdges.attackWeapon = lg::Weapon::RocketLauncher;
  input.slots[0].attackEdgeAccepted = true;
  input.slots[0].attackEdgeCommand = input.slots[0].command;
  input.slots[0].attackEdgeViewedServerTick = 98U;
  input.slots[1].present = true;
  input.slots[1].hasCommand = true;
  input.slots[1].command.sequence = 4U;
  input.slots[1].command.viewYawRadians = -0.5F;
  input.slots[1].command.weapon = lg::Weapon::MachineGun;
  demo.ticks.push_back(input);

  lg::replay::ReplayCheckpoint checkpoint;
  checkpoint.serverTick = 101U;
  checkpoint.mapRevision = 7U;
  checkpoint.projectileRevision = 3U;
  checkpoint.players[0].connected = true;
  checkpoint.players[0].participating = true;
  checkpoint.players[0].player.position = {1.0F, 2.0F, 3.0F};
  checkpoint.players[0].player.health = 73;
  checkpoint.players[0].weapon.selectedWeapon = lg::Weapon::RocketLauncher;
  checkpoint.players[0].weapon.ammo[lg::weaponIndex(lg::Weapon::RocketLauncher)] = 4;
  checkpoint.players[1].connected = true;
  checkpoint.players[1].participating = true;
  checkpoint.players[1].player.position = {-2.0F, 0.5F, 0.9F};
  checkpoint.players[1].player.health = 100;
  checkpoint.projectiles[0].active = true;
  checkpoint.projectiles[0].owner = 0U;
  checkpoint.projectiles[0].sequence = 14U;
  checkpoint.projectiles[0].weapon = lg::Weapon::RocketLauncher;
  checkpoint.projectiles[0].position = {1.0F, 2.0F, 3.0F};
  checkpoint.projectiles[0].previousPosition = {0.8F, 2.0F, 3.0F};
  checkpoint.projectiles[0].velocity = {20.0F, 0.0F, 0.0F};
  checkpoint.projectiles[0].projectileRadius = 3.0F;
  checkpoint.match.gameMode = lg::GameMode::Duel;
  checkpoint.match.phase = lg::MatchPhase::Live;
  checkpoint.match.scores[0] = 2U;
  checkpoint.history.push_back({100U, {}});
  checkpoint.history[0].players[0] = checkpoint.players[0].player;
  demo.checkpoints.push_back(checkpoint);
  demo.hashes.push_back({101U, lg::replay::canonicalStateHash(checkpoint)});
  demo.lethalEvents.push_back({101U, 1U, 1U, 0U, lg::Weapon::RocketLauncher, 14U, lg::replay::LethalKind::Direct});
  return demo;
}

} // namespace

int main() {
  int failures = 0;
  const lg::replay::ReplayDemo source = validDemo();
  std::vector<std::uint8_t> wire;
  std::string error;
  failures += expect(lg::replay::encodeDemo(source, wire, &error), "valid replay should encode");
  failures += expect(!wire.empty(), "encoded replay should not be empty");
  const std::vector<std::uint8_t> validWire = wire;

  lg::replay::ReplayDemo decoded;
  failures += expect(lg::replay::decodeDemo(wire, decoded, &error), "valid replay should decode");
  failures += expect(decoded.metadata.mapName == source.metadata.mapName, "metadata should round trip");
  failures += expect(decoded.ticks.size() == 1U && decoded.ticks[0].slots[0].attackEdgeAccepted,
    "accepted action edge should round trip");
  failures += expect(decoded.ticks[0].slots[0].consumedActionEdges.attackWeapon == lg::Weapon::RocketLauncher,
    "original attack weapon should round trip");
  failures += expect(decoded.checkpoints.size() == 1U && decoded.checkpoints[0].projectiles[0].sequence == 14U,
    "checkpoint projectile should round trip");
  failures += expect(decoded.hashes.size() == 1U && decoded.hashes[0].value == source.hashes[0].value,
    "state hash should round trip");
  failures += expect(lg::replay::canonicalStateHash(decoded.checkpoints[0]) == source.hashes[0].value,
    "canonical checkpoint hash should survive codec round trip");

  {
    std::vector<std::uint8_t> truncated = validWire;
    truncated.pop_back();
    lg::replay::ReplayDemo unchanged = source;
    failures += expect(!lg::replay::decodeDemo(truncated, unchanged, &error), "truncated replay should fail");
    failures += expect(unchanged.metadata.mapName == source.metadata.mapName,
      "failed decode must not partly apply the replay");
  }
  {
    std::vector<std::uint8_t> corrupt = validWire;
    corrupt.back() ^= 0x40U;
    failures += expect(!lg::replay::decodeDemo(corrupt, decoded, &error), "corrupt checksum should fail");
  }
  {
    std::vector<std::uint8_t> wrongMagic = validWire;
    wrongMagic[0] = 'X';
    failures += expect(!lg::replay::decodeDemo(wrongMagic, decoded, &error), "invalid magic should fail");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.ticks[0].slots[0].command.viewYawRadians = std::numeric_limits<float>::quiet_NaN();
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error), "non-finite command should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.checkpoints[0].projectiles[0].owner = static_cast<std::uint8_t>(lg::kDuelPlayerCount);
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error), "invalid projectile owner should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.checkpoints[0].history.clear();
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "a playable checkpoint must encode at least one lag-history frame");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.checkpoints[0].nextDeathmatchSpawnIndex = lg::Arena::kSpawnCount;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "spawn cursor outside the static arena bound should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.ticks.push_back(invalid.ticks.front());
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error), "out-of-order ticks should not encode");
  }
  {
    std::vector<std::uint8_t> trailing = validWire;
    trailing.push_back(0U);
    failures += expect(!lg::replay::decodeDemo(trailing, decoded, &error), "trailing partial chunk should fail");
  }
  return failures == 0 ? 0 : 1;
}
