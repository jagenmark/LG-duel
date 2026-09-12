#include "replay/ReplayCodec.hpp"

#include "net/NetCodec.hpp"
#include "sim/Arena.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

lg::GlideDuelState glideFixture() {
  lg::GlideDuelState state;
  state.arenaReady = true;
  state.roundNumber = 17U;
  state.pickupPositions = {{{1.25F, -2.5F, 3.75F}, {4.5F, 5.25F, -6.75F}, {-7.25F, 8.5F, 9.75F}}};
  state.claimedMask = 5U;
  state.buffs = {lg::GlidePowerup::Surge, lg::GlidePowerup::Guard};
  state.buffTicks = {750U, 123U};
  state.pickupSequence = 0x12345678U;
  state.lastCollector = 1U;
  state.lastPickup = lg::GlidePowerup::Repair;
  state.announcementTicks = 250U;
  return state;
}

bool sameGlide(const lg::GlideDuelState& a, const lg::GlideDuelState& b) {
  for (std::size_t i = 0; i < 3; ++i) {
    if (a.pickupPositions[i].x != b.pickupPositions[i].x ||
        a.pickupPositions[i].y != b.pickupPositions[i].y ||
        a.pickupPositions[i].z != b.pickupPositions[i].z) return false;
  }
  return a.arenaReady == b.arenaReady && a.roundNumber == b.roundNumber && a.claimedMask == b.claimedMask &&
    a.buffs == b.buffs && a.buffTicks == b.buffTicks && a.pickupSequence == b.pickupSequence &&
    a.lastCollector == b.lastCollector && a.lastPickup == b.lastPickup &&
    a.announcementTicks == b.announcementTicks;
}

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

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::uint32_t replayCrc32(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t crc = 0xffffffffU;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

void appendTickChunk(
  std::vector<std::uint8_t>& bytes,
  std::uint32_t tick
) {
  std::vector<std::uint8_t> payload;
  appendU32(payload, tick);
  payload.push_back(0U);
  payload.push_back(0U);
  bytes.push_back(static_cast<std::uint8_t>(
    lg::replay::ReplayChunkType::TickInputs
  ));
  appendU32(bytes, static_cast<std::uint32_t>(payload.size()));
  appendU32(bytes, replayCrc32(payload));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
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
  demo.metadata.gameplayConfig.movementTuning.glideMovementEnabled = true;
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
  checkpoint.rocketExplosionSequence = 31U;
  checkpoint.fragEventSequence = 32U;
  checkpoint.grenadeBounceEventSequence = 33U;
  checkpoint.rocketExplosionNextSlot = 14U;
  checkpoint.fragEventNextSlot = 15U;
  checkpoint.grenadeBounceEventNextSlot = 1U;
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
    lg::replay::LethalKind::Direct, 1U, true});
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

  const auto expectInvalidGameplayConfig = [&](auto mutate, std::string_view message) {
    lg::replay::ReplayDemo invalid = source;
    mutate(invalid.metadata.gameplayConfig);
    invalid.metadata.gameplayConfigHash =
      lg::replay::canonicalGameplayConfigHash(invalid.metadata.gameplayConfig);
    return expect(!lg::replay::encodeDemo(invalid, wire, &error), message);
  };

  lg::replay::ReplayDemo decoded;
  failures += expect(lg::replay::decodeDemo(wire, decoded, &error), "valid replay should decode");
  failures += expect(decoded.metadata.mapName == source.metadata.mapName, "metadata should round trip");
  failures += expect(
      decoded.metadata.gameplayConfigHash == source.metadata.gameplayConfigHash &&
      decoded.metadata.gameplayConfig.balance.rocketLauncher.speed == 31.0F &&
      decoded.metadata.gameplayConfig.movementTuning.glideMovementEnabled &&
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
  failures += expect(
    decoded.checkpoints.size() == 1U &&
      decoded.checkpoints[0].rocketExplosionSequence == 31U &&
      decoded.checkpoints[0].fragEventSequence == 32U &&
      decoded.checkpoints[0].grenadeBounceEventSequence == 33U &&
      decoded.checkpoints[0].rocketExplosionNextSlot == 14U &&
      decoded.checkpoints[0].fragEventNextSlot == 15U &&
      decoded.checkpoints[0].grenadeBounceEventNextSlot == 1U,
    "global combat-event stream state should round trip in replay checkpoints"
  );
  failures += expect(decoded.hashes.size() == 1U && decoded.hashes[0].value == source.hashes[0].value,
    "state hash should round trip");
  failures += expect(decoded.lethalEvents.size() == 4U &&
    decoded.lethalEvents[0].sequence == 1U && decoded.lethalEvents[0].headshot &&
    !decoded.lethalEvents[1].headshot &&
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
    oldVersion[4] = static_cast<std::uint8_t>(
      lg::replay::kReplayFormatVersion - 1U
    );
    oldVersion[5] = 0U;
    failures += expect(
      !lg::replay::decodeDemo(oldVersion, decoded, &error) &&
        error == "replay version is incompatible",
      "the previous replay layout must be rejected as unsupported"
    );
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
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.weaponDamage.railgunDamage = -1; },
      "negative replay weapon damage should not encode"
    );
  }
  {
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.railgun.damage = -1; },
      "negative replay hitscan damage should not encode"
    );
  }
  {
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.revolver.knockback = -1.0F; },
      "negative replay hitscan knockback should not encode"
    );
  }
  {
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.machineGun.range = -1.0F; },
      "negative machine-gun range should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.machineGun.damage = -1; },
      "negative machine-gun damage should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.machineGun.eyeHeight = -1.0F; },
      "negative machine-gun eye height should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.machineGun.knockback = -1.0F; },
      "negative machine-gun knockback should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.machineGun.spreadRadians = -1.0F; },
      "negative machine-gun spread should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.machineGun.headshotMultiplier = -1.0F; },
      "negative machine-gun headshot multiplier should not encode"
    );
  }
  {
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.range = -1.0F; },
      "negative shotgun range should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.pelletCount = 0U; },
      "zero shotgun pellet count should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.damagePerPellet = -1; },
      "negative shotgun damage should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.spreadRadians = -1.0F; },
      "negative shotgun spread should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.eyeHeight = -1.0F; },
      "negative shotgun eye height should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.knockback = -1.0F; },
      "negative shotgun knockback should not encode"
    );
    failures += expectInvalidGameplayConfig(
      [](auto& config) { config.balance.shotgun.headshotMultiplier = -1.0F; },
      "negative shotgun headshot multiplier should not encode"
    );
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
    invalid.checkpoints[0].fragEventNextSlot = lg::kDuelPlayerCount;
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error),
      "combat-event cursor outside its ring should not encode");
  }
  {
    lg::replay::ReplayDemo invalid = source;
    invalid.ticks.push_back(invalid.ticks.front());
    failures += expect(!lg::replay::encodeDemo(invalid, wire, &error), "out-of-order ticks should not encode");
  }
  {
    const std::uint32_t metadataBytes =
      static_cast<std::uint32_t>(validWire[8]) |
      (static_cast<std::uint32_t>(validWire[9]) << 8U) |
      (static_cast<std::uint32_t>(validWire[10]) << 16U) |
      (static_cast<std::uint32_t>(validWire[11]) << 24U);
    std::vector<std::uint8_t> compactExpansion(
      validWire.begin(),
      validWire.begin() + static_cast<std::ptrdiff_t>(16U + metadataBytes)
    );
    for (std::uint32_t index = 0U; index < 12U; ++index) {
      appendTickChunk(compactExpansion, source.metadata.initialServerTick + index);
    }
    const std::size_t smallResidentLimit =
      sizeof(lg::replay::ReplayDemo) +
      8U * sizeof(lg::replay::ReplayTickInput);
    lg::replay::ReplayDemo unchanged = source;
    failures += expect(
      !lg::replay::decodeDemo(
        compactExpansion,
        unchanged,
        &error,
        smallResidentLimit
      ) && error == "replay decoded data exceeds resident-memory limit" &&
        unchanged.metadata.mapName == source.metadata.mapName,
      "compact replay records must be rejected before native expansion exceeds its budget"
    );
    unchanged = source;
    failures += expect(
      !lg::replay::decodeDemo(
        compactExpansion,
        unchanged,
        &error,
        lg::replay::kMaxReplayDecodedResidentBytes,
        8U
      ) && error == "replay has too many input ticks for decode limit" &&
        unchanged.metadata.mapName == source.metadata.mapName,
      "remote-style decode must reject a sparse replay at its explicit tick cap"
    );
  }
  {
    std::vector<std::uint8_t> trailing = validWire;
    trailing.push_back(0U);
    failures += expect(!lg::replay::decodeDemo(trailing, decoded, &error), "trailing partial chunk should fail");
  }
  {
    auto demo = validDemo();
    demo.checkpoints.front().glide = glideFixture();
    lg::replay::ReplayDemo roundtrip;
    std::vector<std::uint8_t> encoded;
    failures += expect(lg::replay::encodeDemo(demo, encoded, &error) &&
      lg::replay::decodeDemo(encoded, roundtrip, &error) &&
      sameGlide(demo.checkpoints.front().glide, roundtrip.checkpoints.front().glide),
      "replay preserves every Glide state field");
    const std::array<std::uint8_t, 12> marker = {0, 0, 160, 63, 0, 0, 32, 192, 0, 0, 112, 64};
    const auto found = std::search(encoded.begin(), encoded.end(), marker.begin(), marker.end());
    failures += expect(found != encoded.end(), "checkpoint contains Glide position payload");
    if (found != encoded.end()) {
      const auto start = static_cast<std::size_t>(found - encoded.begin()) - 5U;
      const auto read32 = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(encoded[offset]) |
          (static_cast<std::uint32_t>(encoded[offset + 1]) << 8U) |
          (static_cast<std::uint32_t>(encoded[offset + 2]) << 16U) |
          (static_cast<std::uint32_t>(encoded[offset + 3]) << 24U);
      };
      std::size_t chunk = 16U + read32(8U);
      while (chunk + 9U + read32(chunk + 1U) <= start) chunk += 9U + read32(chunk + 1U);
      for (const auto mutation : std::array<std::pair<std::size_t, std::uint8_t>, 9>{{
          {0U, 2U}, {41U, 8U}, {42U, 3U}, {45U, 3U}, {44U, 0U},
          {52U, 2U}, {53U, 4U}, {54U, 251U}, {8U, 127U}}}) {
        auto corrupt = encoded;
        corrupt[start + mutation.first] = mutation.second;
        if (mutation.first == 44U) corrupt[start + 45U] = 0U;
        const std::vector<std::uint8_t> payload(corrupt.begin() + static_cast<std::ptrdiff_t>(chunk + 9U),
          corrupt.begin() + static_cast<std::ptrdiff_t>(chunk + 9U + read32(chunk + 1U)));
        writeLittleEndian(corrupt, chunk + 5U, replayCrc32(payload), 4U);
        failures += expect(!lg::replay::decodeDemo(corrupt, roundtrip, &error),
          "invalid Glide checkpoint payload rejects with valid chunk checksum");
      }
    }
    const auto original = demo.checkpoints.front();
    const auto hash = lg::replay::canonicalStateHash(original);
    for (unsigned field = 0; field < 20; ++field) {
      auto changed = original;
      auto& g = changed.glide;
      if (field < 9) {
        auto& p = g.pickupPositions[field / 3];
        if (field % 3 == 0) p.x += 1.0F;
        if (field % 3 == 1) p.y += 1.0F;
        if (field % 3 == 2) p.z += 1.0F;
      } else switch (field) {
      case 9: ++g.roundNumber; break;
      case 10: g.claimedMask = 6U; break;
      case 11: g.buffs[0] = lg::GlidePowerup::Guard; break;
      case 12: g.buffs[1] = lg::GlidePowerup::Surge; break;
      case 13: --g.buffTicks[0]; break;
      case 14: --g.buffTicks[1]; break;
      case 15: ++g.pickupSequence; break;
      case 16: g.lastCollector = 0U; break;
      case 17: g.lastPickup = lg::GlidePowerup::Surge; break;
      case 18: --g.announcementTicks; break;
      case 19: g.arenaReady = false; break;
      }
      failures += expect(lg::replay::canonicalStateHash(changed) != hash &&
        lg::replay::canonicalStateHash(changed) != 0U, "every Glide field affects canonical hash");
    }
    for (unsigned invalid = 0; invalid < 8; ++invalid) {
      auto changed = original;
      switch (invalid) {
      case 0: changed.glide.claimedMask = 8U; break;
      case 1: changed.glide.buffs[0] = lg::GlidePowerup::Repair; break;
      case 2: changed.glide.buffTicks[0] = 751U; break;
      case 3: changed.glide.buffTicks[0] = 0U; break;
      case 4: changed.glide.lastCollector = 2U; break;
      case 5: changed.glide.lastPickup = static_cast<lg::GlidePowerup>(4U); break;
      case 6: changed.glide.announcementTicks = 251U; break;
      case 7: changed.glide.pickupPositions[2].x = std::numeric_limits<float>::quiet_NaN(); break;
      }
      failures += expect(!lg::replay::validateReplayCheckpoint(changed) &&
        lg::replay::canonicalStateHash(changed) == 0U, "invalid Glide checkpoint rejects serialization and hashing");
    }
  }

  return failures == 0 ? 0 : 1;
}
