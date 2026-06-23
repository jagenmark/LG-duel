#include "sim/DuelRules.hpp"

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

  const std::array<bool, lg::kMaxPlayers> onePlayer = {
    true, false, false, false, false, false,
  };
  const std::array<bool, lg::kMaxPlayers> twoPlayers = {
    true, false, true, false, false, false,
  };
  const std::array<bool, lg::kMaxPlayers> threePlayers = {
    true, true, true, false, false, false,
  };

  failures += expect(
    !lg::hasRequiredDuelPlayers(onePlayer),
    "duel should not accept a one-player roster"
  );
  failures += expect(
    lg::hasRequiredDuelPlayers(twoPlayers),
    "duel should accept exactly two connected players"
  );
  failures += expect(
    !lg::hasRequiredDuelPlayers(threePlayers),
    "duel should reject more than two connected players"
  );

  std::array<bool, lg::kMaxPlayers> readyPlayers = {};
  readyPlayers[0] = true;
  failures += expect(
    !lg::canStartDuel(twoPlayers, readyPlayers),
    "duel should wait until both connected players are ready"
  );
  readyPlayers[2] = true;
  failures += expect(
    lg::canStartDuel(twoPlayers, readyPlayers),
    "duel should start when exactly two connected players are ready"
  );
  failures += expect(
    !lg::canStartDuel(threePlayers, {true, true, true, false, false, false}),
    "ready state should not bypass the exact duel roster requirement"
  );

  failures += expect(
    lg::areDuelOpponents(0, 2),
    "distinct duel players should be opponents"
  );
  failures += expect(
    !lg::areDuelOpponents(0, 0),
    "a duel player should not be their own opponent"
  );

  const auto winner = lg::duelRoundWinner(twoPlayers, 0);
  failures += expect(
    winner.has_value() && *winner == 2,
    "eliminating one duel player should make the other player the round winner"
  );
  failures += expect(
    !lg::duelRoundWinner(onePlayer, 0).has_value(),
    "an incomplete duel roster should not produce a round winner"
  );

  std::array<std::uint16_t, lg::kMaxPlayers> scores = {};
  scores[2] = 9;
  lg::awardDuelRound(scores, 2);
  failures += expect(
    scores[2] == 10,
    "awarding a duel round should increment its winner"
  );
  failures += expect(
    lg::hasWonDuel(scores, 2, 10),
    "a player reaching the round limit should win the duel"
  );
  failures += expect(
    !lg::hasWonDuel(scores, 0, 10),
    "a player below the round limit should not win the duel"
  );

  scores = {};
  scores[0] = 3;
  scores[2] = 2;
  const auto leader = lg::duelScoreLeader(scores, twoPlayers);
  failures += expect(
    leader.has_value() && *leader == 0,
    "duel score leadership should consider the connected roster"
  );
  scores[2] = 3;
  failures += expect(
    !lg::duelScoreLeader(scores, twoPlayers).has_value(),
    "a tied duel should not have a unique score leader"
  );

  return failures == 0 ? 0 : 1;
}
