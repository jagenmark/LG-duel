#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/Arena.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/UserCommand.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr float kPi = 3.14159265359F;

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::ServerSnapshot latestSnapshot(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot latest;
  lg::ServerSnapshot received;
  while (transport.receiveSnapshot(received)) {
    latest = received;
  }
  return latest;
}

std::string basicMapWithBrush(std::string brush) {
  return
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "\"lg_bounds_min\" \"-160 -160 -40\"\n"
    "\"lg_bounds_max\" \"160 160 160\"\n" +
    brush +
    "}\n"
    "{\n"
    "\"classname\" \"lg_spawn\"\n"
    "\"origin\" \"0 0 24\"\n"
    "}\n"
    "{\n"
    "\"classname\" \"lg_spawn\"\n"
    "\"origin\" \"120 0 24\"\n"
    "}\n";
}

float walkableSurfaceZ(const lg::ArenaBrush& brush, lg::Vec3 position) {
  float bestZ = 0.0F;
  float bestNormalZ = 0.0F;
  for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
    const lg::ArenaBrushFace& face = brush.faces[faceIndex];
    if (face.normal.z <= bestNormalZ || face.normal.z <= 0.35F) {
      continue;
    }
    bestNormalZ = face.normal.z;
    bestZ =
      (face.distance - (face.normal.x * position.x) - (face.normal.y * position.y)) /
      face.normal.z;
  }
  return bestZ;
}

lg::Arena triangularSlopedBrushArena() {
  const std::string triangularRampBrush =
    "{\n"
    "( -80 -80 0 ) ( -80 -80 32 ) ( -80 80 32 ) stone 0 0 0 1 1\n"
    "( -80 -80 0 ) ( 80 -80 0 ) ( -80 -80 32 ) stone 0 0 0 1 1\n"
    "( 80 -80 0 ) ( 80 80 0 ) ( 80 80 48 ) stone 0 0 0 1 1\n"
    "( -80 80 0 ) ( -80 80 32 ) ( 80 80 48 ) stone 0 0 0 1 1\n"
    "( -80 -80 0 ) ( -80 80 0 ) ( 80 80 0 ) stone 0 0 0 1 1\n"
    "( -80 -80 32 ) ( 80 -80 0 ) ( 80 80 48 ) stone 0 0 0 1 1\n"
    "}\n";
  lg::ArenaLoadResult loaded =
    lg::loadArenaFromMapText(basicMapWithBrush(triangularRampBrush));
  if (!loaded.ok || loaded.arena.brushCount == 0) {
    return {};
  }

  lg::Arena arena = loaded.arena;
  arena.spawnPositions[0] = {
    -0.1F,
    0.0F,
    walkableSurfaceZ(arena.brushes[0], {-0.1F, 0.0F, 0.0F}),
  };
  arena.spawnPositions[1] = {2.0F, 0.0F, arena.spawnPositions[0].z};
  return arena;
}

int runDownwardSelfKnockbackCase(
  lg::Weapon weapon,
  std::string_view weaponName,
  float minimumExplosionVelocityZ,
  float minimumNextVelocityZ,
  float minimumNextPositionDeltaZ
) {
  int failures = 0;
  lg::LoopbackTransport transport;
  lg::ServerGame server(transport);
  const lg::Arena arena = triangularSlopedBrushArena();
  failures += expect(arena.brushCount == 1, "triangular sloped brush arena should load");
  server.setArena(arena);
  latestSnapshot(transport);

  lg::CommandPacket noSelfDamage;
  noSelfDamage.command.sequence = 1;
  noSelfDamage.requestMovementTuning = true;
  noSelfDamage.selfDamagePercent = 0;
  noSelfDamage.rocketKnockback = 1000.0F;
  transport.sendCommand(noSelfDamage);
  server.tick(lg::kFixedTickSeconds);
  latestSnapshot(transport);

  lg::UserCommand fireDown;
  fireDown.sequence = 2;
  fireDown.attack = true;
  fireDown.weapon = weapon;
  fireDown.planarAim = false;
  fireDown.viewPitchRadians = -kPi * 0.5F;
  lg::CommandPacket firePacket;
  firePacket.playerIndex = 0;
  firePacket.command = fireDown;
  transport.sendCommand(firePacket);

  lg::ServerSnapshot explosionSnapshot;
  bool exploded = false;
  for (int tick = 0; tick < 220; ++tick) {
    server.tick(lg::kFixedTickSeconds);
    explosionSnapshot = latestSnapshot(transport);
    if (explosionSnapshot.rocketExplosions[0].active) {
      exploded = true;
      break;
    }
  }

  failures += expect(exploded, "downward projectile should explode on triangular sloped brush");
  failures += expect(
    explosionSnapshot.rocketExplosions[0].weapon == weapon,
    "explosion should report the fired projectile weapon"
  );
  failures += expect(
    explosionSnapshot.rocketExplosions[0].ownerDamageApplied == 0,
    "self-damage disabled projectile should still report no owner damage"
  );
  failures += expect(
    explosionSnapshot.players[0].velocity.z > minimumExplosionVelocityZ,
    "projectile explosion should immediately apply upward self-knockback velocity"
  );

  const float explosionPositionZ = explosionSnapshot.players[0].position.z;
  server.tick(lg::kFixedTickSeconds);
  const lg::ServerSnapshot nextSnapshot = latestSnapshot(transport);
  const bool movedUpOnNextTick =
    nextSnapshot.players[0].position.z >
        explosionPositionZ + minimumNextPositionDeltaZ &&
    nextSnapshot.players[0].velocity.z > minimumNextVelocityZ;
  if (!movedUpOnNextTick) {
    std::cerr << "debug " << weaponName
              << ": explosion pos z=" << explosionPositionZ
              << " vel z=" << explosionSnapshot.players[0].velocity.z
              << " onGround=" << explosionSnapshot.players[0].onGround
              << " next pos z=" << nextSnapshot.players[0].position.z
              << " next vel z=" << nextSnapshot.players[0].velocity.z
              << " next onGround=" << nextSnapshot.players[0].onGround
              << '\n';
  }
  failures += expect(
    movedUpOnNextTick,
    "projectile self-knockback should move the player upward on the next tick"
  );
  return failures;
}

