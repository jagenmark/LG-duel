#include "sim/ClanArenaRules.hpp"

#include <array>
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

} // namespace

int main() {
  int failures = 0;

  const std::array<bool, lg::kMaxPlayers> connected = {
    true, true, true, true, false, false,
  };
  const std::array<bool, lg::kMaxPlayers> ready = {
    true, true, true, true, false, false,
  };
  const std::array<lg::Team, lg::kMaxPlayers> unequalTeams = {
    lg::Team::Red,
    lg::Team::Blue,
    lg::Team::Blue,
    lg::Team::Blue,
    lg::Team::None,
    lg::Team::None,
  };

  failures += expect(
    lg::hasRequiredClanArenaPlayers(connected),
    "Clan Arena should accept a roster of at least two players"
  );
  failures += expect(
    lg::canStartClanArena(connected, ready, unequalTeams),
    "Clan Arena should accept unequal initial teams"
  );

  auto unassignedTeams = unequalTeams;
  unassignedTeams[3] = lg::Team::None;
  failures += expect(
    !lg::canStartClanArena(connected, ready, unassignedTeams),
    "an unassigned connected player should prevent Clan Arena from starting"
  );

  auto oneSidedTeams = unequalTeams;
  oneSidedTeams[0] = lg::Team::Blue;
  failures += expect(
    !lg::canStartClanArena(connected, ready, oneSidedTeams),
    "Clan Arena should require at least one player on each team"
  );

  failures += expect(
    lg::areClanArenaTeammates(unequalTeams, 1, 2),
    "players on the same playable team should be teammates"
  );
  failures += expect(
    lg::areClanArenaEnemies(unequalTeams, 0, 1),
    "players on different playable teams should be enemies"
  );
  failures += expect(
    !lg::areClanArenaTeammates(unequalTeams, 0, 0),
    "self damage should not be classified as friendly fire"
  );

  std::array<bool, lg::kMaxPlayers> alive = {
    false, true, true, false, false, false,
  };
  const auto winner = lg::clanArenaRoundWinner(connected, unequalTeams, alive);
  failures += expect(
    winner.has_value() && *winner == lg::Team::Blue,
    "the last team with living players should win the round"
  );
  alive[0] = true;
  failures += expect(
    !lg::clanArenaRoundWinner(connected, unequalTeams, alive).has_value(),
    "the round should continue while both teams have living players"
  );

  std::array<std::uint16_t, lg::kPlayableTeamCount> scores = {};
  scores[1] = 9;
  lg::awardClanArenaRound(scores, lg::Team::Blue);
  failures += expect(
    scores[1] == 10 && lg::hasWonClanArena(scores, lg::Team::Blue, 10),
    "Clan Arena team scoring should honor the round limit"
  );
  failures += expect(
    lg::clanArenaScoreLeader(scores) == lg::Team::Blue,
    "Clan Arena should identify a unique team-score leader"
  );
  scores[0] = scores[1];
  failures += expect(
    !lg::clanArenaScoreLeader(scores).has_value(),
    "tied Clan Arena team scores should not have a leader"
  );

  return failures == 0 ? 0 : 1;
}
