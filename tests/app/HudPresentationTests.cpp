#include "app/HudPresentation.hpp"

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

lg::LocalDamageEvent damageEvent(
  std::uint32_t serverTick,
  std::uint8_t targetPlayerIndex,
  int damageApplied
) {
  return {
    lg::LocalDamageSource::LightningGun,
    serverTick,
    0,
    targetPlayerIndex,
    damageApplied,
  };
}

lg::LocalDamageEvent worldDamageEvent(
  std::uint32_t serverTick,
  std::uint8_t targetPlayerIndex,
  int damageApplied,
  lg::Vec3 targetPosition
) {
  lg::LocalDamageEvent event = damageEvent(
    serverTick,
    targetPlayerIndex,
    damageApplied
  );
  event.hasTargetPosition = true;
  event.targetPosition = targetPosition;
  return event;
}

} // namespace

int main() {
  lg::ServerSnapshot snapshot;
  snapshot.gameMode = lg::GameMode::ClanArena;
  snapshot.matchPhase = lg::MatchPhase::Live;
  snapshot.connectedPlayers = {true, true, true};
  snapshot.teams = {lg::Team::Red, lg::Team::Red, lg::Team::Blue};
  snapshot.teamScores = {4, 2};
  snapshot.roundWinningTeam = lg::Team::Red;
  snapshot.matchWinningTeam = lg::Team::Red;
  snapshot.playerNames[2] = "ENEMY";

  int failures = 0;
  failures += expect(
    lg::MatchRules{}.roundEndTicks == 625,
    "the default round-end phase should last five seconds"
  );
  failures += expect(
    lg::matchPhaseMessageOffsetY(lg::MatchPhase::WaitingForPlayers) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::WaitingForReady) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::Countdown) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::RoundEnd) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::MatchEnd) == -220.0F,
    "all non-live phase messages should share the warmup banner position"
  );
  failures += expect(
    lg::matchPhaseMessageOffsetY(lg::MatchPhase::Live) == 0.0F,
    "live play should not apply a phase-message offset"
  );
  failures += expect(
    lg::hudScoreLine(snapshot, 0) == "SCORE 4-2 / 10",
    "Clan Arena HUD score should use team scores"
  );
  failures += expect(
    lg::localPlayerWonResult(snapshot, 0, false),
    "Clan Arena round result should use the winning team"
  );
  failures += expect(
    lg::localPlayerWonResult(snapshot, 0, true),
    "Clan Arena match result should use the winning team"
  );
  failures += expect(
    lg::opponentPlayerIndex(snapshot, 0) == 2,
    "Clan Arena post-round stats should select an enemy, not a teammate"
  );
  failures += expect(
    lg::playerPresentedAsTeammate(snapshot, 0, 1) &&
      !lg::playerPresentedAsTeammate(snapshot, 0, 2),
    "live Clan Arena player presentation should distinguish teammates from enemies"
  );
  failures += expect(
    lg::playerRoundStatsLine(snapshot, 2) == "ENEMY lg 0%  DMG 0",
    "post-round stats should display the enemy player name"
  );

  {
    lg::ServerSnapshot warmupSnapshot = snapshot;
    warmupSnapshot.matchPhase = lg::MatchPhase::WaitingForReady;
    warmupSnapshot.teams = {lg::Team::None, lg::Team::None, lg::Team::None};
    warmupSnapshot.connectedPlayers = {true, true, false};
    warmupSnapshot.participatingPlayers = {true, true, false};
    warmupSnapshot.players[1].health = 100;
    failures += expect(
      lg::opponentPlayerIndex(warmupSnapshot, 0) == 1,
      "Clan Arena warmup should select teamless opponents for health bars"
    );
    failures += expect(
      !lg::playerPresentedAsTeammate(warmupSnapshot, 0, 1),
      "teamless Clan Arena warmup players should be presented as enemies"
    );
  }

  {
    lg::ServerSnapshot warmupSnapshot = snapshot;
    warmupSnapshot.matchPhase = lg::MatchPhase::WaitingForReady;
    warmupSnapshot.teams = {lg::Team::Red, lg::Team::Red, lg::Team::None};
    warmupSnapshot.connectedPlayers = {true, true, false};
    warmupSnapshot.participatingPlayers = {true, true, false};
    warmupSnapshot.players[1].health = 100;
    failures += expect(
      lg::opponentPlayerIndex(warmupSnapshot, 0) == 1,
      "Clan Arena warmup should select same-team opponents for health bars"
    );
    failures += expect(
      !lg::playerPresentedAsTeammate(warmupSnapshot, 0, 1),
      "same-team Clan Arena warmup players should be presented as enemies"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstance,
      0.4F,
      0.65F,
    };
    for (std::uint32_t tick = 1; tick <= 4; ++tick) {
      damageNumbers.addLocalDamageEvent(damageEvent(tick, 1, 6), config);
    }
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 4U &&
        presentation.entries[0].damage == 6 &&
        presentation.entries[1].damage == 6 &&
        presentation.entries[2].damage == 6 &&
        presentation.entries[3].damage == 6,
      "per-instance mode should retain one entry for each local damage event"
    );
    bool foundTally = false;
    for (const lg::DamageNumberTally& tally : presentation.tallies) {
      foundTally = foundTally || tally.active;
    }
    failures += expect(
      !foundTally,
      "per-instance mode should not create cumulative tallies"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    std::array<int, 4> observedTallies = {};
    for (std::uint32_t tick = 1; tick <= 4; ++tick) {
      damageNumbers.addLocalDamageEvent(damageEvent(tick, 1, 6), config);
      observedTallies[tick - 1U] =
        damageNumbers.presentation().tallies[1].damage;
      damageNumbers.update(0.08F, config);
    }
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 4U &&
        observedTallies == std::array<int, 4>{{6, 12, 18, 24}},
      "combined mode should keep individual entries and update one active tally immediately"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 6), config);
    damageNumbers.update(0.41F, config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 1, 6), config);
    failures += expect(
      damageNumbers.presentation().tallies[1].damage == 6,
      "a hit after the inactivity window should start a fresh tally"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 5), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 2, 7), config);
    damageNumbers.addLocalDamageEvent(damageEvent(3, 1, 5), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.tallies[1].active &&
        presentation.tallies[1].damage == 10 &&
        presentation.tallies[2].active &&
        presentation.tallies[2].damage == 7,
      "damage on separate targets should maintain separate tallies"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 20), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 2, 20), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.tallies[1].active &&
        presentation.tallies[1].damage == 20 &&
        presentation.tallies[2].active &&
        presentation.tallies[2].damage == 20,
      "P2 and P3 hits inside one burst window should create separate tallies"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::TallyOnly,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 20), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 2, 20), config);
    damageNumbers.addLocalDamageEvent(damageEvent(3, 1, 10), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.tallies[1].active &&
        presentation.tallies[1].damage == 30 &&
        presentation.tallies[2].active &&
        presentation.tallies[2].damage == 20,
      "a later P2 hit should extend only P2's tally"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 0), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 0, 15), config);
    damageNumbers.addLocalDamageEvent(
      {
        lg::LocalDamageSource::WeaponFire,
        3,
        1,
        0,
        12,
        false,
      },
      config
    );
    failures += expect(
      damageNumbers.presentation().entries.empty(),
      "zero damage, self damage, and enemy damage should not create local damage numbers"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    const lg::LocalDamageEvent event = damageEvent(1, 1, 6);
    damageNumbers.addLocalDamageEvent(event, config);
    damageNumbers.addLocalDamageEvent(event, config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 1U &&
        presentation.tallies[1].damage == 6,
      "reprocessing the same authoritative event should not double-count"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::Disabled,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 6), config);
    failures += expect(
      damageNumbers.presentation().entries.empty(),
      "mode 0 should produce no damage number presentation entries"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::TallyOnly,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 8), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 1, 12), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.empty() &&
        presentation.tallies[1].active &&
        presentation.tallies[1].damage == 20,
      "mode 3 should update the cumulative tally without individual entries"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::WorldTallyOnly,
      0.4F,
      0.65F,
    };
    lg::LocalDamageEvent first = damageEvent(1, 1, 8);
    first.hasTargetPosition = true;
    first.targetPosition = {10.0F, 1.0F, 0.0F};
    lg::LocalDamageEvent second = damageEvent(2, 1, 12);
    second.hasTargetPosition = true;
    second.targetPosition = {12.0F, 2.0F, 0.0F};
    damageNumbers.addLocalDamageEvent(first, config);
    damageNumbers.addLocalDamageEvent(second, config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.empty() &&
        presentation.tallies[1].active &&
        presentation.tallies[1].damage == 20 &&
        presentation.tallies[1].hasWorldPosition &&
        presentation.tallies[1].worldPosition.x == 12.0F &&
        presentation.tallies[1].worldPosition.y == 2.0F,
      "mode 4 should update the cumulative tally at the latest target position"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::WorldTallyOnly,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(
      worldDamageEvent(1, 1, 20, {10.0F, 1.0F, 0.0F}),
      config
    );
    damageNumbers.addLocalDamageEvent(
      worldDamageEvent(2, 2, 20, {30.0F, 3.0F, 0.0F}),
      config
    );
    damageNumbers.addLocalDamageEvent(
      worldDamageEvent(3, 1, 10, {12.0F, 2.0F, 0.0F}),
      config
    );
    lg::DamageNumberPresentation presentation = damageNumbers.presentation();
    failures += expect(
      presentation.tallies[1].damage == 30 &&
        presentation.tallies[1].worldPosition.x == 12.0F &&
        presentation.tallies[2].damage == 20 &&
        presentation.tallies[2].worldPosition.x == 30.0F,
      "mode 4 should move only the target that was hit again"
    );
    damageNumbers.addLocalDamageEvent(
      worldDamageEvent(4, 2, 5, {35.0F, 3.0F, 0.0F}),
      config
    );
    presentation = damageNumbers.presentation();
    failures += expect(
      presentation.tallies[1].damage == 30 &&
        presentation.tallies[1].worldPosition.x == 12.0F &&
        presentation.tallies[2].damage == 25 &&
        presentation.tallies[2].worldPosition.x == 35.0F,
      "mode 4 should move P3 without moving P2"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::TallyOnly,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 20), config);
    damageNumbers.update(0.2F, config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 2, 20), config);
    damageNumbers.update(0.25F, config);
    lg::DamageNumberPresentation presentation = damageNumbers.presentation();
    failures += expect(
      !presentation.tallies[1].active &&
        presentation.tallies[2].active &&
        presentation.tallies[2].damage == 20,
      "P2 expiry should not reset P3's tally"
    );
    damageNumbers.addLocalDamageEvent(damageEvent(3, 1, 10), config);
    damageNumbers.update(0.2F, config);
    presentation = damageNumbers.presentation();
    failures += expect(
      presentation.tallies[1].active &&
        presentation.tallies[1].damage == 10 &&
        !presentation.tallies[2].active,
      "P3 expiry should not reset P2's new tally"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(10, 1, 20), config);
    damageNumbers.addLocalDamageEvent(damageEvent(10, 2, 20), config);
    damageNumbers.addLocalDamageEvent(damageEvent(10, 1, 20), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 2U &&
        presentation.tallies[1].damage == 20 &&
        presentation.tallies[2].damage == 20,
      "dedupe should include target identity but still ignore duplicate same-target events"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 20), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 0, 10), config);
    damageNumbers.addLocalDamageEvent(damageEvent(3, 255, 10), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 1U &&
        presentation.tallies[1].active &&
        presentation.tallies[1].damage == 20 &&
        !presentation.tallies[0].active,
      "invalid and self targets should not alter another player's tally"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::PerInstanceAndTally,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(damageEvent(1, 1, 9), config);
    damageNumbers.addLocalDamageEvent(damageEvent(2, 1, 13), config);
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 2U &&
        presentation.entries[0].damage == 9 &&
        presentation.entries[1].damage == 13 &&
        presentation.tallies[1].damage == 22,
      "damage-number presentation should use actual event damage values"
    );
  }

  snapshot.gameMode = lg::GameMode::Duel;
  snapshot.scores = {3, 5};
  snapshot.roundWinner = 0;
  snapshot.matchWinner = 1;
  failures += expect(
    lg::hudScoreLine(snapshot, 0) == "SCORE 3 / 10",
    "duel HUD score should omit the lead readout"
  );
  failures += expect(
    lg::localPlayerWonResult(snapshot, 0, false) &&
      !lg::localPlayerWonResult(snapshot, 0, true),
    "duel result behavior should remain unchanged"
  );

  lg::ServerSnapshot botSnapshot;
  botSnapshot.connectedPlayers = {true};
  botSnapshot.participatingPlayers = {true, true};
  failures += expect(
    lg::opponentPlayerIndex(botSnapshot, 0) == 1,
    "HUD opponent selection should use the authoritative player roster"
  );
  botSnapshot.participatingPlayers = {true};
  botSnapshot.botDodgeEnabled = true;
  failures += expect(
    lg::opponentPlayerIndex(botSnapshot, 0) == 0,
    "bot settings alone should not create a HUD opponent"
  );

  return failures == 0 ? 0 : 1;
}