int runGrenadeBrushSideBounceCase() {
  int failures = 0;
  lg::LoopbackTransport transport;
  lg::ServerGame server(transport);
  const lg::Arena arena = triangularSlopedBrushArena();
  failures += expect(arena.brushCount == 1, "grenade brush-bounce arena should load");
  server.setArena(arena);

  lg::BalanceConfig config;
  config.grenadeLauncher.speed = 16.0F;
  config.grenadeLauncher.verticalBoost = 0.0F;
  config.grenadeLauncher.gravity = 0.0F;
  config.grenadeLauncher.bounceDamping = 0.65F;
  config.grenadeLauncher.bounceSoundMinSpeed = 0.1F;
  config.grenadeLauncher.projectileRadius = 0.05F;
  config.grenadeLauncher.projectileHitboxRadius = 0.0F;
  config.grenadeLauncher.fuseTicks = 250;
  config.grenadeLauncher.radius = 3.0F;
  config.grenadeLauncher.cooldownTicks = 100;
  server.applyBalanceConfig(config);
  latestSnapshot(transport);

  lg::UserCommand fireSide;
  fireSide.sequence = 1;
  fireSide.attack = true;
  fireSide.weapon = lg::Weapon::GrenadeLauncher;
  fireSide.viewYawRadians = 0.0F;
  fireSide.planarAim = true;
  lg::CommandPacket firePacket;
  firePacket.playerIndex = 0;
  firePacket.command = fireSide;
  transport.sendCommand(firePacket);

  lg::ServerSnapshot snapshot;
  bool bounced = false;
  for (int tick = 0; tick < 80; ++tick) {
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    bounced =
      snapshot.grenadeBounceAudioEvents[0].active ||
      (
        server.projectiles()[0].active &&
        server.projectiles()[0].weapon == lg::Weapon::GrenadeLauncher &&
        server.projectiles()[0].velocity.x < -1.0F
      );
    if (bounced) {
      break;
    }
  }

  failures += expect(bounced, "grenade should bounce from a triangular brush side");
  failures += expect(
    server.projectiles()[0].active &&
      server.projectiles()[0].weapon == lg::Weapon::GrenadeLauncher &&
      server.projectiles()[0].velocity.x < -1.0F,
    "brush-side grenade bounce should reflect horizontal velocity away from the brush"
  );
  failures += expect(
    !snapshot.rocketExplosions[0].active,
    "brush-side grenade bounce should not explode before fuse"
  );
  return failures;
}

} // namespace

int main() {
  int failures = 0;

  failures += runDownwardSelfKnockbackCase(
    lg::Weapon::RocketLauncher,
    "rocket",
    20.0F,
    19.0F,
    0.1F
  );
  failures += runDownwardSelfKnockbackCase(
    lg::Weapon::PlasmaGun,
    "plasma",
    2.0F,
    1.9F,
    0.005F
  );
  failures += runGrenadeBrushSideBounceCase();

  return failures == 0 ? 0 : 1;
}
