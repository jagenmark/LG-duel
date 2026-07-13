#include "app/DeathCamera.hpp"

#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::ServerSnapshot teamSnapshot() {
  lg::ServerSnapshot snapshot;
  snapshot.matchPhase = lg::MatchPhase::Live;
  snapshot.gameMode = lg::GameMode::McGuffin;
  snapshot.teams[0] = lg::Team::Red;
  snapshot.teams[1] = lg::Team::Red;
  snapshot.teams[2] = lg::Team::Blue;
  snapshot.teams[3] = lg::Team::Red;
  snapshot.participatingPlayers[0] = true;
  snapshot.participatingPlayers[1] = true;
  snapshot.participatingPlayers[2] = true;
  snapshot.participatingPlayers[3] = true;
  snapshot.players[0].health = 0;
  snapshot.players[1].health = 100;
  snapshot.players[2].health = 100;
  snapshot.players[3].health = 100;
  return snapshot;
}

} // namespace

int main() {
  int failures = 0;
  const lg::DeathCameraConfig config;

  lg::ServerSnapshot shortDeath = teamSnapshot();
  shortDeath.matchRules.deathRespawnTicks = 250;
  shortDeath.respawnTicksRemaining[0] = 125;
  const lg::DeathCameraDecision shortDecision =
    lg::deathCameraDecision(shortDeath, 0, 1.0F, config);
  failures += expect(
    shortDecision.mode == lg::DeathCameraMode::DeathPosition &&
      !shortDecision.teammateIndex.has_value() &&
      shortDecision.respawnSecondsRemaining == 1.0F,
    "a sub-threshold respawn should retain the local death-position camera"
  );

  lg::ServerSnapshot longDeath = teamSnapshot();
  longDeath.matchRules.deathRespawnTicks = 375;
  longDeath.respawnTicksRemaining[0] = 300;
  failures += expect(
    lg::deathCameraDecision(longDeath, 0, 0.49F, config).mode ==
      lg::DeathCameraMode::DeathPosition,
    "a long respawn should retain the death view for the configured hold"
  );
  const lg::DeathCameraDecision spectate =
    lg::deathCameraDecision(longDeath, 0, 0.5F, config);
  failures += expect(
    spectate.mode == lg::DeathCameraMode::Teammate &&
      spectate.teammateIndex == 1 &&
      lg::deathCameraSubjectIndex(spectate, 0) == 1 &&
      lg::presentationSubjectIndex(spectate, 0, false) == 1,
    "a three-second respawn should spectate the first living teammate after the hold"
  );
  failures += expect(
    lg::cycleDeathCameraTeammate(longDeath, 0, 1, 1) == 3 &&
      lg::cycleDeathCameraTeammate(longDeath, 0, 3, 1) == 1 &&
      lg::cycleDeathCameraTeammate(longDeath, 0, 1, -1) == 3,
    "spectator cycling should wrap in both directions and skip enemies"
  );
  failures += expect(
    lg::deathCameraDecision(longDeath, 0, 0.5F, config, 3).teammateIndex == 3,
    "a valid manually selected teammate should remain the camera target"
  );
  const lg::DeathCameraDecision observer =
    lg::spectatorCameraDecision(longDeath, 2);
  longDeath.selectedWeapons[2] = lg::Weapon::Railgun;
  failures += expect(
    observer.mode == lg::DeathCameraMode::Teammate &&
      observer.teammateIndex == 2 && observer.desaturation == 0.0F &&
      lg::cycleSpectatorTarget(longDeath, 2, 1) == 3 &&
      lg::cycleSpectatorTarget(longDeath, 3, 1) == 1,
    "dedicated spectators should follow and cycle every living active player"
  );
  failures += expect(
    lg::presentationSubjectWeapon(
      longDeath,
      observer,
      lg::kNoAssignedPlayer,
      true,
      lg::Weapon::MachineGun
    ) == lg::Weapon::Railgun,
    "followed-player switch and ready presentation should use the target's selected weapon"
  );
  failures += expect(
    lg::presentationSubjectIndex(observer, lg::kNoAssignedPlayer, true) == 2 &&
      !lg::presentationSubjectIndex(
        [] {
          lg::DeathCameraDecision decision;
          decision.mode = lg::DeathCameraMode::DeathPosition;
          return decision;
        }(),
        lg::kNoAssignedPlayer,
        true
      ).has_value(),
    "HUD and camera presentation should use a nonzero followed body and no body for an untargeted observer"
  );

  longDeath.players[1].health = 0;
  longDeath.players[3].health = 0;
  failures += expect(
    lg::deathCameraDecision(longDeath, 0, 1.0F, config).mode ==
      lg::DeathCameraMode::DeathPosition,
    "the death camera should remain local when no teammate is alive"
  );

  lg::ServerSnapshot elimination = teamSnapshot();
  elimination.gameMode = lg::GameMode::ClanArena;
  failures += expect(
    lg::deathCameraDecision(elimination, 0, 0.5F, config).mode ==
      lg::DeathCameraMode::Teammate,
    "live elimination deaths should use teammate spectating after the hold"
  );

  elimination.players[0].health = 100;
  const lg::DeathCameraDecision alive =
    lg::deathCameraDecision(elimination, 0, 4.0F, config);
  failures += expect(
    alive.mode == lg::DeathCameraMode::Alive && alive.desaturation == 0.0F,
    "living players should never receive death-camera presentation"
  );
  return failures == 0 ? 0 : 1;
}
