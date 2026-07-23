#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/BalanceConfig.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

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

lg::ServerSnapshot sendAndTick(
  lg::LoopbackTransport& transport,
  lg::ServerGame& server,
  const lg::UserCommand& command
) {
  lg::CommandPacket packet;
  packet.playerIndex = 0;
  packet.command = command;
  transport.sendCommand(packet);
  server.tick(lg::kFixedTickSeconds);
  return latestSnapshot(transport);
}

lg::UserCommand attackWith(lg::Weapon weapon, std::uint32_t sequence) {
  lg::UserCommand command;
  command.sequence = sequence;
  command.attack = true;
  command.weapon = weapon;
  return command;
}

lg::UserCommand bodyAttackWith(
  const lg::ServerSnapshot& snapshot,
  lg::Weapon weapon,
  std::uint32_t sequence
) {
  constexpr float weaponEyeHeight = 0.65F;
  constexpr float defaultPlayerHalfHeight = 0.9F;
  lg::UserCommand command = attackWith(weapon, sequence);
  const lg::PlayerState& attacker = snapshot.players[0];
  const lg::PlayerState& target = snapshot.players[1];
  const float scaledEyeHeight =
    weaponEyeHeight *
    (attacker.bounds.halfHeight / defaultPlayerHalfHeight);
  const lg::Vec3 muzzle =
    attacker.position + lg::Vec3{0.0F, 0.0F, scaledEyeHeight};
  const lg::Vec3 offset = target.position - muzzle;
  command.planarAim = false;
  command.viewYawRadians = std::atan2(offset.y, offset.x);
  command.viewPitchRadians = std::atan2(
    offset.z,
    std::hypot(offset.x, offset.y)
  );
  return command;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::BalanceConfig balance;
    balance.railgunCooldownTicks = 100;
    balance.revolver.damage = 7;
    balance.revolverCooldownTicks = 1;
    balance.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] = 9;
    balance.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Revolver)] = 11;
    server.applyBalanceConfig(balance);

    lg::UserCommand sniperMiss = attackWith(lg::Weapon::Railgun, 1);
    sniperMiss.viewYawRadians = 3.14159265F;
    lg::ServerSnapshot snapshot =
      sendAndTick(transport, server, sniperMiss);
    failures += expect(
      snapshot.weaponFires[0].fired,
      "setup Sniper shot should start only the Sniper cooldown"
    );

    lg::UserCommand release;
    release.sequence = 2;
    release.weapon = lg::Weapon::Revolver;
    sendAndTick(transport, server, release);
    snapshot =
      sendAndTick(transport, server, attackWith(lg::Weapon::Revolver, 3));
    failures += expect(
      snapshot.weaponFires[0].fired &&
        snapshot.weaponFires[0].weapon == lg::Weapon::Revolver,
      "Sniper cooldown must not block the Revolver"
    );
    failures += expect(
      snapshot.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Railgun)] == 9 &&
        snapshot.weaponAmmo.spawnAmmo[lg::weaponIndex(lg::Weapon::Revolver)] == 11,
      "Sniper and Revolver spawn ammo must remain separate"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);

    snapshot = sendAndTick(
      transport,
      server,
      bodyAttackWith(snapshot, lg::Weapon::Railgun, 1)
    );
    failures += expect(
      snapshot.weaponFires[0].fired && snapshot.players[1].health == 20,
      "setup rail shot should fire before testing crazy switch rules"
    );

    snapshot =
      sendAndTick(transport, server, attackWith(lg::Weapon::RocketLauncher, 2));
    failures += expect(
      snapshot.selectedWeapons[0] == lg::Weapon::RocketLauncher &&
        snapshot.weaponFires[0].weapon == lg::Weapon::RocketLauncher &&
        snapshot.weaponFires[0].fired,
      "crazy mode should allow switching and firing during prior cooldown"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::CommandPacket modeRequest;
    modeRequest.command.sequence = 1;
    modeRequest.requestMovementTuning = true;
    modeRequest.weaponSwitchingMode = lg::WeaponSwitchingMode::Cpma;
    transport.sendCommand(modeRequest);
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.weaponSwitchingMode == lg::WeaponSwitchingMode::Cpma,
      "runtime tuning should update and replicate weapon switching mode"
    );

    snapshot =
      sendAndTick(
        transport,
        server,
        bodyAttackWith(snapshot, lg::Weapon::Railgun, 2)
      );
    failures += expect(
      snapshot.weaponFires[0].fired && snapshot.players[1].health == 20,
      "setup rail shot should fire before testing CPMA switch lockout"
    );

    snapshot =
      sendAndTick(transport, server, attackWith(lg::Weapon::RocketLauncher, 3));
    failures += expect(
      snapshot.selectedWeapons[0] == lg::Weapon::Railgun &&
        snapshot.players[1].health == 20,
      "CPMA mode should block switching away until fired weapon cooldown ends"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    server.setWeaponSwitchingMode(lg::WeaponSwitchingMode::Ql);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);

    lg::UserCommand rail =
      bodyAttackWith(snapshot, lg::Weapon::Railgun, 1);
    snapshot = sendAndTick(transport, server, rail);
    failures += expect(
      snapshot.selectedWeapons[0] == lg::Weapon::Railgun &&
        !snapshot.weaponFires[0].fired &&
        snapshot.players[1].health == 100,
      "QL mode should block firing during weapon pullout"
    );

    for (int tick = 0; tick < 20; ++tick) {
      rail.sequence = static_cast<std::uint32_t>(tick + 2);
      snapshot = sendAndTick(transport, server, rail);
    }

    failures += expect(
      snapshot.weaponFires[0].fired && snapshot.players[1].health == 20,
      "QL mode should allow firing after pullout finishes"
    );
  }

  return failures == 0 ? 0 : 1;
}
