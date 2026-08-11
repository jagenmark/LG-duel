#include "net/NetCodec.hpp"
#include "net/LoopbackTransport.hpp"
#include "sim/MovementModes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

lg::MapDescriptor testMapDescriptor() {
  return {"testmap", 0x12345678U};
}

bool expandSnapshotForMutation(
  const lg::WirePacket& wire,
  lg::WirePacket& expanded
) {
  constexpr std::size_t headerBytes = 12U;
  constexpr std::uint8_t compressedFlag = 1U;
  constexpr std::size_t minMatchBytes = 3U;
  if (wire.size() < headerBytes) return false;
  if (wire[7] == 0U) {
    expanded = wire;
    return true;
  }
  if (wire[7] != compressedFlag) return false;

  const std::size_t expandedPayloadBytes =
    static_cast<std::size_t>(wire[headerBytes]) |
    (static_cast<std::size_t>(wire[headerBytes + 1U]) << 8U);
  lg::WirePacket payload;
  payload.reserve(expandedPayloadBytes);
  std::size_t inputOffset = headerBytes + 2U;
  while (inputOffset < wire.size() && payload.size() < expandedPayloadBytes) {
    const std::uint8_t control = wire[inputOffset++];
    for (std::size_t token = 0U;
         token < 8U && payload.size() < expandedPayloadBytes;
         ++token) {
      if ((control & (std::uint8_t{1} << token)) == 0U) {
        if (inputOffset >= wire.size()) return false;
        payload.push_back(wire[inputOffset++]);
        continue;
      }
      if (inputOffset + 2U > wire.size()) return false;
      const std::uint16_t encoded = static_cast<std::uint16_t>(wire[inputOffset]) |
        (static_cast<std::uint16_t>(wire[inputOffset + 1U]) << 8U);
      inputOffset += 2U;
      const std::size_t distance = (encoded & 0x0FFFU) + 1U;
      const std::size_t length = (encoded >> 12U) + minMatchBytes;
      if (distance > payload.size() ||
          length > expandedPayloadBytes - payload.size()) return false;
      const std::size_t matchOffset = payload.size() - distance;
      for (std::size_t index = 0U; index < length; ++index) {
        payload.push_back(payload[matchOffset + index]);
      }
    }
  }
  if (inputOffset != wire.size() || payload.size() != expandedPayloadBytes) {
    return false;
  }

  expanded.assign(wire.begin(), wire.begin() + headerBytes);
  expanded[7] = 0U;
  expanded[8] = static_cast<std::uint8_t>(expandedPayloadBytes);
  expanded[9] = static_cast<std::uint8_t>(expandedPayloadBytes >> 8U);
  expanded.insert(expanded.end(), payload.begin(), payload.end());
  return true;
}

