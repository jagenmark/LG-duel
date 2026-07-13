#include "sim/McGuffinRules.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  const lg::McGuffinConfig config = {
    .scoreLimit = 3,
    .pointsPerSecond = 3,
    .carryPointsPerSecond = 1,
    .carryPointLimit = 10,
    .initialSpawnTicks = 0,
    .installationDelayTicks = 2,
    .stealTicks = 1,
    .returnTicks = 3,
    .finalHoldTicks = 3,
    .pickupRadius = 1.0F,
  };

  const std::array<bool, lg::kMaxPlayers> connected = {true, true, true, false, false, false};
  const std::array<bool, lg::kMaxPlayers> ready = {true, true, true, false, false, false};
  const std::array<lg::Team, lg::kMaxPlayers> teams = {
    lg::Team::Red, lg::Team::Blue, lg::Team::Blue, lg::Team::None, lg::Team::None, lg::Team::None,
  };
  failures += expect(lg::hasRequiredMcGuffinPlayers(connected), "two players should satisfy McGuffin roster minimum");
  failures += expect(lg::canStartMcGuffin(connected, ready, teams), "ready teams on both sides should start McGuffin");
  failures += expect(lg::areMcGuffinEnemies(teams, 0, 1) && lg::areMcGuffinTeammates(teams, 1, 2),
                     "McGuffin team helpers should classify enemies and teammates");

  lg::McGuffinObjective objective;
  lg::resetMcGuffin(objective, {10.0F, 0.0F, 0.0F});
  failures += expect(objective.state == lg::McGuffinState::NeutralSpawn && lg::isValidMcGuffinObjective(objective),
                     "reset should make a valid neutral objective at its spawn");
  failures += expect(!lg::tryPickupMcGuffin(objective, config, 0, lg::Team::Red, {11.1F, 0.0F, 0.0F}),
                     "pickup outside radius should be rejected");
  failures += expect(lg::tryPickupMcGuffin(objective, config, 0, lg::Team::Red, {10.5F, 0.0F, 0.0F}),
                     "a playable team can pick up neutral objective in radius");
  failures += expect(objective.state == lg::McGuffinState::Carried && objective.carrierIndex == 0,
                     "pickup should record the authoritative carrier");

  std::array<std::uint16_t, lg::kPlayableTeamCount> scores = {};
  (void)lg::tickMcGuffin(objective, config, true, scores);
  failures += expect(objective.state == lg::McGuffinState::Carried && objective.stateTicks == 1,
                     "installation should wait for its configured fixed ticks");
  (void)lg::tickMcGuffin(objective, config, true, scores);
  failures += expect(objective.state == lg::McGuffinState::InstalledRed && objective.associatedTeam == lg::Team::Red,
                     "carrier may install only after the delay at its own base");

  failures += expect(!lg::tryStealMcGuffin(objective, config, 0, lg::Team::Red, {10.0F, 0.0F, 0.0F}),
                     "the installing team cannot steal its own installed objective");
  failures += expect(lg::tryStealMcGuffin(objective, config, 1, lg::Team::Blue, {10.0F, 0.0F, 0.0F}),
                     "enemy may steal an installed objective");
  failures += expect(lg::throwMcGuffin(
                       objective, 1, {4.0F, 0.0F, 0.0F}, {12.0F, 0.0F, 4.0F}),
                     "the authoritative carrier can throw the objective");
  failures += expect(objective.state == lg::McGuffinState::Dropped &&
                       objective.velocity.x == 12.0F && objective.velocity.z == 4.0F,
                     "throw should preserve its launch velocity for server physics");
  failures += expect(lg::tryPickupMcGuffin(objective, config, 0, lg::Team::Red, {4.0F, 0.0F, 0.0F}),
                     "either team can recover a dropped objective as a carrier");

  lg::resetMcGuffin(objective, {});
  (void)lg::tryPickupMcGuffin(objective, config, 0, lg::Team::Red, {});
  (void)lg::dropMcGuffin(objective, 0, {2.0F, 0.0F, 0.0F});
  (void)lg::tickMcGuffin(objective, config, false, scores);
  (void)lg::tickMcGuffin(objective, config, false, scores);
  (void)lg::tickMcGuffin(objective, config, false, scores);
  failures += expect(objective.state == lg::McGuffinState::NeutralSpawn && objective.position.x == 0.0F,
                     "dropped neutral objective should return to spawn after configured ticks");

  objective.state = lg::McGuffinState::InstalledBlue;
  objective.associatedTeam = lg::Team::Blue;
  objective.position = objective.spawnPosition;
  objective.carrierIndex = lg::kNoMcGuffinCarrier;
  objective.carrierTeam = lg::Team::None;
  objective.stateTicks = 0;
  objective.scoreSubPoints = 0;
  scores = {};
  std::optional<lg::Team> winner;
  for (int tick = 0; tick < 125; ++tick) winner = lg::tickMcGuffin(objective, config, false, scores);
  failures += expect(scores[1] == 3 && winner == lg::Team::Blue,
                     "fixed-tick sub-points should award all points without truncation and report score-limit winner");

  lg::McGuffinConfig invalid = config;
  invalid.pickupRadius = 0.0F;
  failures += expect(!lg::isValidMcGuffinConfig(invalid), "non-positive pickup radius should violate config invariants");
  objective.carrierIndex = 0;
  failures += expect(!lg::isValidMcGuffinObjective(objective), "installed objective cannot retain a carrier");

  return failures == 0 ? 0 : 1;
}
