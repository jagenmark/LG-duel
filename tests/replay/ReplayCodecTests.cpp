#include "replay/ReplayCodec.hpp"

#include "net/NetCodec.hpp"
#include "sim/Arena.hpp"

#include <algorithm>
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

void writeLittleEndian(
  std::vector<std::uint8_t>& bytes,
  std::size_t offset,
  std::uint64_t value,
  std::size_t width
) {
  for (std::size_t index = 0U; index < width; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

lg::replay::ReplayDemo validDemo() {
  lg::replay::ReplayDemo demo;
  demo.metadata.protocolRevision = lg::kProtocolVersion;
  demo.metadata.buildFingerprint = lg::replay::kReplayBuildFingerprint;
  demo.metadata.initialServerTick = 100U;
  demo.metadata.mapRevision = 7U;
  demo.metadata.mapName = "replay_test";
  demo.metadata.mapContentHash = 0x12345678U;
  demo.metadata.gameMode = lg::GameMode::FreeForAll;
  demo.metadata.visibility = lg::replay::ReplayVisibility::DeveloperFull;
  demo.metadata.gameplayConfig.balance.rocketLauncher.speed = 31.0F;
  demo.metadata.gameplayConfig.movementTuning.gravity = 27.0F;
  demo.metadata.gameplayConfig.mcguffinConfig.throwSpeed = 14.0F;
  demo.metadata.gameplayConfigHash =
    lg::replay::canonicalGameplayConfigHash(demo.metadata.gameplayConfig);
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
  checkpoint.gameplayConfigHash = demo.metadata.gameplayConfigHash;
  checkpoint.lethalSequence = 4U;
  checkpoint.damageTakenSequences[0] = 17U;
  checkpoint.damageTakenSequences[1] = 23U;
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
  checkpoint.match.gameMode = lg::GameMode::FreeForAll;
  checkpoint.match.phase = lg::MatchPhase::Live;
  checkpoint.match.scores[0] = -2;
  checkpoint.match.scores[1] = std::numeric_limits<lg::PlayerScore>::min();
  checkpoint.match.scores[2] = std::numeric_limits<lg::PlayerScore>::max();
  checkpoint.history.push_back({100U, {}});
  checkpoint.history[0].players[0] = checkpoint.players[0].player;
  demo.checkpoints.push_back(checkpoint);
  demo.hashes.push_back({101U, lg::replay::canonicalStateHash(checkpoint)});
  demo.lethalEvents.push_back({101U, 1U, 1U, 0U, lg::Weapon::RocketLauncher, 14U,
    lg::replay::LethalKind::Direct, 1U});
  demo.lethalEvents.push_back({101U, 1U, 0U, 1U, lg::Weapon::RocketLauncher, 15U,
    lg::replay::LethalKind::Splash, 2U});
  demo.lethalEvents.push_back({101U, 1U, 1U, 1U, lg::Weapon::RocketLauncher, 16U,
    lg::replay::LethalKind::Self, 3U});
  demo.lethalEvents.push_back({101U, 1U, 0U, lg::replay::kNoReplayPlayer,
    lg::Weapon::LightningGun, 0U, lg::replay::LethalKind::World, 4U});
  lg::replay::ReplayAuthorityBoundary boundary;
  boundary.tick = 101U;
  boundary.checkpoint = checkpoint;
  boundary.configurationRevision = 2U;
  boundary.gameMode = demo.metadata.gameMode;
  boundary.matchRules = demo.metadata.matchRules;
  boundary.players = demo.metadata.players;
  boundary.gameplayConfig = demo.metadata.gameplayConfig;
  boundary.gameplayConfig.movementTuning.gravity = 28.0F;
  boundary.checkpoint.gameplayConfigHash =
    lg::replay::canonicalGameplayConfigHash(boundary.gameplayConfig);
  demo.authorityBoundaries.push_back(boundary);
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
  failures += expect(
    decoded.metadata.gameplayConfigHash == source.metadata.gameplayConfigHash &&
      decoded.metadata.gameplayConfig.balance.rocketLauncher.speed == 31.0F &&
      decoded.metadata.gameplayConfig.movementTuning.gravity == 27.0F &&
      decoded.metadata.gameplayConfig.mcguffinConfig.throwSpeed == 14.0F,
    "full gameplay configuration should round trip"
  );
  failures += expect(decoded.ticks.size() == 1U && decoded.ticks[0].slots[0].attackEdgeAccepted,
    "accepted action edge should round trip");
  failures += expect(decoded.ticks.size() == 1U && std::all_of(
    decoded.ticks[0].slots.begin() + 2, decoded.ticks[0].slots.end(),
    [](const lg::replay::ReplaySlotInput& slot) {
      return !slot.present && !slot.hasCommand && !slot.receivedThisTick &&
        slot.command.sequence == 0U && slot.command.planarAim &&
        slot.viewedServerTick == 0U && slot.consumedActionEdges.attack == 0U &&
        !slot.attackEdgeAccepted && !slot.mcguffinThrowAccepted;
    }), "sparse absent slots should round trip as fully default input state");
  failures += expect(decoded.ticks[0].slots[0].consumedActionEdges.attackWeapon == lg::Weapon::RocketLauncher,
    "original attack weapon should round trip");
  failures += expect(decoded.checkpoints.size() == 1U && decoded.checkpoints[0].projectiles[0].sequence == 14U,
    "checkpoint projectile should round trip");
  failures += expect(
      decoded.metadata.gameMode == lg::GameMode::FreeForAll &&
      decoded.checkpoints[0].match.gameMode == lg::GameMode::FreeForAll &&
      decoded.checkpoints[0].match.scores[0] == -2 &&
      decoded.checkpoints[0].match.scores[1] ==
        std::numeric_limits<lg::PlayerScore>::min() &&
      decoded.checkpoints[0].match.scores[2] ==
        std::numeric_limits<lg::PlayerScore>::max(),
    "FFA mode and signed score bounds should round trip through replay data"
  );
  failures += expect(
    decoded.checkpoints.size() == 1U &&
      decoded.checkpoints[0].damageTakenSequences == source.checkpoints[0].damageTakenSequences,
    "damage-event sequences should round trip in replay checkpoints"
  );
  failures += expect(decoded.hashes.size() == 1U && decoded.hashes[0].value == source.hashes[0].value,
    "state hash should round trip");
  failures += expect(decoded.lethalEvents.size() == 4U &&
    decoded.lethalEvents[0].sequence == 1U &&
    decoded.lethalEvents[1].kind == lg::replay::LethalKind::Splash &&
    decoded.lethalEvents[1].projectileSequence == 15U &&
    decoded.lethalEvents[2].kind == lg::replay::LethalKind::Self &&
    decoded.lethalEvents[3].kind == lg::replay::LethalKind::World,
    "lethal sequence and all provenance kinds should round trip");
  failures += expect(decoded.authorityBoundaries.size() == 1U &&
    decoded.authorityBoundaries[0].configurationRevision == 2U &&
    decoded.authorityBoundaries[0].checkpoint.gameplayConfigHash ==
      lg::replay::canonicalGameplayConfigHash(decoded.authorityBoundaries[0].gameplayConfig),
    "authority boundary and its config snapshot should round trip");
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
    std::vector<std::uint8_t> oldVersion = validWire;
    oldVersion[4] = 4U;
    oldVersion[5] = 0U;
    failures += expect(!lg::replay::decodeDemo(oldVersion, decoded, &error),
      "v4 replay bytes must be rejected instead of reinterpreted as v5");
  }
  {
    std::vector<std::uint8_t> unsupportedSimulation = validWire;
    writeLittleEndian(
      unsupportedSimulation,
      40U,
      lg::replay::kReplaySimulationRevision + 1U,
      4U
    );
    failures += expect(
      !lg::replay::decodeDemo(unsupportedSimulation, decoded, &error) &&
        error == "replay simulation revision is incompatible",
      "unsupported simulation revisions should fail with a distinct diagnostic"
    );
  }
  {
    std::vector<std::uint8_t> unsupportedProtocol = validWire;
    writeLittleEndian(
      unsupportedProtocol,
      20U,
      lg::replay::kReplayProtocolRevision + 1U,
      4U
    );
    failures += expect(
      !lg::replay::decodeDemo(unsupportedProtocol, decoded, &error) &&
        error == "replay protocol revision is incompatible",
      "unsupported protocol revisions should fail with a distinct diagnostic"
    );
  }
  {
    std::vector<std::uint8_t> unsupportedBuild = validWire;
    writeLittleEndian(
      unsupportedBuild,
      24U,
      lg::replay::kReplayBuildFingerprint ^ 1ULL,
      8U
    );
    failures += expect(
      !lg::replay::decodeDemo(unsupportedBuild, decoded, &error) &&
        error == "replay build fingerprint is incompatible",
      "unsupported build fingerprints should fail with a distinct diagnostic"
    );
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.metadata.gameMode = static_cast<lg::GameMode>(255);
    failures += expect(
      !lg::replay::encodeDemo(invalid, wire, &error),
      "a replay game mode outside the enum range should not encode"
    );
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.metadata.simulationRevision = lg::replay::kReplaySimulationRevision + 1U;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "an unsupported replay simulation revision should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.ticks[0].slots[0].command.viewYawRadians = std::numeric_limits<float>::quiet_NaN();
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error), "non-finite command should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.metadata.gameplayConfig.weaponDamage.railgunDamage = -1;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "negative replay weapon damage should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.metadata.gameplayConfig.balance.railgun.damage = -1;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "negative replay hitscan damage should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.metadata.gameplayConfig.balance.revolver.knockback = -1.0F;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "negative replay hitscan knockback should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.lethalEvents[1].sequence = invalid.lethalEvents[0].sequence;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "duplicate lethal sequences in one tick should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.ticks[0].slots[3].hasCommand = true;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error) && wire.empty(),
      "a non-present slot with command state must fail without returning partial bytes");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.ticks[0].slots[3].command.planarAim = false;
    invalid.ticks[0].slots[3].command.sequence = 1U;
    invalid.ticks[0].slots[3].viewedServerTick = 1U;
    invalid.ticks[0].slots[3].consumedActionEdges.attack = 1U;
    invalid.ticks[0].slots[3].jumpEdgeAccepted = true;
    invalid.ticks[0].slots[3].dashEdgeAccepted = true;
    invalid.ticks[0].slots[3].attackEdgeAccepted = true;
    invalid.ticks[0].slots[3].attackEdgeCommand.sequence = 1U;
    invalid.ticks[0].slots[3].attackEdgeViewedServerTick = 1U;
    invalid.ticks[0].slots[3].mcguffinThrowAccepted = true;
    invalid.ticks[0].slots[3].mcguffinThrowCommand.sequence = 1U;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error) && wire.empty(),
      "a non-present slot must reject non-default command, edge, and throw fields");
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