bool compactSnapshotForMutation(
  lg::WirePacket& wire,
  std::size_t forceLiteralOffset
) {
  constexpr std::size_t headerBytes = 12U;
  constexpr std::size_t matchWindow = 4096U;
  constexpr std::size_t minMatchBytes = 3U;
  constexpr std::size_t maxMatchBytes = 18U;
  constexpr std::uint8_t compressedFlag = 1U;
  if (wire.size() < headerBytes || forceLiteralOffset >= wire.size()) return false;

  const std::size_t payloadBytes = wire.size() - headerBytes;
  lg::WirePacket compressed = {
    static_cast<std::uint8_t>(payloadBytes),
    static_cast<std::uint8_t>(payloadBytes >> 8U),
  };
  std::array<std::int32_t, matchWindow> latest = {};
  latest.fill(-1);
  std::size_t inputOffset = headerBytes;
  while (inputOffset < wire.size()) {
    const std::size_t controlOffset = compressed.size();
    compressed.push_back(0U);
    std::uint8_t control = 0U;
    for (std::size_t token = 0U; token < 8U && inputOffset < wire.size(); ++token) {
      std::size_t matchLength = 0U;
      std::size_t matchDistance = 0U;
      if (inputOffset + minMatchBytes <= wire.size()) {
        const std::uint32_t matchValue =
          static_cast<std::uint32_t>(wire[inputOffset]) |
          (static_cast<std::uint32_t>(wire[inputOffset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(wire[inputOffset + 2U]) << 16U);
        const std::size_t hash =
          static_cast<std::size_t>((matchValue * 2654435761U) >> 20U);
        const std::int32_t candidate = latest[hash];
        latest[hash] = static_cast<std::int32_t>(inputOffset);
        if (candidate >= static_cast<std::int32_t>(headerBytes)) {
          const std::size_t candidateOffset = static_cast<std::size_t>(candidate);
          const std::size_t distance = inputOffset - candidateOffset;
          if (distance <= matchWindow) {
            const std::size_t limit = std::min(maxMatchBytes, wire.size() - inputOffset);
            while (matchLength < limit &&
                   wire[candidateOffset + matchLength] ==
                     wire[inputOffset + matchLength]) {
              ++matchLength;
            }
            if (inputOffset <= forceLiteralOffset &&
                forceLiteralOffset < inputOffset + matchLength) {
              matchLength = 0U;
            }
            if (matchLength >= minMatchBytes) matchDistance = distance;
          }
        }
      }
      if (matchLength >= minMatchBytes) {
        control |= static_cast<std::uint8_t>(1U << token);
        const std::uint16_t encoded = static_cast<std::uint16_t>(
          (matchDistance - 1U) | ((matchLength - minMatchBytes) << 12U)
        );
        compressed.push_back(static_cast<std::uint8_t>(encoded));
        compressed.push_back(static_cast<std::uint8_t>(encoded >> 8U));
        inputOffset += matchLength;
      } else {
        compressed.push_back(wire[inputOffset++]);
      }
    }
    compressed[controlOffset] = control;
  }
  if (compressed.size() >= payloadBytes) return false;

  lg::WirePacket compacted(wire.begin(), wire.begin() + headerBytes);
  compacted[7] = compressedFlag;
  compacted.insert(compacted.end(), compressed.begin(), compressed.end());
  const std::size_t compactedPayloadBytes = compacted.size() - headerBytes;
  if (compacted.size() > lg::kMaxUdpApplicationDatagramBytes) return false;
  compacted[8] = static_cast<std::uint8_t>(compactedPayloadBytes);
  compacted[9] = static_cast<std::uint8_t>(compactedPayloadBytes >> 8U);
  wire = std::move(compacted);
  return true;
}

std::size_t findDamageEventPayload(
  const lg::WirePacket& wire,
  const lg::DamageTakenEvent& event
) {
  const std::array<std::uint8_t, 8U> payload = {
    static_cast<std::uint8_t>(event.sequence),
    static_cast<std::uint8_t>(event.sequence >> 8U),
    static_cast<std::uint8_t>(event.sequence >> 16U),
    static_cast<std::uint8_t>(event.sequence >> 24U),
    event.direction256,
    event.presentationDamage,
    event.metadata,
    static_cast<std::uint8_t>(event.weapon),
  };
  const auto found = std::search(wire.begin(), wire.end(), payload.begin(), payload.end());
  return found == wire.end()
    ? wire.size()
    : static_cast<std::size_t>(std::distance(wire.begin(), found));
}

std::size_t findAmmoValuePayload(
  const lg::WirePacket& wire,
  std::int32_t value
) {
  const std::uint32_t encodedValue = static_cast<std::uint32_t>(value);
  const std::array<std::uint8_t, 6U> payload = {
    0xffU,
    0xffU,
    static_cast<std::uint8_t>(encodedValue),
    static_cast<std::uint8_t>(encodedValue >> 8U),
    static_cast<std::uint8_t>(encodedValue >> 16U),
    static_cast<std::uint8_t>(encodedValue >> 24U),
  };
  const auto found = std::search(wire.begin(), wire.end(), payload.begin(), payload.end());
  return found == wire.end()
    ? wire.size()
    : static_cast<std::size_t>(std::distance(wire.begin(), found));
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::WirePacket wire;
    lg::PacketType type;
    lg::ConnectRequest request{12345};
    lg::ConnectRequest decodedRequest;
    failures += expect(lg::encodeConnectRequest(request, wire), "connect request should encode");
    failures += expect(lg::inspectPacketType(wire, type), "connect packet type should inspect");
    failures += expect(type == lg::PacketType::ConnectRequest, "connect request type should match");
    failures += expect(lg::decodeConnectRequest(wire, decodedRequest), "connect request should decode");
    failures += expect(decodedRequest.clientNonce == 12345, "connect nonce should round trip");

    lg::ConnectAccept accept{12345, 3, 1, 77};
    lg::ConnectAccept decodedAccept;
    failures += expect(lg::encodeConnectAccept(accept, wire), "connect accept should encode");
    failures += expect(lg::decodeConnectAccept(wire, decodedAccept), "connect accept should decode");
    failures += expect(decodedAccept.playerIndex == 1, "assigned player should round trip");
    failures += expect(decodedAccept.clientIndex == 3, "client slot should round trip");
    failures += expect(decodedAccept.serverTick == 77, "accept server tick should round trip");

    lg::PingPacket ping{88};
    lg::PingPacket decodedPing;
    failures += expect(
      lg::encodePingPacket(lg::PacketType::Ping, ping, wire),
      "ping should encode"
    );
    failures += expect(
      lg::decodePingPacket(wire, lg::PacketType::Ping, decodedPing),
      "ping should decode"
    );
    failures += expect(decodedPing.token == 88, "ping token should round trip");
  }

  {
    lg::ProjectileUpdatePacket source;
    source.serverTick = 9001U;
    source.mapRevision = 17U;
    source.projectileRevision = 23U;
    source.updateCount =
      static_cast<std::uint8_t>(lg::kMaxProjectileUpdatesPerPacket);
    for (std::size_t index = 0; index < source.updateCount; ++index) {
      lg::ProjectileUpdate& update = source.updates[index];
      update.slot = static_cast<std::uint16_t>(index);
      update.sequence = static_cast<std::uint32_t>(100U + index);
      update.kind = index % 3U == 0U
        ? lg::ProjectileUpdateKind::Spawn
        : index % 3U == 1U
          ? lg::ProjectileUpdateKind::Correct
          : lg::ProjectileUpdateKind::Remove;
      update.weapon = index % 3U == 0U
        ? lg::Weapon::RocketLauncher
        : index % 3U == 1U
          ? lg::Weapon::GrenadeLauncher
          : lg::Weapon::PlasmaGun;
      update.position = {
        static_cast<float>(index),
        -static_cast<float>(index),
        0.25F,
      };
      update.velocity = {1.0F, 2.0F, 3.0F};
      update.radius = update.weapon == lg::Weapon::GrenadeLauncher
        ? 0.15F : 0.0F;
      update.ageTicks = static_cast<std::uint32_t>(index * 3U);
      update.resting =
        update.weapon == lg::Weapon::GrenadeLauncher && index == 1U;
    }
    source.updates[source.updateCount - 1U].slot =
      static_cast<std::uint16_t>(lg::kMaxRocketProjectiles - 1U);

    lg::WirePacket wire;
    lg::ProjectileUpdatePacket decoded;
    lg::PacketType type;
    failures += expect(
      lg::encodeProjectileUpdatePacket(source, wire),
      "maximum projectile update packet should encode"
    );
    failures += expect(
      wire.size() == 1173U &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes,
      "maximum projectile update packet should use its fixed bounded wire size"
    );
    failures += expect(
      lg::inspectPacketType(wire, type) &&
        type == lg::PacketType::ProjectileUpdates,
      "projectile update packet type should inspect"
    );
    failures += expect(
      lg::decodeProjectileUpdatePacket(wire, decoded),
      "projectile update packet should decode"
    );
    failures += expect(
      decoded.serverTick == source.serverTick &&
        decoded.mapRevision == source.mapRevision &&
        decoded.projectileRevision == source.projectileRevision &&
        decoded.updateCount == source.updateCount &&
        decoded.updates[1].resting &&
        decoded.updates[1].weapon == lg::Weapon::GrenadeLauncher &&
        decoded.updates[2].kind == lg::ProjectileUpdateKind::Remove &&
        decoded.updates[source.updateCount - 1U].slot ==
          lg::kMaxRocketProjectiles - 1U,
      "projectile update fields should round trip"
    );

    lg::LoopbackTransport loopback;
    loopback.sendProjectileUpdates(source);
    failures += expect(
      loopback.receiveProjectileUpdates(decoded) &&
        decoded.updateCount == source.updateCount &&
        decoded.updates[2].kind == lg::ProjectileUpdateKind::Remove,
      "loopback transport should preserve projectile update packets"
    );

    lg::ProjectileUpdatePacket empty = source;
    empty.updateCount = 0U;
    failures += expect(
      lg::encodeProjectileUpdatePacket(empty, wire) &&
        lg::decodeProjectileUpdatePacket(wire, decoded) &&
        decoded.updateCount == 0U,
      "an empty projectile revision should round trip"
    );

    lg::ProjectileUpdatePacket invalid = source;
    invalid.updateCount = 1U;
    invalid.updates[0].slot =
      static_cast<std::uint16_t>(lg::kMaxRocketProjectiles);
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire) && wire.empty(),
      "out-of-range projectile slots should not encode"
    );
    invalid = source;
    invalid.updateCount = 1U;
    invalid.updates[0].sequence = 0U;
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire),
      "zero projectile sequences should not encode"
    );
    invalid = source;
    invalid.updateCount = 1U;
    invalid.updates[0].kind =
      static_cast<lg::ProjectileUpdateKind>(255);
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire),
      "invalid projectile update kinds should not encode"
    );
    invalid = source;
    invalid.updateCount = 1U;
    invalid.updates[0].weapon = lg::Weapon::Railgun;
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire),
      "non-projectile weapons should not encode as projectile updates"
    );
    invalid = source;
    invalid.updateCount = 1U;
    invalid.updates[0].position.x =
      std::numeric_limits<float>::infinity();
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire),
      "non-finite projectile state should not encode"
    );
    invalid = source;
    invalid.updateCount = 2U;
    invalid.updates[1].slot = invalid.updates[0].slot;
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire),
      "duplicate projectile slots should not encode"
    );
    invalid = source;
    invalid.updateCount = 1U;
    invalid.updates[0].resting = true;
    failures += expect(
      !lg::encodeProjectileUpdatePacket(invalid, wire),
      "only grenades may encode as resting projectiles"
    );

    lg::ProjectileUpdatePacket one = source;
    one.updateCount = 1U;
    failures += expect(
      lg::encodeProjectileUpdatePacket(one, wire),
      "single projectile update should encode for malformed fixtures"
    );
    lg::WirePacket malformed = wire;
    malformed[31] = 255U;
    failures += expect(
      !lg::decodeProjectileUpdatePacket(malformed, decoded),
      "decoder should reject invalid projectile update kinds"
    );
    malformed = wire;
    malformed[32] = static_cast<std::uint8_t>(lg::Weapon::Railgun);
    failures += expect(
      !lg::decodeProjectileUpdatePacket(malformed, decoded),
      "decoder should reject non-projectile weapon values"
    );
    malformed = wire;
    malformed[65] = 2U;
    failures += expect(
      !lg::decodeProjectileUpdatePacket(malformed, decoded),
      "decoder should reject non-canonical projectile booleans"
    );
    malformed = wire;
    malformed.pop_back();
    failures += expect(
      !lg::decodeProjectileUpdatePacket(malformed, decoded),
      "decoder should reject truncated projectile packets"
    );
    malformed.resize(lg::kMaxUdpApplicationDatagramBytes + 1U, 0U);
    failures += expect(
      !lg::decodeProjectileUpdatePacket(malformed, decoded),
      "decoder should reject projectile packets above the UDP cap"
    );
  }

  {
    lg::CommandPacket source;
    source.playerIndex = 1;
    source.clientIndex = 7;
    source.clientNonce = 12345;
    source.command.sequence = 42;
    source.acknowledgedConfigurationRevision = 77;
    source.acknowledgedPlayerNameRevision = 31;
    source.wantsScoreboardStats = true;
    source.command.clientTick = 99;
    source.command.viewYawRadians = 1.25F;
    source.command.viewPitchRadians = -0.25F;
    source.command.forwardMove = 1.0F;
    source.command.rightMove = -0.5F;
    source.command.upMove = 0.75F;
    source.command.attack = true;
    source.command.jump = true;
    source.command.dash = true;
    source.command.crouch = true;
    source.command.sneak = true;
    source.command.planarAim = false;
    source.command.weapon = lg::Weapon::PlasmaGun;
    source.requestReset = true;
    source.toggleReady = true;
    source.requestGameMode = true;
    source.requestedGameMode = lg::GameMode::ClanArena;
    source.requestTeam = true;
    source.requestedTeam = lg::Team::Blue;
    source.requestSpectator = true;
    source.weaponSwitchingMode = lg::WeaponSwitchingMode::Cpma;
    source.requestMovementTuning = true;
    source.movementTuning.flightEnabled = true;
    source.movementTuning.airControlEnabled = true;
    source.movementTuning.groundAcceleration = 120.0F;
    source.movementTuning.airAcceleration = 2.0F;
    source.movementTuning.groundFriction = 6.0F;
    source.movementTuning.stopSpeed = 2.5F;
    source.movementTuning.maxGroundSpeed = 14.0F;
    source.movementTuning.dashTargetSpeed = 13.0F;
    source.movementTuning.dashMaxSpeed = 14.5F;
    source.movementTuning.dashAcceleration = 240.0F;
    source.movementTuning.dashDuration = 0.12F;
    source.movementTuning.dashCooldown = 0.7F;
    source.movementTuning.dashGroundHopVelocity = 3.6F;
    source.movementTuning.dashAirHopVelocity = 2.1F;
    source.movementTuning.flightAcceleration = 48.0F;
    source.movementTuning.maxFlightSpeed = 16.0F;
    source.movementTuning.flightDamping = 1.5F;
    source.movementTuning.flightGravityCancel = 1.0F;
    source.playerSizeScaleXY = 1.75F;
    source.playerSizeScaleZ = 0.75F;
    source.lightningKnockback = 1500.0F;
    source.lightningFireHz = 40.0F;
    source.rocketKnockback = 625.0F;
    source.knockbackTimeMs = 125;
    source.weaponDamage.shotgunDamagePerPellet = 7;
    source.weaponDamage.machineGunDamage = 9;
    source.weaponDamage.lightningGunDamage = 111;
    source.weaponDamage.railgunDamage = 50;
    source.weaponDamage.rocketLauncherDamage = 125;
    source.weaponDamage.plasmaGunDamage = 20;
    source.weaponDamage.freezeGunDamage = 112;
    source.weaponAmmo.infiniteAmmo = false;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::LightningGun)] = 1000;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] = 1'000'000;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::RocketLauncher)] = 12;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::MachineGun)] = 101;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Shotgun)] = 13;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 14;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::PlasmaGun)] = 51;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::FreezeGun)] = 152;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Revolver)] = 15;
    source.vampirism = 0.1F;
    source.selfDamagePercent = 37;
    source.healthAmount = 175;
    source.botDodgeEnabled = true;
    source.botDodgeMinIntervalMs = 250;
    source.botDodgeMaxIntervalMs = 750;
    source.chatMessage = "åäöÅÄÖ";
    source.playerName = "yg";
    source.mapName = "testmap";
    source.botCommand = lg::BotCommandType::Weapon;
    source.botCommandValue = static_cast<std::int32_t>(lg::Weapon::Revolver);
    source.requestMcGuffinThrow = true;
    source.viewedServerTick = 88;
    source.actionEdges.jump = 4;
    source.actionEdges.dash = 5;
    source.actionEdges.reset = 6;
    source.actionEdges.ready = 7;
    source.actionEdges.mcguffinThrow = 8;
    source.actionEdges.mcguffinThrowYawRadians = 0.75F;
    source.actionEdges.mcguffinThrowPitchRadians = -0.2F;
    source.actionEdges.attack = 9;
    source.actionEdges.attackYawRadians = 1.25F;
    source.actionEdges.attackPitchRadians = -0.15F;
    source.actionEdges.attackViewedServerTick = 86;
    source.actionEdges.attackWeapon = lg::Weapon::Railgun;
    source.actionEdges.attackZoomed = true;
    source.command.zoomed = true;

    lg::WirePacket wire;
    lg::CommandPacket decoded;
    failures += expect(lg::encodeCommandPacket(source, wire), "command should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "command should respect packet limit");
    failures += expect(lg::decodeCommandPacket(wire, decoded), "command should decode");
    failures += expect(decoded.playerIndex == source.playerIndex, "command player should round trip");
    failures += expect(
      decoded.clientIndex == source.clientIndex,
      "command connection identity should round trip separately from its body"
    );
    failures += expect(decoded.clientNonce == 12345, "command nonce should round trip");
    failures += expect(decoded.command.sequence == 42, "command sequence should round trip");
    failures += expect(
      decoded.acknowledgedConfigurationRevision == 77,
      "configuration acknowledgement should round trip"
    );
    failures += expect(
      decoded.acknowledgedPlayerNameRevision == 31,
      "player-name acknowledgement should round trip"
    );
    failures += expect(decoded.wantsScoreboardStats,
                       "scoreboard statistics interest should round trip");
    failures += expect(decoded.command.clientTick == 99, "command tick should round trip");
    failures += expect(decoded.viewedServerTick == 88, "viewed server tick should round trip");
    failures += expect(
      nearlyEqual(decoded.command.viewPitchRadians, -0.25F),
      "command pitch should round trip"
    );
    failures += expect(
      decoded.command.attack &&
        decoded.command.jump &&
        decoded.command.dash &&
        decoded.command.crouch &&
        decoded.command.sneak &&
        decoded.command.zoomed,
      "command bits should round trip"
    );
    failures += expect(!decoded.command.planarAim, "command aim dimensionality should round trip");
    failures += expect(decoded.command.weapon == lg::Weapon::PlasmaGun, "weapon selection should round trip");
    failures += expect(decoded.chatMessage == "åäöÅÄÖ", "Swedish chat message should round trip");
    failures += expect(decoded.playerName == "yg", "player name should round trip");
    failures += expect(decoded.mapName == "testmap", "map name should round trip");
    failures += expect(
      decoded.botCommand == lg::BotCommandType::Weapon &&
        decoded.botCommandValue == static_cast<std::int32_t>(lg::Weapon::Revolver),
      "bot weapon command request should round trip"
    );
    failures += expect(decoded.requestReset, "reset bit should round trip");
    failures += expect(decoded.toggleReady, "ready bit should round trip");
    failures += expect(decoded.requestMcGuffinThrow,
      "McGuffin throw request should round trip");
    failures += expect(
      decoded.actionEdges.jump == 4U &&
        decoded.actionEdges.mcguffinThrow == 8U &&
        nearlyEqual(decoded.actionEdges.mcguffinThrowYawRadians, 0.75F) &&
        decoded.actionEdges.attack == 9U &&
        decoded.actionEdges.attackWeapon == lg::Weapon::Railgun &&
        decoded.actionEdges.attackZoomed,
      "cumulative action edges should round trip"
    );
    failures += expect(
      decoded.requestGameMode &&
        decoded.requestedGameMode == lg::GameMode::ClanArena,
      "explicit gamemode request should round trip"
    );
    failures += expect(
      decoded.requestTeam && decoded.requestedTeam == lg::Team::Blue,
      "explicit team request should round trip"
    );
    failures += expect(decoded.requestSpectator,
      "spectator role request should round trip");
    failures += expect(
      decoded.weaponSwitchingMode == lg::WeaponSwitchingMode::Cpma,
      "weapon switching mode request should round trip"
    );
    failures += expect(
      decoded.requestMovementTuning &&
        decoded.movementTuning.flightEnabled &&
        decoded.movementTuning.airControlEnabled &&
        nearlyEqual(decoded.movementTuning.groundAcceleration, 120.0F) &&
        nearlyEqual(decoded.movementTuning.airAcceleration, 2.0F) &&
        nearlyEqual(decoded.movementTuning.groundFriction, 6.0F) &&
        nearlyEqual(decoded.movementTuning.stopSpeed, 2.5F) &&
        nearlyEqual(decoded.movementTuning.maxGroundSpeed, 14.0F) &&
        nearlyEqual(decoded.movementTuning.dashTargetSpeed, 13.0F) &&
        nearlyEqual(decoded.movementTuning.dashMaxSpeed, 14.5F) &&
        nearlyEqual(decoded.movementTuning.dashAcceleration, 240.0F) &&
        nearlyEqual(decoded.movementTuning.dashDuration, 0.12F) &&
        nearlyEqual(decoded.movementTuning.dashCooldown, 0.7F) &&
        nearlyEqual(decoded.movementTuning.dashGroundHopVelocity, 3.6F) &&
        nearlyEqual(decoded.movementTuning.dashAirHopVelocity, 2.1F) &&
        nearlyEqual(decoded.movementTuning.flightAcceleration, 48.0F) &&
        nearlyEqual(decoded.movementTuning.maxFlightSpeed, 16.0F) &&
        nearlyEqual(decoded.movementTuning.flightDamping, 1.5F) &&
        nearlyEqual(decoded.movementTuning.flightGravityCancel, 1.0F) &&
        nearlyEqual(decoded.playerSizeScaleXY, 1.75F) &&
        nearlyEqual(decoded.playerSizeScaleZ, 0.75F) &&
        nearlyEqual(decoded.lightningKnockback, 1500.0F) &&
        nearlyEqual(decoded.lightningFireHz, 40.0F) &&
        nearlyEqual(decoded.rocketKnockback, 625.0F) &&
        decoded.knockbackTimeMs == 125 &&
        decoded.weaponDamage.shotgunDamagePerPellet == 7 &&
        decoded.weaponDamage.machineGunDamage == 9 &&
        decoded.weaponDamage.lightningGunDamage == 111 &&
        decoded.weaponDamage.railgunDamage == 50 &&
        decoded.weaponDamage.rocketLauncherDamage == 125 &&
        decoded.weaponDamage.plasmaGunDamage == 20 &&
        decoded.weaponDamage.freezeGunDamage == 112 &&
        !decoded.weaponAmmo.infiniteAmmo &&
        decoded.weaponAmmo.spawnAmmo == source.weaponAmmo.spawnAmmo &&
        nearlyEqual(decoded.vampirism, 0.1F) &&
        decoded.selfDamagePercent == 37 &&
        decoded.healthAmount == 175 &&
        decoded.botDodgeEnabled &&
        decoded.botDodgeMinIntervalMs == 250 &&
        decoded.botDodgeMaxIntervalMs == 750,
      "movement tuning request should round trip"
    );

    lg::WirePacket wrongVersion = wire;
    wrongVersion[4] = 1;
    failures += expect(
      !lg::decodeCommandPacket(wrongVersion, decoded),
      "wrong protocol version should be rejected"
    );

    lg::WirePacket truncated = wire;
    truncated.pop_back();
    failures += expect(!lg::decodeCommandPacket(truncated, decoded), "truncated command should be rejected");

    lg::WirePacket wrongType = wire;
    wrongType[6] = static_cast<std::uint8_t>(lg::PacketType::Snapshot);
    failures += expect(!lg::decodeCommandPacket(wrongType, decoded), "wrong packet type should be rejected");

    lg::CommandPacket invalidMode = source;
    invalidMode.requestedGameMode = static_cast<lg::GameMode>(255);
    failures += expect(
      !lg::encodeCommandPacket(invalidMode, wire),
      "invalid requested gamemode should not encode"
    );

    lg::CommandPacket invalidTeam = source;
    invalidTeam.requestedTeam = static_cast<lg::Team>(255);
    failures += expect(
      !lg::encodeCommandPacket(invalidTeam, wire),
      "invalid requested team should not encode"
    );

    lg::CommandPacket invalidBotWeapon = source;
    invalidBotWeapon.botCommandValue =
      static_cast<std::int32_t>(lg::kLastWeapon) + 1;
    failures += expect(
      !lg::encodeCommandPacket(invalidBotWeapon, wire),
      "invalid bot weapon should not encode"
    );

    lg::CommandPacket invalidMovement = source;
    invalidMovement.command.forwardMove = 1.1F;
    failures += expect(lg::encodeCommandPacket(invalidMovement, wire), "finite command should encode");
    failures += expect(
      !lg::decodeCommandPacket(wire, decoded),
      "out-of-range movement input should be rejected"
    );

    lg::CommandBundle bundle;
    bundle.datagramSequence = 91;
    bundle.actionEdges = source.actionEdges;
    bundle.commandCount = 3;
    bundle.commands[0] = source;
    bundle.commands[1] = source;
    bundle.commands[1].command.sequence = 43;
    bundle.commands[2] = source;
    bundle.commands[2].command.sequence = 44;
    lg::CommandBundle decodedBundle;
    failures += expect(lg::encodeCommandBundle(bundle, wire), "command bundle should encode");
    failures += expect(lg::decodeCommandBundle(wire, decodedBundle), "command bundle should decode");
    failures += expect(decodedBundle.commandCount == 3, "bundle count should round trip");
    failures += expect(
      decodedBundle.datagramSequence == 91,
      "command datagram sequence should round trip"
    );
    failures += expect(
      decodedBundle.commands[2].command.sequence == 44,
      "bundle command order should round trip"
    );
    failures += expect(
      decodedBundle.commands[0].requestedGameMode == lg::GameMode::ClanArena &&
        decodedBundle.commands[2].requestedTeam == lg::Team::Blue,
      "redundant command bundles should preserve explicit mode and team requests"
    );
    std::cout << "command control bundle bytes=" << wire.size() << '\n';
    failures += expect(
      wire.size() < 1200,
      "ordinary redundant command bundles should stay below the datagram budget"
    );

    lg::CommandBundle gameplayBundle;
    gameplayBundle.datagramSequence = 92;
    gameplayBundle.actionEdges = source.actionEdges;
    gameplayBundle.commandCount = lg::kMaxBundledCommands;
    for (std::size_t index = 0; index < gameplayBundle.commandCount; ++index) {
      lg::CommandPacket& command = gameplayBundle.commands[index];
      command.clientIndex = source.clientIndex;
      command.playerIndex = source.playerIndex;
      command.clientNonce = source.clientNonce;
      command.command = source.command;
      command.command.sequence = static_cast<std::uint32_t>(100U + index);
      command.viewedServerTick = static_cast<std::uint32_t>(80U + index);
      command.acknowledgedConfigurationRevision = 77;
      command.acknowledgedPlayerNameRevision = 31;
    }
    failures += expect(
      lg::encodeCommandBundle(gameplayBundle, wire) &&
        lg::decodeCommandBundle(wire, decodedBundle),
      "maximum gameplay command history should round trip"
    );
    failures += expect(
      wire.size() <= lg::kMaxCommandDatagramBytes,
      "maximum gameplay command history should stay below the datagram budget"
    );
    std::cout << "command gameplay bundle bytes=" << wire.size() << '\n';

    lg::CommandBundle spectatorBundle = gameplayBundle;
    spectatorBundle.datagramSequence = 93;
    spectatorBundle.commandCount = 2;
    for (std::size_t index = 0; index < spectatorBundle.commandCount; ++index) {
      spectatorBundle.commands[index].clientIndex = 9;
      spectatorBundle.commands[index].playerIndex = lg::kNoAssignedPlayer;
    }
    failures += expect(
      lg::encodeCommandBundle(spectatorBundle, wire) &&
        lg::decodeCommandBundle(wire, decodedBundle) &&
        decodedBundle.commands[1].clientIndex == 9 &&
        decodedBundle.commands[1].playerIndex == lg::kNoAssignedPlayer,
      "spectator client slots should retain bundled command redundancy"
    );

    spectatorBundle.commands[1].clientIndex = 8;
    failures += expect(
      !lg::encodeCommandBundle(spectatorBundle, wire),
      "a command bundle should have one authenticated connection identity"
    );
    bundle.datagramSequence = 0;
    failures += expect(
      !lg::encodeCommandBundle(bundle, wire),
      "command bundles should reject the reserved zero datagram sequence"
    );
    bundle.datagramSequence = 91;
    for (lg::CommandPacket& command : bundle.commands) {
      command.chatMessage.assign(lg::kMaxChatMessageBytes, 'c');
      command.playerName.assign(lg::kMaxPlayerNameBytes, 'n');
      command.mapName.assign(lg::kMaxMapNameBytes, 'm');
    }
    failures += expect(
      !lg::encodeCommandBundle(bundle, wire),
      "an oversized redundant command bundle should fail encoding"
    );
  }

  {
    lg::CommandPacket source;
    source.playerIndex = 0;
    source.chatMessage.assign(180U, 'l');

    lg::WirePacket wire;
    lg::CommandPacket decoded;
    failures += expect(
      lg::encodeCommandPacket(source, wire),
      "longer chat command should encode"
    );
    failures += expect(
      lg::decodeCommandPacket(wire, decoded),
      "longer chat command should decode"
    );
    failures += expect(
      decoded.chatMessage == source.chatMessage &&
        decoded.chatMessage.size() > 64U,
      "chat command should allow messages beyond the old 64-byte cap"
    );
  }

  {
    const std::array states = {
      lg::McGuffinState::NeutralSpawn,
      lg::McGuffinState::Carried,
      lg::McGuffinState::Dropped,
      lg::McGuffinState::InstalledRed,
      lg::McGuffinState::InstalledBlue,
    };
    for (lg::McGuffinState state : states) {
      lg::ServerSnapshot source;
      source.map = testMapDescriptor();
      source.gameMode = lg::GameMode::McGuffin;
      source.mcguffin.state = state;
      source.mcguffin.position = {1.0F, 2.0F, 3.0F};
      source.mcguffin.velocity = {4.0F, 5.0F, 6.0F};
      source.mcguffinConfig.throwSpeed = 13.0F;
      source.mcguffinConfig.throwUpSpeed = 5.0F;
      source.mcguffin.stateTicks = 41;
      source.mcguffin.eventSequence = 9;
      source.mcguffin.lastEvent = lg::McGuffinEventType::Pickup;
      source.mcguffinScores = {42, 37};
      source.mcguffinRoundsWon = {1, 0};
      if (state == lg::McGuffinState::Carried) {
        source.mcguffin.carrierIndex = 1;
        source.mcguffin.carrierTeam = lg::Team::Blue;
      }
      if (state == lg::McGuffinState::InstalledRed) {
        source.mcguffin.associatedTeam = lg::Team::Red;
      } else if (state == lg::McGuffinState::InstalledBlue) {
        source.mcguffin.associatedTeam = lg::Team::Blue;
      }
      lg::WirePacket wire;
      lg::ServerSnapshot decoded;
      failures += expect(lg::encodeServerSnapshot(source, wire),
        "every McGuffin objective state should encode");
      failures += expect(lg::decodeServerSnapshot(wire, decoded),
        "every McGuffin objective state should decode");
      failures += expect(
        decoded.gameMode == lg::GameMode::McGuffin &&
          decoded.mcguffin.state == state &&
          decoded.mcguffin.position.z == 3.0F &&
          decoded.mcguffin.velocity.z == 6.0F &&
          decoded.mcguffinConfig.throwSpeed == 13.0F &&
          decoded.mcguffinConfig.throwUpSpeed == 5.0F &&
          decoded.mcguffinScores == source.mcguffinScores,
        "McGuffin snapshot should round trip authoritatively"
      );
    }

    lg::ServerSnapshot invalid;
    invalid.map = testMapDescriptor();
    invalid.gameMode = static_cast<lg::GameMode>(255);
    lg::WirePacket wire;
    failures += expect(!lg::encodeServerSnapshot(invalid, wire),
      "invalid game mode enum should be rejected");
    invalid.gameMode = lg::GameMode::McGuffin;
    invalid.mcguffin.state = static_cast<lg::McGuffinState>(255);
    failures += expect(!lg::encodeServerSnapshot(invalid, wire),
      "invalid McGuffin state enum should be rejected");
  }

  {
    lg::ServerSnapshot source;
    source.serverTick = 1234;
    source.acknowledgedCommandDatagramSequence = 88;
    source.commandDatagramAckBits = 0xA5A55A5AU;
    source.mapRevision = 77;
    source.damageFeedbackRevision = 81;
    source.projectileRevision = 79;
    source.map = testMapDescriptor();
    source.acknowledgedCommand = {12, 34};
    source.hasAcknowledgedCommand = {true, false};
    source.players[0].position = {1.0F, 2.0F, 3.0F};
    source.players[0].velocity = {-1.0F, 0.5F, 4.0F};
    source.players[0].viewYawRadians = 0.75F;
    source.players[0].viewPitchRadians = -0.1F;
    source.players[0].health = 81;
    source.players[0].freezeLevel = 37.5F;
    source.players[0].movementMode = lg::MovementMode::Flying;
    source.players[0].onGround = false;
    source.players[0].jumpHeld = true;
    source.players[0].dashHeld = true;
    source.players[0].dashCooldownTicksRemaining = 77;
    source.players[0].dashActiveTicksRemaining = 4;
    source.players[0].dashDirection = {0.0F, -1.0F, 0.0F};
    source.players[0].crouched = true;
    source.players[0].sneaking = true;
    source.players[0].knockbackTicksRemaining = 9;
    source.players[1].health = 0;
    source.selectedWeapons[0] = lg::Weapon::Revolver;
    source.selectedWeapons[1] = lg::Weapon::Railgun;
    source.sniperChargePercent[1] = 73;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::LightningGun)] = 1000;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::Railgun)] = 1'000'000;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::RocketLauncher)] = 8;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::MachineGun)] = 99;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::Shotgun)] = 7;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 6;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::PlasmaGun)] = 49;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::FreezeGun)] = 148;
    source.playerAmmo[0][lg::weaponIndex(lg::Weapon::Revolver)] = 13;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::LightningGun)] = 120;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::Railgun)] = 5;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::RocketLauncher)] = 4;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::MachineGun)] = 88;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::Shotgun)] = 3;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 2;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::PlasmaGun)] = 48;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::FreezeGun)] = 147;
    source.playerAmmo[1][lg::weaponIndex(lg::Weapon::Revolver)] = 12;
    source.lightningGuns[0].active = true;
    source.lightningGuns[0].hit = true;
    source.lightningGuns[0].headshot = true;
    source.lightningGuns[0].targetPlayerIndex = 1;
    source.lightningGuns[0].damageApplied = 2;
    source.lightningGuns[0].start = {1.0F, 2.0F, 3.0F};
    source.lightningGuns[0].end = {5.0F, 6.0F, 7.0F};
    source.lightningGuns[0].knockbackImpulse = {0.1F, 0.2F, 0.3F};
    source.lightningGuns[0].freezeApplied = 2.5F;
    source.lightningGuns[0].requestedRewindTicks = 20;
    source.lightningGuns[0].appliedRewindTicks = 18;
    source.lightningGuns[0].rewindClamped = true;
    source.lightningGuns[0].hasRewindDebug = true;
    source.lightningGuns[0].rewindTargetTick = 1216;
    source.lightningGuns[0].currentTargetPosition = {8.0F, 3.0F, 4.0F};
    source.lightningGuns[0].rewoundTargetPosition = {8.0F, 0.0F, 2.0F};
    source.lightningGuns[0].currentTargetBounds = {0.5F, 1.1F};
    source.lightningGuns[0].rewoundTargetBounds = {0.4F, 0.9F};
    source.weaponFires[0].fired = true;
    source.weaponFires[0].hit = true;
    source.weaponFires[0].headshot = true;
    source.weaponFires[0].weapon = lg::Weapon::Revolver;
    source.weaponFires[0].damageApplied = 80;
    source.weaponFires[0].start = {1.0F, 1.5F, 2.0F};
    source.weaponFires[0].end = {9.0F, 1.5F, 2.0F};
    source.weaponFires[0].knockbackImpulse = {2.0F, 0.0F, 0.0F};
    source.weaponFires[1].fired = true;
    source.weaponFires[1].hit = true;
    source.weaponFires[1].headshot = true;
    source.weaponFires[1].weapon = lg::Weapon::Shotgun;
    source.weaponFires[1].damageApplied = 45;
    source.weaponFires[1].start = {1.0F, -1.5F, 2.0F};
    source.weaponFires[1].end = {7.0F, -1.5F, 2.0F};
    source.weaponFires[1].knockbackImpulse = {1.0F, 0.0F, 0.0F};
    source.weaponFires[1].pelletCount = lg::kShotgunPelletCount;
    source.weaponFires[1].pelletHitCount = 9;
    source.weaponFires[1].pelletHeadshotCount = 3;
    source.weaponFires[1].visualSeed = 12;
    source.weaponFires[2].fired = true;
    source.weaponFires[2].hit = true;
    source.weaponFires[2].weapon = lg::Weapon::MachineGun;
    source.weaponFires[2].damageApplied = 5;
    source.weaponFires[2].start = {2.0F, 1.5F, 2.0F};
    source.weaponFires[2].visualSeed = 77;
    source.weaponFires[2].end = {8.0F, 1.5F, 2.0F};
    source.weaponFires[2].knockbackImpulse = {0.11F, 0.0F, 0.0F};
    source.rocketExplosions[0].active = true;
    source.rocketExplosions[0].weapon = lg::Weapon::GrenadeLauncher;
    source.rocketExplosions[0].position = {3.0F, 4.0F, 0.0F};
    source.rocketExplosions[0].radius = 3.0F;
    source.rocketExplosions[0].ownerDamageApplied = 12;
    source.rocketExplosions[0].opponentDamageApplied = 80;
    source.rocketExplosions[0].sequence = 42;
    source.rocketExplosions[0].projectileSequence = 73;
    source.fragEvents[0].active = true;
    source.fragEvents[0].sequence = 55;
    source.fragEvents[0].targetPlayerIndex = 1;
    source.fragEvents[0].weapon = lg::Weapon::Railgun;
    source.localHitFeedbackEvents[0][0].active = true;
    source.localHitFeedbackEvents[0][0].sequence = 17;
    source.localHitFeedbackEvents[0][0].targetPlayerIndex = 1;
    source.localHitFeedbackEvents[0][0].damageApplied = 45;
    source.localHitFeedbackEvents[0][0].headshot = true;
    source.localHitFeedbackEvents[0][0].weapon = lg::Weapon::Shotgun;
    source.localHitFeedbackEvents[0][1].active = true;
    source.localHitFeedbackEvents[0][1].sequence = 18;
    source.localHitFeedbackEvents[0][1].targetPlayerIndex = 2;
    source.localHitFeedbackEvents[0][1].damageApplied = 80;
    source.localHitFeedbackEvents[0][1].weapon = lg::Weapon::RocketLauncher;
    lg::DamageTakenEventRing& damageRing = source.damageTakenEvents[1];
    lg::DamageTakenEvent& damageEvent = damageRing.events[3];
    damageEvent.sequence = 73U;
    damageEvent.direction256 = 192U;
    damageEvent.presentationDamage = 255U;
    damageEvent.metadata = lg::kDamageTakenDirectionValid |
      lg::kDamageTakenAttackerValid;
    damageEvent.weapon = lg::Weapon::RocketLauncher;
    failures += expect(
      lg::setDamageTakenEventActive(damageRing, 3U) &&
        !lg::setDamageTakenEventActive(damageRing, lg::kDamageTakenEventWindow),
      "damage-taken ring helpers should enforce slot bounds"
    );
    source.footstepAudioEvents[1].active = true;
    source.footstepAudioEvents[1].jumping = true;
    source.footstepAudioEvents[1].landing = true;
    source.footstepAudioEvents[1].sequence = 42;
    source.footstepAudioEvents[1].position = {2.5F, -1.0F, 1.5F};
    source.grenadeBounceAudioEvents[0].active = true;
    source.grenadeBounceAudioEvents[0].sequence = 9;
    source.grenadeBounceAudioEvents[0].position = {4.5F, -2.0F, 0.75F};
    source.icePools[0].active = true;
    source.icePools[0].center = {1.0F, 2.0F, 0.0F};
    source.icePools[0].normal = {0.0F, 0.0F, 1.0F};
    source.icePools[0].radius = 1.25F;
    source.icePools[0].lifetimeSeconds = 2.5F;
    source.healthPickupAvailable[0] = true;
    source.healthPickupAvailable[3] = true;
    source.respawnTicksRemaining = {0, 88};
    source.scores = {7, 4};
    source.gameMode = lg::GameMode::ClanArena;
    source.teams = {
      lg::Team::Red,
      lg::Team::Blue,
      lg::Team::None,
      lg::Team::None,
      lg::Team::None,
      lg::Team::None,
    };
    source.teamScores = {8, 6};
    source.roundWinningTeam = lg::Team::Red;
    source.matchWinningTeam = lg::Team::None;
    source.connectedPlayers = {true, true};
    source.botPlayers = {false, false, true, false};
    source.participatingPlayers = {true, true, true};
    source.readyPlayers = {true, false};
    source.roundCombatStats[0].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {80, 250, 125};
    source.roundCombatStats[1].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {24, 200, 40};
    source.matchCombatStats[0].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {180, 500, 275};
    source.matchCombatStats[1].weapons[lg::weaponIndex(lg::Weapon::LightningGun)] =
      {74, 450, 90};
    source.playerNames = {"yg", "opponent"};
    source.playerNameRevision = 17U;
    source.matchPhase = lg::MatchPhase::Countdown;
    source.matchRules.roundLimit = 10;
    source.matchRules.timeLimitMinutes = 5;
    source.matchRules.playerLimit = 2;
    source.matchRules.countdownTicks = 625;
    source.matchRules.roundEndTicks = 125;
    source.matchRules.matchEndTicks = 625;
    source.matchRules.deathRespawnTicks = 250;
    source.matchRules.showOpponentHealth = true;
    source.movementTuning.flightEnabled = true;
    source.movementTuning.airControlEnabled = true;
    source.movementTuning.groundAcceleration = 120.0F;
    source.movementTuning.airAcceleration = 2.0F;
    source.movementTuning.groundFriction = 6.0F;
    source.movementTuning.stopSpeed = 2.5F;
    source.movementTuning.maxGroundSpeed = 14.0F;
    source.movementTuning.dashTargetSpeed = 13.0F;
    source.movementTuning.dashMaxSpeed = 14.5F;
    source.movementTuning.dashAcceleration = 240.0F;
    source.movementTuning.dashDuration = 0.12F;
    source.movementTuning.dashCooldown = 0.7F;
    source.movementTuning.dashGroundHopVelocity = 3.6F;
    source.movementTuning.dashAirHopVelocity = 2.1F;
    source.movementTuning.flightAcceleration = 48.0F;
    source.movementTuning.maxFlightSpeed = 16.0F;
    source.movementTuning.flightDamping = 1.5F;
    source.movementTuning.flightGravityCancel = 1.0F;
    source.playerSizeScaleXY = 1.75F;
    source.playerSizeScaleZ = 0.75F;
    source.lightningKnockback = 1500.0F;
    source.lightningFireHz = 25.0F;
    source.rocketKnockback = 625.0F;
    source.knockbackTimeMs = 125;
    source.weaponDamage.shotgunDamagePerPellet = 11;
    source.weaponDamage.machineGunDamage = 13;
    source.weaponDamage.lightningGunDamage = 90;
    source.weaponDamage.railgunDamage = 50;
    source.weaponDamage.rocketLauncherDamage = 140;
    source.weaponDamage.plasmaGunDamage = 25;
    source.weaponDamage.freezeGunDamage = 95;
    source.icePoolTuning.maxRadius = 3.0F;
    source.icePoolTuning.growthPerSecond = 11.0F;
    source.icePoolTuning.lifetimeSeconds = 4.0F;
    source.icePoolTuning.friction = 0.75F;
    source.icePoolTuning.slopeGravityScale = 1.25F;
    source.icePoolTuning.controlScale = 0.25F;
    source.icePoolTuning.mergeDistance = 1.4F;
    source.projectilePresentation.rocketLifetimeTicks = 625U;
    source.projectilePresentation.grenadeFuseTicks = 250U;
    source.projectilePresentation.plasmaLifetimeTicks = 150U;
    source.projectilePresentation.grenadeGravity = 12.0F;
    source.projectilePresentation.grenadeBounceDamping = 0.7F;
    source.projectilePresentation.grenadeRestSpeed = 0.8F;
    source.weaponAmmo.infiniteAmmo = false;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::LightningGun)] = 150;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] = 10;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::RocketLauncher)] = 11;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::MachineGun)] = 100;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Shotgun)] = 12;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::GrenadeLauncher)] = 13;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::PlasmaGun)] = 50;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::FreezeGun)] = 149;
    source.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Revolver)] = 14;
    source.vampirism = 2.0F;
    source.selfDamagePercent = 25;
    source.healthAmount = 150;
    source.botDodgeEnabled = true;
    source.botDodgeMinIntervalMs = 300;
    source.botDodgeMaxIntervalMs = 700;
    source.botWeapon = lg::Weapon::RocketLauncher;
    source.weaponSwitchingMode = lg::WeaponSwitchingMode::Ql;
    source.phaseTicksRemaining = 321;
    source.liveTicksElapsed = 900;
    source.overtime = true;
    source.roundWinner = 0;
    source.matchWinner = 255;
    source.playersColliding = true;
    // High-frequency gameplay snapshots never embed scoreboard aggregates;
    // those use the independently bounded CombatStats packet below.
    source.hasCombatStats = false;

    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(lg::encodeServerSnapshot(source, wire), "snapshot should encode");
    failures += expect(wire.size() <= lg::kMaxPacketBytes, "snapshot should respect packet limit");
    const std::size_t activeCombatSnapshotBytes = wire.size();
    const bool activeCombatCompressed = wire[7] == 1U;
    failures += expect(
      activeCombatSnapshotBytes < 2800,
      "representative active-combat snapshot should remain below 2800 bytes"
    );
    failures += expect(lg::decodeServerSnapshot(wire, decoded), "snapshot should decode");
    failures += expect(decoded.serverTick == 1234, "snapshot tick should round trip");
    failures += expect(decoded.mapRevision == 77, "snapshot map revision should round trip");
    failures += expect(
      decoded.damageFeedbackRevision == 81,
      "damage-feedback timeline revision should round trip"
    );
    failures += expect(
      decoded.projectileRevision == 79,
      "snapshot projectile generation should round trip"
    );
    failures += expect(
      decoded.map.mapName == source.map.mapName &&
        decoded.map.contentHash == source.map.contentHash,
      "snapshot map descriptor should round trip without arena geometry"
    );
    failures += expect(decoded.acknowledgedCommand[0] == 12, "snapshot ack should round trip");
    failures += expect(
      decoded.acknowledgedCommandDatagramSequence == 88 &&
        decoded.commandDatagramAckBits == 0xA5A55A5AU,
      "command datagram acknowledgement window should round trip"
    );
    failures += expect(
        decoded.players[0].movementMode == lg::MovementMode::Flying &&
        decoded.players[0].jumpHeld &&
        decoded.players[0].dashHeld &&
        decoded.players[0].crouched &&
        decoded.players[0].sneaking &&
        decoded.players[0].knockbackTicksRemaining == 9 &&
        decoded.players[0].dashCooldownTicksRemaining == 77 &&
        decoded.players[0].dashActiveTicksRemaining == 4 &&
        nearlyEqual(decoded.players[0].dashDirection.y, -1.0F) &&
        nearlyEqual(decoded.players[0].freezeLevel, 37.5F),
      "movement mode, movement latches, dash state, knockback timer, and freeze level should round trip"
    );
    failures += expect(nearlyEqual(decoded.players[0].position.z, 3.0F), "3D position should round trip");
    failures += expect(nearlyEqual(decoded.players[0].velocity.z, 4.0F), "3D velocity should round trip");
    failures += expect(decoded.players[1].health == 0, "death state should round trip");
    failures += expect(
      decoded.selectedWeapons[0] == lg::Weapon::Revolver &&
        decoded.selectedWeapons[1] == lg::Weapon::Railgun,
      "selected weapons should round trip"
    );
    failures += expect(
      decoded.sniperChargePercent[1] == 73,
      "server-owned sniper charge should round trip"
    );
    failures += expect(
      decoded.playerAmmo == source.playerAmmo,
      "per-player ammo should round trip"
    );
    failures += expect(
      decoded.playerNameRevision == 17U && decoded.hasPlayerNames &&
        decoded.playerNames[0] == "yg",
      "versioned player names should round trip"
    );
    failures += expect(
      lg::damageTakenEventActive(decoded.damageTakenEvents[1], 3U) &&
        decoded.damageTakenEvents[1].events[3].sequence == 73U &&
        decoded.damageTakenEvents[1].events[3].direction256 == 192U &&
        decoded.damageTakenEvents[1].events[3].presentationDamage == 255U &&
        lg::damageTakenHasAttacker(decoded.damageTakenEvents[1].events[3]) &&
        !lg::damageTakenIsSelfDamage(decoded.damageTakenEvents[1].events[3]) &&
        decoded.damageTakenEvents[1].events[3].weapon ==
          lg::Weapon::RocketLauncher,
      "victim damage event should round trip through its outer mask"
    );
    lg::WirePacket malformedWire;
    lg::WirePacket expandedWire;
    failures += expect(
      lg::encodeServerSnapshot(source, malformedWire) &&
        expandSnapshotForMutation(malformedWire, expandedWire),
      "snapshot should expand for malformed damage-event tests"
    );
    const std::size_t damageEventOffset = findDamageEventPayload(
      expandedWire,
      source.damageTakenEvents[1].events[3]
    );
    failures += expect(
      damageEventOffset != expandedWire.size(),
      "damage event payload should be located for malformed decode tests"
    );
    if (damageEventOffset != expandedWire.size()) {
      failures += expect(
        lg::decodeServerSnapshot(wire, decoded),
        "the unmodified compressed snapshot should decode before mutation"
      );

      malformedWire = expandedWire;
      malformedWire[damageEventOffset + 6U] |= std::uint8_t{1} << 3U;
      failures += expect(
        compactSnapshotForMutation(malformedWire, damageEventOffset + 6U) &&
          !lg::decodeServerSnapshot(malformedWire, decoded),
        "decoder should reject reserved damage-event metadata bits"
      );

      malformedWire = expandedWire;
      malformedWire[damageEventOffset + 6U] = static_cast<std::uint8_t>(
        lg::kDamageTakenDirectionValid |
        lg::kDamageTakenAttackerValid |
        (std::uint8_t{1} << 4U)
      );
      failures += expect(
        compactSnapshotForMutation(malformedWire, damageEventOffset + 6U) &&
          !lg::decodeServerSnapshot(malformedWire, decoded),
        "decoder should reject self-damage without the self flag"
      );
    }
    const std::size_t ammoExtendedOffset =
      findAmmoValuePayload(expandedWire, 1'000'000);
    failures += expect(
      ammoExtendedOffset != expandedWire.size(),
      "extended ammo payload should be located for canonical-form tests"
    );
    if (ammoExtendedOffset != expandedWire.size()) {
      malformedWire = expandedWire;
      malformedWire[ammoExtendedOffset + 2U] = 0xfeU;
      malformedWire[ammoExtendedOffset + 3U] = 0xffU;
      malformedWire[ammoExtendedOffset + 4U] = 0U;
      malformedWire[ammoExtendedOffset + 5U] = 0U;
      failures += expect(
        !lg::decodeServerSnapshot(malformedWire, decoded),
        "decoder should reject an extended ammo value below the marker"
      );
    }
    lg::ServerSnapshot malformedDamage = source;
    malformedDamage.damageTakenEvents[1].events[3].metadata |= 1U << 3U;
    failures += expect(
      !lg::encodeServerSnapshot(malformedDamage, wire),
      "damage event metadata should reject its reserved bit"
    );
    malformedDamage = source;
    malformedDamage.damageTakenEvents[1].events[3].metadata = 1U << 4U;
    failures += expect(
      !lg::encodeServerSnapshot(malformedDamage, wire),
      "damage event metadata should reject a packed attacker without its flag"
    );
    malformedDamage = source;
    malformedDamage.damageTakenEvents[1].events[3].metadata = 0U;
    failures += expect(
      !lg::encodeServerSnapshot(malformedDamage, wire),
      "damage event without direction validity should require a zero bearing"
    );
    malformedDamage = source;
    malformedDamage.damageTakenEvents[1].events[3].metadata =
      lg::kDamageTakenDirectionValid |
      lg::kDamageTakenSelfDamage |
      lg::kDamageTakenAttackerValid;
    failures += expect(
      !lg::encodeServerSnapshot(malformedDamage, wire),
      "self-damage metadata should name the victim as its attacker"
    );
    malformedDamage = source;
    malformedDamage.damageTakenEvents[1].events[3].metadata =
      lg::kDamageTakenDirectionValid |
      lg::kDamageTakenAttackerValid |
      (std::uint8_t{1} << 4U);
    failures += expect(
      !lg::encodeServerSnapshot(malformedDamage, wire),
      "a victim-named attacker should set self-damage metadata"
    );
    malformedDamage = source;
    malformedDamage.damageTakenEvents[1].events[3].sequence = 0U;
    failures += expect(
      !lg::encodeServerSnapshot(malformedDamage, wire),
      "damage event metadata should reject zero sequences"
    );
    lg::ServerSnapshot leanNames = source;
    leanNames.playerNameRevision = 18U;
    leanNames.hasPlayerNames = false;
    failures += expect(
      lg::encodeServerSnapshot(leanNames, wire) &&
        lg::decodeServerSnapshot(wire, decoded) &&
        decoded.playerNameRevision == 18U && !decoded.hasPlayerNames,
      "lean snapshots should carry a name revision without a name payload"
    );
    failures += expect(
      lg::encodeServerSnapshot(source, wire) &&
        lg::decodeServerSnapshot(wire, decoded),
      "full snapshot should restore the round-trip fixture after lean-name test"
    );
    failures += expect(
      decoded.lightningGuns[0].hit &&
        decoded.lightningGuns[0].headshot &&
        decoded.lightningGuns[0].targetPlayerIndex == 1,
      "beam hit target should round trip"
    );
    failures += expect(
      decoded.lightningGuns[0].requestedRewindTicks == 20 &&
        decoded.lightningGuns[0].appliedRewindTicks == 18 &&
        decoded.lightningGuns[0].rewindClamped &&
        decoded.lightningGuns[0].hasRewindDebug &&
        decoded.lightningGuns[0].rewindTargetTick == 1216 &&
        nearlyEqual(decoded.lightningGuns[0].currentTargetPosition.z, 4.0F) &&
        nearlyEqual(decoded.lightningGuns[0].rewoundTargetPosition.y, 0.0F) &&
        nearlyEqual(decoded.lightningGuns[0].currentTargetBounds.radius, 0.5F) &&
        nearlyEqual(decoded.lightningGuns[0].rewoundTargetBounds.halfHeight, 0.9F),
      "rewind diagnostics should round trip"
    );
    failures += expect(
      nearlyEqual(decoded.lightningGuns[0].knockbackImpulse.z, 0.3F),
      "3D knockback should round trip"
    );
    failures += expect(
      nearlyEqual(decoded.lightningGuns[0].freezeApplied, 2.5F),
      "beam freeze amount should round trip"
    );
    failures += expect(
      decoded.weaponFires[0].fired &&
        decoded.weaponFires[0].hit &&
        decoded.weaponFires[0].headshot &&
        decoded.weaponFires[0].weapon == lg::Weapon::Revolver &&
        decoded.weaponFires[0].damageApplied == 80 &&
        nearlyEqual(decoded.weaponFires[0].end.x, 9.0F) &&
        nearlyEqual(decoded.weaponFires[0].knockbackImpulse.x, 2.0F),
      "instant weapon events should round trip"
    );
    failures += expect(
      decoded.weaponFires[1].fired &&
        decoded.weaponFires[1].hit &&
        decoded.weaponFires[1].headshot &&
        decoded.weaponFires[1].weapon == lg::Weapon::Shotgun &&
        decoded.weaponFires[1].damageApplied == 45 &&
        decoded.weaponFires[1].pelletCount == lg::kShotgunPelletCount &&
        decoded.weaponFires[1].pelletHitCount == 9 &&
        decoded.weaponFires[1].pelletHeadshotCount == 3 &&
        decoded.weaponFires[1].visualSeed == 12 &&
        decoded.weaponFires[2].visualSeed == 77,
      "shotgun pellet event data should round trip"
    );
    failures += expect(
      decoded.localHitFeedbackEvents[0][0].active &&
        decoded.localHitFeedbackEvents[0][0].sequence == 17 &&
        decoded.localHitFeedbackEvents[0][0].targetPlayerIndex == 1 &&
        decoded.localHitFeedbackEvents[0][0].damageApplied == 45 &&
        decoded.localHitFeedbackEvents[0][0].headshot &&
        decoded.localHitFeedbackEvents[0][0].weapon == lg::Weapon::Shotgun &&
        decoded.localHitFeedbackEvents[0][1].active &&
        decoded.localHitFeedbackEvents[0][1].sequence == 18 &&
        decoded.localHitFeedbackEvents[0][1].targetPlayerIndex == 2 &&
        decoded.localHitFeedbackEvents[0][1].damageApplied == 80 &&
        decoded.localHitFeedbackEvents[0][1].weapon == lg::Weapon::RocketLauncher,
      "local hit feedback event window should round trip"
    );
    failures += expect(
      decoded.weaponFires[2].fired &&
        decoded.weaponFires[2].hit &&
        decoded.weaponFires[2].weapon == lg::Weapon::MachineGun &&
        decoded.weaponFires[2].damageApplied == 5 &&
        nearlyEqual(decoded.weaponFires[2].knockbackImpulse.x, 0.11F),
      "machine gun weapon events should round trip"
    );
    failures += expect(
      decoded.rocketExplosions[0].active &&
        decoded.rocketExplosions[0].weapon == lg::Weapon::GrenadeLauncher &&
        nearlyEqual(decoded.rocketExplosions[0].position.y, 4.0F) &&
        nearlyEqual(decoded.rocketExplosions[0].radius, 3.0F) &&
        decoded.rocketExplosions[0].ownerDamageApplied == 12 &&
        decoded.rocketExplosions[0].opponentDamageApplied == 80 &&
        decoded.rocketExplosions[0].sequence == 42 &&
        decoded.rocketExplosions[0].projectileSequence == 73 &&
        decoded.footstepAudioEvents[1].active &&
        decoded.footstepAudioEvents[1].jumping &&
        decoded.footstepAudioEvents[1].landing &&
        decoded.footstepAudioEvents[1].sequence == 42 &&
        nearlyEqual(decoded.footstepAudioEvents[1].position.z, 1.5F) &&
        decoded.grenadeBounceAudioEvents[0].active &&
        decoded.grenadeBounceAudioEvents[0].sequence == 9 &&
        nearlyEqual(decoded.grenadeBounceAudioEvents[0].position.z, 0.75F) &&
        decoded.fragEvents[0].active &&
        decoded.fragEvents[0].sequence == 55 &&
        decoded.fragEvents[0].targetPlayerIndex == 1 &&
        decoded.fragEvents[0].weapon == lg::Weapon::Railgun,
      "explosion, footstep audio, grenade audio, and frag events should round trip"
    );
    failures += expect(
      decoded.icePools[0].active &&
        nearlyEqual(decoded.icePools[0].center.x, 1.0F) &&
        nearlyEqual(decoded.icePools[0].center.y, 2.0F) &&
        nearlyEqual(decoded.icePools[0].normal.z, 1.0F) &&
        nearlyEqual(decoded.icePools[0].radius, 1.25F) &&
        nearlyEqual(decoded.icePools[0].lifetimeSeconds, 2.5F),
      "ice pool snapshot should round trip"
    );
    failures += expect(
      decoded.healthPickupAvailable == source.healthPickupAvailable,
      "health pickup availability bits should round trip"
    );
    failures += expect(decoded.respawnTicksRemaining[1] == 88, "respawn timer should round trip");
    failures += expect(decoded.scores == source.scores, "scores should round trip");
    failures += expect(
      decoded.gameMode == lg::GameMode::ClanArena &&
        decoded.teams == source.teams &&
        decoded.teamScores == source.teamScores &&
        decoded.roundWinningTeam == lg::Team::Red &&
        decoded.matchWinningTeam == lg::Team::None,
      "gamemode and team match state should round trip"
    );
    failures += expect(
      !decoded.hasCombatStats && decoded.playerNames[0] == "yg" &&
        decoded.playerNames[1] == "opponent",
      "gameplay names should round trip without embedded combat aggregates"
    );
    failures += expect(
      decoded.connectedPlayers == source.connectedPlayers &&
        decoded.botPlayers == source.botPlayers &&
        decoded.participatingPlayers == source.participatingPlayers &&
        decoded.readyPlayers == source.readyPlayers,
      "lobby, bot, and participating-player state should round trip"
    );
    failures += expect(
      !decoded.hasCombatStats,
      "round combat stats should stay out of gameplay snapshots"
    );
    failures += expect(
      decoded.matchPhase == lg::MatchPhase::Countdown &&
        decoded.matchRules.showOpponentHealth &&
        decoded.matchRules.deathRespawnTicks == 250 &&
        decoded.phaseTicksRemaining == 321 &&
        decoded.overtime,
      "match rules and phase should round trip"
    );
    failures += expect(
      decoded.movementTuning.flightEnabled &&
      decoded.movementTuning.airControlEnabled &&
      nearlyEqual(decoded.movementTuning.groundAcceleration, 120.0F) &&
      nearlyEqual(decoded.movementTuning.airAcceleration, 2.0F) &&
      nearlyEqual(decoded.movementTuning.groundFriction, 6.0F) &&
      nearlyEqual(decoded.movementTuning.stopSpeed, 2.5F) &&
      nearlyEqual(decoded.movementTuning.maxGroundSpeed, 14.0F) &&
      nearlyEqual(decoded.movementTuning.dashTargetSpeed, 13.0F) &&
      nearlyEqual(decoded.movementTuning.dashMaxSpeed, 14.5F) &&
      nearlyEqual(decoded.movementTuning.dashAcceleration, 240.0F) &&
      nearlyEqual(decoded.movementTuning.dashDuration, 0.12F) &&
      nearlyEqual(decoded.movementTuning.dashCooldown, 0.7F) &&
      nearlyEqual(decoded.movementTuning.dashGroundHopVelocity, 3.6F) &&
      nearlyEqual(decoded.movementTuning.dashAirHopVelocity, 2.1F) &&
      nearlyEqual(decoded.movementTuning.flightAcceleration, 48.0F) &&
      nearlyEqual(decoded.movementTuning.maxFlightSpeed, 16.0F) &&
      nearlyEqual(decoded.movementTuning.flightDamping, 1.5F) &&
      nearlyEqual(decoded.movementTuning.flightGravityCancel, 1.0F) &&
      nearlyEqual(decoded.playerSizeScaleXY, 1.75F) &&
      nearlyEqual(decoded.playerSizeScaleZ, 0.75F) &&
      nearlyEqual(decoded.lightningKnockback, 1500.0F) &&
      nearlyEqual(decoded.lightningFireHz, 25.0F) &&
      nearlyEqual(decoded.rocketKnockback, 625.0F) &&
      decoded.knockbackTimeMs == 125 &&
      decoded.weaponDamage.shotgunDamagePerPellet == 11 &&
      decoded.weaponDamage.machineGunDamage == 13 &&
      decoded.weaponDamage.lightningGunDamage == 90 &&
      decoded.weaponDamage.railgunDamage == 50 &&
      decoded.weaponDamage.rocketLauncherDamage == 140 &&
      decoded.weaponDamage.plasmaGunDamage == 25 &&
      decoded.weaponDamage.freezeGunDamage == 95 &&
      nearlyEqual(decoded.icePoolTuning.maxRadius, 3.0F) &&
      nearlyEqual(decoded.icePoolTuning.growthPerSecond, 11.0F) &&
      nearlyEqual(decoded.icePoolTuning.lifetimeSeconds, 4.0F) &&
      nearlyEqual(decoded.icePoolTuning.friction, 0.75F) &&
      nearlyEqual(decoded.icePoolTuning.slopeGravityScale, 1.25F) &&
      nearlyEqual(decoded.icePoolTuning.controlScale, 0.25F) &&
      nearlyEqual(decoded.icePoolTuning.mergeDistance, 1.4F) &&
      decoded.projectilePresentation.rocketLifetimeTicks == 625U &&
      decoded.projectilePresentation.grenadeFuseTicks == 250U &&
      decoded.projectilePresentation.plasmaLifetimeTicks == 150U &&
      nearlyEqual(decoded.projectilePresentation.grenadeGravity, 12.0F) &&
      nearlyEqual(
        decoded.projectilePresentation.grenadeBounceDamping,
        0.7F
      ) &&
      nearlyEqual(decoded.projectilePresentation.grenadeRestSpeed, 0.8F) &&
      !decoded.weaponAmmo.infiniteAmmo &&
      decoded.weaponAmmo.spawnAmmo == source.weaponAmmo.spawnAmmo &&
      nearlyEqual(decoded.vampirism, 2.0F) &&
      decoded.selfDamagePercent == 25 &&
      decoded.healthAmount == 150 &&
      decoded.botDodgeEnabled &&
      decoded.botDodgeMinIntervalMs == 300 &&
      decoded.botDodgeMaxIntervalMs == 700 &&
      decoded.botWeapon == lg::Weapon::RocketLauncher &&
      decoded.weaponSwitchingMode == lg::WeaponSwitchingMode::Ql,
      "authoritative movement tuning should round trip"
    );
    failures += expect(decoded.playersColliding, "collision diagnostic should round trip");

    // Ordinary gameplay snapshots omit both recoverable blocks. Configuration
    // is retransmitted until acknowledged; scoreboard aggregates use their own
    // packet and never inflate the latency-sensitive snapshot transport path.
    lg::ServerSnapshot leanSnapshot;
    leanSnapshot.map = testMapDescriptor();
    leanSnapshot.connectedPlayers[0] = true;
    leanSnapshot.connectedPlayers[1] = true;
    leanSnapshot.participatingPlayers[0] = true;
    leanSnapshot.participatingPlayers[1] = true;
    leanSnapshot.hasCombatStats = false;
    leanSnapshot.hasConfiguration = false;
    leanSnapshot.hasPlayerNames = false;
    failures += expect(lg::encodeServerSnapshot(leanSnapshot, wire),
                       "lean snapshot should encode");
    const std::size_t leanBytes = wire.size();
    const bool leanCompressed = wire[7] == 1U;
    lg::ServerSnapshot decodedLean;
    failures += expect(
      lg::decodeServerSnapshot(wire, decodedLean) &&
        !decodedLean.hasCombatStats && !decodedLean.hasConfiguration,
      "lean snapshot should advertise omitted recoverable blocks"
    );

    lg::ServerSnapshot fullConfigSnapshot = leanSnapshot;
    fullConfigSnapshot.hasConfiguration = true;
    failures += expect(lg::encodeServerSnapshot(fullConfigSnapshot, wire),
                       "configuration refresh snapshot should encode");
    const std::size_t configurationBytes = wire.size();
    const bool configurationCompressed = wire[7] == 1U;

    lg::ServerSnapshot fullSnapshot = fullConfigSnapshot;
    fullSnapshot.hasCombatStats = true;
    fullSnapshot.hasPlayerNames = true;
    failures += expect(lg::encodeServerSnapshot(fullSnapshot, wire),
                       "full refresh snapshot should encode");
    const std::size_t fullBytes = wire.size();
    const bool fullCompressed = wire[7] == 1U;
    lg::ServerSnapshot nameRefreshSnapshot = leanSnapshot;
    nameRefreshSnapshot.hasPlayerNames = true;
    failures += expect(
      lg::encodeServerSnapshot(nameRefreshSnapshot, wire),
      "name refresh snapshot should encode"
    );
    const std::size_t nameRefreshBytes = wire.size();
    failures += expect(
      fullBytes <= lg::kMaxUdpApplicationDatagramBytes,
      "compressible combat-statistics fixture should stay within one datagram"
    );
    failures += expect(leanBytes < 2500 && configurationBytes < 2500,
                       "normal and configuration refresh snapshots should stay below budget");
    std::cout << "snapshot bytes: gameplay=" << leanBytes
              << " configuration-retry=" << configurationBytes
              << " embedded-stats-fixture=" << fullBytes
              << " name-refresh=" << nameRefreshBytes
              << " compressed=" << leanCompressed << '/'
              << configurationCompressed << '/' << fullCompressed << '\n';

    lg::CombatStatsPacket statsPacket;
    statsPacket.serverTick = 1234;
    statsPacket.round = source.roundCombatStats;
    statsPacket.match = source.matchCombatStats;
    failures += expect(lg::encodeCombatStatsPacket(statsPacket, wire),
                       "combat statistics packet should encode");
    const std::size_t statsBytes = wire.size();
    lg::CombatStatsPacket decodedStats;
    failures += expect(
      statsBytes < 1200 &&
        lg::decodeCombatStatsPacket(wire, decodedStats) &&
        decodedStats.serverTick == statsPacket.serverTick &&
        decodedStats.match[0].weapons[0].damageDealt ==
          statsPacket.match[0].weapons[0].damageDealt,
      "combat statistics should round trip below the datagram budget"
    );
    std::cout << "combat-stats packet bytes=" << statsBytes << '\n';

    lg::CombatStatsPacket diverseStats;
    diverseStats.serverTick = 1235;
    diverseStats.firstPlayerIndex = 8;
    diverseStats.playerCount = 4;
    for (std::size_t player = 8; player < 12; ++player) {
      for (std::size_t weapon = 0; weapon < lg::kWeaponCount; ++weapon) {
        const std::uint32_t seed = static_cast<std::uint32_t>(
          (player + 1U) * 2654435761U + weapon * 2246822519U
        );
        diverseStats.round[player].weapons[weapon] = {
          seed,
          static_cast<std::uint16_t>(seed >> 8U),
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(seed >> 8U) / 2U),
        };
        diverseStats.match[player].weapons[weapon] = {
          seed ^ 0xA5A5A5A5U,
          static_cast<std::uint16_t>(seed >> 3U),
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(seed >> 3U) / 2U),
        };
      }
    }
    failures += expect(
      lg::encodeCombatStatsPacket(diverseStats, wire) &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes &&
        lg::decodeCombatStatsPacket(wire, decodedStats) &&
        decodedStats.firstPlayerIndex == 8U &&
        decodedStats.playerCount == 4U &&
        decodedStats.match[11].weapons[8].damageDealt ==
          diverseStats.match[11].weapons[8].damageDealt,
      "diverse combat statistics pages should remain below the datagram ceiling"
    );
    diverseStats.firstPlayerIndex = 15;
    diverseStats.playerCount = 2;
    failures += expect(
      !lg::encodeCombatStatsPacket(diverseStats, wire),
      "combat statistics pages should reject ranges beyond player capacity"
    );
    lg::ServerSnapshot sixPlayerSnapshot = leanSnapshot;
    sixPlayerSnapshot.gameMode = lg::GameMode::ClanArena;
    sixPlayerSnapshot.matchRules.playerLimit =
      static_cast<std::uint8_t>(lg::kDuelPlayerCount);
    sixPlayerSnapshot.connectedPlayers.fill(true);
    sixPlayerSnapshot.participatingPlayers.fill(true);
    sixPlayerSnapshot.teams = {
      lg::Team::Red,
      lg::Team::Blue,
      lg::Team::Red,
      lg::Team::Blue,
      lg::Team::Red,
      lg::Team::Blue,
    };
    failures += expect(
      lg::encodeServerSnapshot(sixPlayerSnapshot, wire),
      "typical six-player snapshot should encode"
    );
    const std::size_t sixPlayerSnapshotBytes = wire.size();
    const bool sixPlayerCompressed = wire[7] == 1U;
    failures += expect(
      sixPlayerSnapshotBytes < 2500,
      "typical six-player snapshot should remain below 2500 bytes"
    );
    std::cout << "snapshot bytes: duel=" << leanBytes
               << " duel-full=" << fullBytes
               << " six-player=" << sixPlayerSnapshotBytes
               << " active-combat=" << activeCombatSnapshotBytes
               << " compressed=" << leanCompressed << '/'
               << fullCompressed << '/' << sixPlayerCompressed << '/'
               << activeCombatCompressed << '\n';

    lg::ServerSnapshot sixteenPlayerSnapshot = leanSnapshot;
    sixteenPlayerSnapshot.gameMode = lg::GameMode::ClanArena;
    sixteenPlayerSnapshot.matchRules.playerLimit =
      static_cast<std::uint8_t>(lg::kDuelPlayerCount);
    sixteenPlayerSnapshot.connectedPlayers.fill(true);
    sixteenPlayerSnapshot.participatingPlayers.fill(true);
    sixteenPlayerSnapshot.hasAcknowledgedCommand.fill(true);
    for (std::size_t index = 0; index < lg::kDuelPlayerCount; ++index) {
      sixteenPlayerSnapshot.acknowledgedCommand[index] =
        static_cast<std::uint32_t>(100U + index);
      sixteenPlayerSnapshot.players[index].position = {
        static_cast<float>(index),
        -static_cast<float>(index) * 0.5F,
        0.9F + static_cast<float>(index) * 0.01F,
      };
      sixteenPlayerSnapshot.selectedWeapons[index] =
        static_cast<lg::Weapon>(index % lg::kWeaponCount);
      sixteenPlayerSnapshot.teams[index] =
        (index % 2U) == 0U ? lg::Team::Red : lg::Team::Blue;
    }
    lg::LocalHitFeedbackEvent& lastWindowEvent =
      sixteenPlayerSnapshot.localHitFeedbackEvents[15][3];
    lastWindowEvent.active = true;
    lastWindowEvent.sequence = 0x12345678U;
    lastWindowEvent.targetPlayerIndex = 0;
    lastWindowEvent.damageApplied = 77;
    lastWindowEvent.headshot = true;
    lastWindowEvent.weapon = lg::Weapon::Railgun;
    failures += expect(
      lg::encodeServerSnapshot(sixteenPlayerSnapshot, wire) &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes,
      "typical sixteen-player snapshot should fit one UDP datagram"
    );
    const std::size_t sixteenPlayerSnapshotBytes = wire.size();
    lg::ServerSnapshot decodedSixteenPlayers;
    failures += expect(
      lg::decodeServerSnapshot(wire, decodedSixteenPlayers) &&
        decodedSixteenPlayers.connectedPlayers ==
          sixteenPlayerSnapshot.connectedPlayers &&
        nearlyEqual(decodedSixteenPlayers.players[15].position.x,
                    sixteenPlayerSnapshot.players[15].position.x) &&
        nearlyEqual(decodedSixteenPlayers.players[15].position.z,
                    sixteenPlayerSnapshot.players[15].position.z) &&
        decodedSixteenPlayers.acknowledgedCommand ==
          sixteenPlayerSnapshot.acknowledgedCommand &&
        decodedSixteenPlayers.playerNames[15] == "PLAYER 16" &&
        decodedSixteenPlayers.localHitFeedbackEvents[15][3].active &&
        decodedSixteenPlayers.localHitFeedbackEvents[15][3].sequence ==
          lastWindowEvent.sequence,
      "all sixteen slots and the final hit-feedback window should round trip"
    );
    std::cout << "snapshot bytes: sixteen-player="
              << sixteenPlayerSnapshotBytes
              << " compressed=" << (wire[7] == 1U) << '\n';

    lg::ServerSnapshot retainedDamageBurst = leanSnapshot;
    retainedDamageBurst.hasLocalClientState = true;
    retainedDamageBurst.localPlayerIndex = 0;
    for (std::size_t slot = 0; slot < lg::kDamageTakenEventWindow; ++slot) {
      lg::DamageTakenEventRing& ring = retainedDamageBurst.damageTakenEvents[0];
      ring.events[slot] = {
        static_cast<std::uint32_t>(1000U + slot),
        static_cast<std::uint8_t>(slot * 31U),
        255U,
        static_cast<std::uint8_t>(
          lg::kDamageTakenDirectionValid |
          lg::kDamageTakenAttackerValid |
          ((slot % lg::kDuelPlayerCount) << 4U) |
          (slot == 0U ? lg::kDamageTakenSelfDamage : 0U)
        ),
        lg::Weapon::RocketLauncher,
      };
      (void)lg::setDamageTakenEventActive(ring, slot);
    }
    failures += expect(
      lg::encodeBoundedGameplaySnapshot(retainedDamageBurst, wire) &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes,
      "a recipient's full retained damage ring should fit one UDP datagram"
    );
    lg::ServerSnapshot decodedRetainedDamageBurst;
    bool fullRetainedRingRoundTrips =
      lg::decodeServerSnapshot(wire, decodedRetainedDamageBurst);
    for (std::size_t slot = 0;
         fullRetainedRingRoundTrips && slot < lg::kDamageTakenEventWindow;
         ++slot) {
      fullRetainedRingRoundTrips =
        lg::damageTakenEventActive(decodedRetainedDamageBurst.damageTakenEvents[0], slot) &&
        decodedRetainedDamageBurst.damageTakenEvents[0].events[slot].sequence ==
          static_cast<std::uint32_t>(1000U + slot);
    }
    failures += expect(
      fullRetainedRingRoundTrips,
      "bounded snapshots should retain every event in the full victim ring"
    );
    std::cout << "snapshot bytes: retained-damage-burst=" << wire.size()
              << " compressed=" << (wire[7] == 1U) << '\n';

    lg::ServerSnapshot unboundedBurst = sixteenPlayerSnapshot;
    for (std::size_t player = 0; player < lg::kDuelPlayerCount; ++player) {
      auto& beam = unboundedBurst.lightningGuns[player];
      beam.active = true;
      beam.hit = true;
      beam.targetPlayerIndex = static_cast<std::uint8_t>((player + 1U) %
        lg::kDuelPlayerCount);
      beam.damageApplied = static_cast<int>(player + 1U);
      beam.start = {static_cast<float>(player * 17U + 1U),
                    static_cast<float>(player * 19U + 2U),
                    static_cast<float>(player * 23U + 3U)};
      beam.end = {beam.start.x + 101.25F, beam.start.y + 203.5F,
                  beam.start.z + 307.75F};
      beam.hasRewindDebug = true;
      beam.rewindTargetTick = static_cast<std::uint32_t>(500U + player);
      beam.currentTargetPosition = beam.start;
      beam.rewoundTargetPosition = beam.end;
      for (std::size_t event = 0;
           event < lg::kLocalHitFeedbackEventWindow;
           ++event) {
        auto& feedback = unboundedBurst.localHitFeedbackEvents[player][event];
        feedback.active = true;
        feedback.sequence = static_cast<std::uint32_t>(player * 101U + event);
        feedback.targetPlayerIndex = beam.targetPlayerIndex;
        feedback.damageApplied = static_cast<int>(player * 7U + event + 1U);
        feedback.weapon = lg::Weapon::LightningGun;
      }
    }
    failures += expect(
      !lg::encodeServerSnapshot(unboundedBurst, wire) && wire.empty(),
      "an incompressible event burst should fail instead of exceeding 1200 bytes"
    );

    lg::ServerSnapshot twoBeamBurst = sixteenPlayerSnapshot;
    twoBeamBurst.players[0].health = 94;
    twoBeamBurst.players[1].health = 94;
    for (std::size_t player = 0; player < 2U; ++player) {
      auto& beam = twoBeamBurst.lightningGuns[player];
      beam.active = true;
      beam.hit = true;
      beam.targetPlayerIndex = static_cast<std::uint8_t>(1U - player);
      beam.damageApplied = 6;
      beam.start = {
        static_cast<float>(player * 41U + 3U),
        static_cast<float>(player * 43U + 5U),
        static_cast<float>(player * 47U + 7U),
      };
      beam.end = {
        beam.start.x + 101.25F,
        beam.start.y + 203.5F,
        beam.start.z + 307.75F,
      };
      beam.hasRewindDebug = true;
      beam.rewindTargetTick = static_cast<std::uint32_t>(900U + player);
      beam.currentTargetPosition = {
        beam.start.x + 11.5F,
        beam.start.y + 13.75F,
        beam.start.z + 17.25F,
      };
      beam.rewoundTargetPosition = {
        beam.end.x - 19.5F,
        beam.end.y - 23.75F,
        beam.end.z - 29.25F,
      };
    }
    for (std::size_t player = 0; player < lg::kDuelPlayerCount; ++player) {
      auto& footstep = twoBeamBurst.footstepAudioEvents[player];
      footstep.active = true;
      footstep.sequence = static_cast<std::uint32_t>(3000U + player);
      footstep.position = {
        static_cast<float>(player * 31U + 1U),
        static_cast<float>(player * 37U + 2U),
        static_cast<float>(player * 41U + 3U),
      };
      auto& bounce = twoBeamBurst.grenadeBounceAudioEvents[player];
      bounce.active = true;
      bounce.sequence = static_cast<std::uint32_t>(4000U + player);
      bounce.position = {
        static_cast<float>(player * 43U + 4U),
        static_cast<float>(player * 47U + 5U),
        static_cast<float>(player * 53U + 6U),
      };
    }
    failures += expect(
      lg::encodeServerSnapshot(twoBeamBurst, wire) &&
        wire.size() <= lg::kMaxUdpApplicationDatagramBytes,
      "two beams plus low-priority debug events should fit after packet cuts"
    );
    lg::WirePacket boundedTwoBeamWire;
    lg::ServerSnapshot decodedTwoBeam;
    failures += expect(
      lg::encodeBoundedGameplaySnapshot(twoBeamBurst, boundedTwoBeamWire) &&
        boundedTwoBeamWire.size() <= lg::kMaxUdpApplicationDatagramBytes &&
        lg::decodeServerSnapshot(boundedTwoBeamWire, decodedTwoBeam) &&
        decodedTwoBeam.players[0].health == 94 &&
        decodedTwoBeam.players[1].health == 94 &&
        decodedTwoBeam.lightningGuns[0].active &&
        decodedTwoBeam.lightningGuns[1].active &&
        decodedTwoBeam.lightningGuns[0].damageApplied == 6 &&
        decodedTwoBeam.lightningGuns[1].damageApplied == 6 &&
        decodedTwoBeam.lightningGuns[0].hasRewindDebug &&
        decodedTwoBeam.lightningGuns[1].hasRewindDebug &&
        twoBeamBurst.lightningGuns[0].hasRewindDebug &&
        twoBeamBurst.lightningGuns[1].hasRewindDebug,
      "bounded gameplay encoding should preserve a snapshot that already fits"
    );

    lg::ServerSnapshot recipientlessBurst = unboundedBurst;
    for (auto& events : recipientlessBurst.localHitFeedbackEvents) {
      events.fill({});
    }
    for (std::size_t player = 0; player < 4U; ++player) {
      for (
        std::size_t event = 0;
        event < lg::kLocalHitFeedbackEventWindow;
        ++event
      ) {
        auto& feedback =
          recipientlessBurst.localHitFeedbackEvents[player][event];
        feedback.active = true;
        feedback.sequence = static_cast<std::uint32_t>(
          7001U + player * lg::kLocalHitFeedbackEventWindow + event
        );
        feedback.targetPlayerIndex =
          static_cast<std::uint8_t>((player + 1U) % 4U);
        feedback.damageApplied = 6;
        feedback.weapon = lg::Weapon::LightningGun;
      }
    }
    auto& preservedFeedback =
      recipientlessBurst.localHitFeedbackEvents[0][0];
    recipientlessBurst.hasLocalClientState = false;
    recipientlessBurst.localPlayerIndex = lg::kNoAssignedPlayer;
    lg::WirePacket recipientlessWire;
    lg::ServerSnapshot decodedRecipientless;
    failures += expect(
      lg::encodeBoundedGameplaySnapshot(
        recipientlessBurst,
        recipientlessWire
      ) &&
        recipientlessWire.size() <=
          lg::kMaxUdpApplicationDatagramBytes &&
        lg::decodeServerSnapshot(
          recipientlessWire,
          decodedRecipientless
        ) &&
        decodedRecipientless.lightningGuns[0].active &&
        decodedRecipientless.localHitFeedbackEvents[0][0].active &&
        decodedRecipientless.localHitFeedbackEvents[0][0].sequence ==
          preservedFeedback.sequence &&
        decodedRecipientless.localHitFeedbackEvents[0][0].damageApplied ==
          preservedFeedback.damageApplied,
      "recipient-free bounded encoding should preserve fitting feedback"
    );

    failures += expect(
      lg::encodeServerSnapshot(sixteenPlayerSnapshot, wire) && wire[7] == 1U,
      "sixteen-player fixture should exercise compressed snapshot framing"
    );
    lg::WirePacket malformedCompressed = wire;
    malformedCompressed.pop_back();
    malformedCompressed[8] = static_cast<std::uint8_t>(
      malformedCompressed.size() - 12U
    );
    malformedCompressed[9] = static_cast<std::uint8_t>(
      (malformedCompressed.size() - 12U) >> 8U
    );
    failures += expect(
      !lg::decodeServerSnapshot(malformedCompressed, decoded),
      "truncated compressed snapshots should be rejected"
    );
    malformedCompressed = wire;
    malformedCompressed[14] |= 1U;
    malformedCompressed[15] = 0xFFU;
    malformedCompressed[16] = 0x0FU;
    failures += expect(
      !lg::decodeServerSnapshot(malformedCompressed, decoded),
      "compressed snapshots should reject back-references before output"
    );
    malformedCompressed = wire;
    malformedCompressed[12] = 0xFFU;
    malformedCompressed[13] = 0xFFU;
    failures += expect(
      !lg::decodeServerSnapshot(malformedCompressed, decoded),
      "compressed snapshot expansion must match its bounded declared size"
    );

    lg::Arena smallArena;
    lg::Arena largeArena = smallArena;
    largeArena.wallCount = 160;
    for (std::size_t wallIndex = 0; wallIndex < largeArena.wallCount; ++wallIndex) {
      const float x = static_cast<float>(wallIndex) * 2.0F;
      largeArena.walls[wallIndex] = {
        {x, 0.0F, 0.0F},
        {x + 1.0F, 1.0F, 1.0F},
      };
    }

    lg::ServerSnapshot smallMapSnapshot;
    smallMapSnapshot.serverTick = 55;
    smallMapSnapshot.mapRevision = 3;
    smallMapSnapshot.map = {"small_map", lg::hashArena(smallArena)};
    failures += expect(
      lg::encodeServerSnapshot(smallMapSnapshot, wire),
      "small map descriptor snapshot should encode"
    );
    failures += expect(
      wire.size() <= lg::kMaxPacketBytes,
      "small map descriptor snapshot should respect packet limit"
    );
    const std::size_t smallMapSnapshotBytes = wire.size();
    lg::ServerSnapshot decodedLargeMap;

    lg::ServerSnapshot largeMapSnapshot = smallMapSnapshot;
    largeMapSnapshot.map = {"large_map", lg::hashArena(largeArena)};
    failures += expect(
      lg::encodeServerSnapshot(largeMapSnapshot, wire),
      "large map descriptor snapshot should encode"
    );
    failures += expect(
      wire.size() <= smallMapSnapshotBytes + 16U,
      "snapshot wire size should not materially scale with static map complexity"
    );
    failures += expect(
      lg::decodeServerSnapshot(wire, decodedLargeMap),
      "large map descriptor snapshot should decode"
    );
    failures += expect(
      decodedLargeMap.map.mapName == "large_map" &&
        decodedLargeMap.map.contentHash == largeMapSnapshot.map.contentHash &&
        decodedLargeMap.mapRevision == largeMapSnapshot.mapRevision,
      "large map descriptor snapshot should preserve revision without arena payload"
    );

    lg::DisconnectPacket disconnect{12345};
    lg::DisconnectPacket decodedDisconnect;
    failures += expect(
      lg::encodeDisconnectPacket(disconnect, wire),
      "disconnect should encode"
    );
    failures += expect(
      lg::decodeDisconnectPacket(wire, decodedDisconnect) &&
        decodedDisconnect.clientNonce == 12345,
      "disconnect should round trip"
    );

    lg::WirePacket oversized(lg::kMaxPacketBytes + 1, 0);
    failures += expect(
      !lg::decodeServerSnapshot(oversized, decoded),
      "oversized snapshot should be rejected"
    );

    lg::ServerSnapshot invalid = source;
    invalid.players[0].position.x = std::numeric_limits<float>::infinity();
    failures += expect(!lg::encodeServerSnapshot(invalid, wire), "non-finite snapshot should not encode");

    invalid = source;
    invalid.teams[0] = static_cast<lg::Team>(255);
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "invalid snapshot team should not encode"
    );

    invalid = source;
    invalid.projectileRevision = 0U;
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "zero snapshot projectile generation should not encode"
    );

    invalid = source;
    invalid.footstepAudioEvents[0].active = true;
    invalid.footstepAudioEvents[0].position.x =
      std::numeric_limits<float>::infinity();
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "non-finite footstep audio event should not encode"
    );

    invalid = source;
    invalid.grenadeBounceAudioEvents[0].position.x =
      std::numeric_limits<float>::infinity();
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "non-finite grenade bounce audio event should not encode"
    );

    invalid = source;
    invalid.projectilePresentation.grenadeBounceDamping = 2.0F;
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "out-of-range projectile presentation tuning should not encode"
    );

    invalid = source;
    invalid.projectilePresentation.grenadeGravity =
      std::numeric_limits<float>::quiet_NaN();
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "non-finite projectile presentation tuning should not encode"
    );

    invalid = source;
    invalid.fragEvents[0].active = true;
    invalid.fragEvents[0].targetPlayerIndex = lg::kDuelPlayerCount;
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "invalid frag target should not encode"
    );

    invalid = source;
    invalid.fragEvents[0].active = true;
    invalid.fragEvents[0].weapon = static_cast<lg::Weapon>(255);
    failures += expect(
      !lg::encodeServerSnapshot(invalid, wire),
      "invalid frag weapon should not encode"
    );

    lg::ServerSnapshot emptySparseSnapshot;
    emptySparseSnapshot.map = testMapDescriptor();
    emptySparseSnapshot.hasCombatStats = false;
    lg::WirePacket emptySparseWire;
    failures += expect(
      lg::encodeServerSnapshot(emptySparseSnapshot, emptySparseWire),
      "empty sparse snapshot should encode"
    );
    lg::ServerSnapshot oneLightningSnapshot = emptySparseSnapshot;
    oneLightningSnapshot.lightningGuns[0].active = true;
    lg::WirePacket oneLightningWire;
    failures += expect(
      lg::encodeServerSnapshot(oneLightningSnapshot, oneLightningWire),
      "single sparse event snapshot should encode"
    );
    std::size_t sparseMaskOffset = 12U;
    while (sparseMaskOffset < emptySparseWire.size() &&
           sparseMaskOffset < oneLightningWire.size() &&
           emptySparseWire[sparseMaskOffset] == oneLightningWire[sparseMaskOffset]) {
      ++sparseMaskOffset;
    }
    failures += expect(
      sparseMaskOffset < emptySparseWire.size(),
      "sparse mask offset should be observable from a single active slot"
    );
    if (sparseMaskOffset + 3U < emptySparseWire.size()) {
      // The first differing payload byte is the little-endian lightning mask.
      // Bit 31 is outside the six-player array and must be rejected.
      emptySparseWire[sparseMaskOffset + 3U] |= 0x80U;
      failures += expect(
        !lg::decodeServerSnapshot(emptySparseWire, decoded),
        "sparse masks should reject unused high slot bits"
      );
    }
  }

  {
    lg::ServerSnapshot source;
    source.map = testMapDescriptor();

    lg::WirePacket wire;
    lg::ServerSnapshot decoded;
    failures += expect(
      lg::encodeServerSnapshot(source, wire),
      "snapshot without embedded chat should encode"
    );
    failures += expect(
      lg::decodeServerSnapshot(wire, decoded),
      "snapshot without embedded chat should decode"
    );
    failures += expect(
      decoded.serverTick == source.serverTick,
      "snapshot without embedded chat should still round trip"
    );
  }

  {
    lg::ChatHistoryChunk source;
    source.oldestAvailableSequence = 7U;
    source.latestSequence = 10U;
    source.messageCount = 4U;
    source.messages[0] = {7U, 1U, "Zap Witch", "snyggt åäöÅÄÖ"};
    source.messages[1] = {8U, 0U, "yg", std::string(lg::kMaxChatMessageBytes, 's')};
    source.messages[2] = {9U, 1U, "Zap Witch", std::string(lg::kMaxChatMessageBytes, 't')};
    source.messages[3] = {
      10U,
      lg::kNoAssignedPlayer,
      "Observer",
      std::string(lg::kMaxChatMessageBytes, 'u')
    };
    lg::WirePacket wire;
    lg::ChatHistoryChunk decoded;
    failures += expect(
      lg::encodeChatHistoryChunk(source, wire) && wire.size() < 1200U,
      "maximum chat-history chunks should remain MTU-friendly"
    );
    failures += expect(
      lg::decodeChatHistoryChunk(wire, decoded) &&
        decoded.oldestAvailableSequence == 7U &&
        decoded.latestSequence == 10U &&
        decoded.messageCount == 4U &&
        decoded.messages[0].speakerName == "Zap Witch" &&
        decoded.messages[0].message == "snyggt åäöÅÄÖ" &&
        decoded.messages[1].message == std::string(lg::kMaxChatMessageBytes, 's') &&
        decoded.messages[3].playerIndex == lg::kNoAssignedPlayer,
      "chat history should round trip names, UTF-8, spectators, and long messages"
    );

    lg::ChatHistoryAck ack;
    failures += expect(
      lg::encodeChatHistoryAck({8U}, wire) &&
        lg::decodeChatHistoryAck(wire, ack) &&
        ack.sequence == 8U,
      "chat history acknowledgements should round trip"
    );
  }

  return failures == 0 ? 0 : 1;
}
