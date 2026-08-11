#include "app/HudPresentation.hpp"

#include <cmath>
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

std::size_t activeDirectionalIndicatorCount(
  const lg::DirectionalDamagePresentation& presentation
) {
  std::size_t count = 0;
  for (const lg::DirectionalDamageIndicator& indicator : presentation.indicators) {
    if (indicator.active) {
      ++count;
    }
  }
  return count;
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
  {
    lg::ServerSnapshot timedSnapshot = snapshot;
    timedSnapshot.gameMode = lg::GameMode::Duel;
    timedSnapshot.matchRules.timeLimitMinutes = 2;
    timedSnapshot.liveTicksElapsed = 61U * 125U;
    failures += expect(
      lg::matchTimeLine(timedSnapshot) == "TIME 0:59",
      "timed match HUD should show the remaining clock"
    );
    timedSnapshot.overtime = true;
    failures += expect(
      lg::matchTimeLine(timedSnapshot) == "TIME OVERTIME",
      "overtime HUD should replace the expired clock"
    );
    timedSnapshot.overtime = false;
    timedSnapshot.matchRules.timeLimitMinutes = 0;
    failures += expect(
      lg::matchTimeLine(timedSnapshot).empty(),
      "untimed match HUD should omit the clock"
    );
  }
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
    constexpr float kPi = 3.14159265359F;
    constexpr float kHalfPi = kPi * 0.5F;
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    config.maxOpacity = 1.0F;
    directionalDamage.addIncomingDamageEvent({1, 0.0F, 1.0F, true, false}, config);
    directionalDamage.addIncomingDamageEvent({2, -kHalfPi, 1.0F, true, false}, config);
    directionalDamage.addIncomingDamageEvent({3, kPi, 1.0F, true, false}, config);
    directionalDamage.addIncomingDamageEvent({4, kHalfPi, 1.0F, true, false}, config);
    const lg::DirectionalDamagePresentation presentation =
      directionalDamage.presentation(0.0F, config);
    failures += expect(
      activeDirectionalIndicatorCount(presentation) == 4U &&
        std::fabs(presentation.indicators[0].relativeYawRadians) < 0.001F &&
        std::fabs(presentation.indicators[1].relativeYawRadians + kHalfPi) < 0.001F &&
        std::fabs(std::fabs(presentation.indicators[2].relativeYawRadians) - kPi) < 0.001F &&
        std::fabs(presentation.indicators[3].relativeYawRadians - kHalfPi) < 0.001F,
      "directional damage should retain front, right, rear, and left bearings"
    );
  }

  {
    constexpr float kHalfPi = 1.57079632679F;
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    directionalDamage.addIncomingDamageEvent({1, kHalfPi, 1.0F, true, false}, config);
    const lg::DirectionalDamagePresentation facingSource =
      directionalDamage.presentation(kHalfPi, config);
    const lg::DirectionalDamagePresentation facingEast =
      directionalDamage.presentation(0.0F, config);
    failures += expect(
      std::fabs(facingSource.indicators[0].relativeYawRadians) < 0.001F &&
        std::fabs(facingEast.indicators[0].relativeYawRadians - kHalfPi) < 0.001F,
      "directional damage should recalculate a world bearing as the camera turns"
    );
  }

  {
    constexpr float kPi = 3.14159265359F;
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    config.mergeAngleRadians = 0.10F;
    directionalDamage.addIncomingDamageEvent(
      {1, -kPi + 0.02F, 1.0F, true, false}, config
    );
    directionalDamage.addIncomingDamageEvent(
      {2, kPi - 0.02F, 1.0F, true, false}, config
    );
    const lg::DirectionalDamagePresentation presentation =
      directionalDamage.presentation(kPi - 0.01F, config);
    failures += expect(
      activeDirectionalIndicatorCount(presentation) == 1U &&
        std::fabs(presentation.indicators[0].relativeYawRadians) < 0.05F,
      "directional damage should merge and wrap across minus pi and pi"
    );
  }

  {
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    config.durationSeconds = 1.0F;
    config.maxOpacity = 1.0F;
    directionalDamage.addIncomingDamageEvent({1, 0.0F, 1.0F, true, false}, config);
    directionalDamage.update(0.5F, config);
    const lg::DirectionalDamagePresentation faded =
      directionalDamage.presentation(0.0F, config);
    directionalDamage.update(0.5F, config);
    const lg::DirectionalDamagePresentation expired =
      directionalDamage.presentation(0.0F, config);
    failures += expect(
      std::fabs(faded.indicators[0].opacity - 0.5F) < 0.001F &&
        activeDirectionalIndicatorCount(expired) == 0U,
      "directional damage should fade and expire at its configured duration"
    );
  }

  {
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    config.durationSeconds = 1.0F;
    config.maxOpacity = 1.0F;
    const lg::IncomingDirectionalDamageEvent event = {17, 0.0F, 1.0F, true, false};
    directionalDamage.addIncomingDamageEvent(event, config);
    directionalDamage.update(0.4F, config);
    const float opacityBefore =
      directionalDamage.presentation(0.0F, config).indicators[0].opacity;
    directionalDamage.addIncomingDamageEvent(event, config);
    const float opacityAfter =
      directionalDamage.presentation(0.0F, config).indicators[0].opacity;
    failures += expect(
      std::fabs(opacityBefore - opacityAfter) < 0.001F,
      "a duplicate directional-damage sequence should not restart its fade"
    );
  }

  {
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    config.mergeAngleRadians = 0.20F;
    directionalDamage.addIncomingDamageEvent({1, 0.05F, 0.6F, true, false}, config);
    directionalDamage.update(0.3F, config);
    directionalDamage.addIncomingDamageEvent({2, 0.15F, 0.9F, true, false}, config);
    const lg::DirectionalDamagePresentation merged =
      directionalDamage.presentation(0.0F, config);
    directionalDamage.addIncomingDamageEvent({3, 2.0F, 1.0F, true, false}, config);
    const lg::DirectionalDamagePresentation distinct =
      directionalDamage.presentation(0.0F, config);
    failures += expect(
      activeDirectionalIndicatorCount(merged) == 1U &&
        merged.indicators[0].opacity > 0.80F &&
        activeDirectionalIndicatorCount(distinct) == 2U,
      "similar directions should merge while distinct directions stay separate"
    );
  }

  {
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    directionalDamage.addIncomingDamageEvent(
      {1, 0.0F, 5.0F / 255.0F, true, false}, config
    );
    const lg::DirectionalDamagePresentation lowDamage =
      directionalDamage.presentation(0.0F, config);
    failures += expect(
      lowDamage.indicators[0].active && lowDamage.indicators[0].opacity >= 0.25F,
      "minimum machine-gun damage should remain visible at default opacity"
    );
  }

  {
    lg::DirectionalDamageState directionalDamage;
    lg::DirectionalDamageHudConfig config;
    directionalDamage.addIncomingDamageEvent({1, 0.0F, 1.0F, false, false}, config);
    directionalDamage.addIncomingDamageEvent({2, 0.0F, 1.0F, false, true}, config);
    const lg::DirectionalDamagePresentation presentation =
      directionalDamage.presentation(0.0F, config, false);
    failures += expect(
      activeDirectionalIndicatorCount(presentation) == 2U &&
        !presentation.enabled &&
        !presentation.indicators[0].directionValid &&
        !presentation.indicators[0].selfDamage &&
        !presentation.indicators[1].directionValid &&
        presentation.indicators[1].selfDamage,
      "unknown and self damage should use neutral directionless HUD cues"
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
      lg::DamageNumbersMode::PerInstance,
      0.4F,
      0.65F,
    };
    damageNumbers.addLocalDamageEvent(
      worldDamageEvent(1, 1, 6, {10.0F, 2.0F, 1.0F}),
      config
    );
    const lg::DamageNumberPresentation presentation =
      damageNumbers.presentation();
    failures += expect(
      presentation.entries.size() == 1U &&
        presentation.entries[0].hasWorldPosition &&
        presentation.entries[0].worldPosition.x == 10.0F &&
        presentation.entries[0].worldPosition.y == 2.0F,
      "per-instance mode should carry the authoritative world-space damage anchor"
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
      lg::DamageNumbersMode::TallyOnly,
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
      "tally-only mode should update the cumulative tally at the latest target position"
    );
  }

  {
    lg::DamageNumberState damageNumbers;
    lg::DamageNumbersConfig config{
      lg::DamageNumbersMode::TallyOnly,
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
      "tally-only mode should move only the target that was hit again"
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
      "tally-only mode should move P3 without moving P2"
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

  snapshot.gameMode = lg::GameMode::McGuffin;
  snapshot.teams[0] = lg::Team::Red;
  snapshot.mcguffinScores = {64, 51};
  snapshot.mcguffinRoundsWon = {1, 0};
  failures += expect(
    lg::hudScoreLine(snapshot, 0) == "MCGUFFIN 64-51 / 100  ROUNDS 1-0",
    "McGuffin HUD should show objective points, limit, and match rounds"
  );
  snapshot.roundWinningTeam = lg::Team::Red;
  failures += expect(lg::localPlayerWonResult(snapshot, 0, false),
    "McGuffin result should use team authority");

  {
    lg::Arena navigationArena;
    navigationArena.mcguffin.hasRedBase = true;
    navigationArena.mcguffin.redBase.min = {-10.0F, -2.0F, 0.0F};
    navigationArena.mcguffin.redBase.max = {-8.0F, 2.0F, 2.0F};
    navigationArena.mcguffin.hasBlueBase = true;
    navigationArena.mcguffin.blueBase.min = {8.0F, -2.0F, 0.0F};
    navigationArena.mcguffin.blueBase.max = {10.0F, 2.0F, 2.0F};

    lg::ServerSnapshot navigationSnapshot;
    navigationSnapshot.gameMode = lg::GameMode::McGuffin;
    navigationSnapshot.matchPhase = lg::MatchPhase::Live;
    navigationSnapshot.teams = {lg::Team::Red, lg::Team::Blue};
    navigationSnapshot.players[0].health = 100;
    navigationSnapshot.players[1].health = 100;
    navigationSnapshot.players[0].position = {-6.0F, 0.0F, 1.0F};
    navigationSnapshot.players[1].position = {6.0F, 0.0F, 1.0F};
    navigationSnapshot.mcguffinConfig.initialSpawnTicks = 20;
    navigationSnapshot.mcguffinRedBaseOwner = lg::Team::Red;
    navigationSnapshot.mcguffinBlueBaseOwner = lg::Team::Blue;
    navigationSnapshot.mcguffin.position = {0.0F, 0.0F, 1.0F};

    navigationSnapshot.mcguffin.state = lg::McGuffinState::NeutralSpawn;
    navigationSnapshot.mcguffin.stateTicks = 19;
    failures += expect(
      !lg::selectMcGuffinNavigationTarget(
        navigationSnapshot,
        navigationArena,
        0
      ).active,
      "McGuffin navigation should stay quiet before neutral spawn"
    );
    navigationSnapshot.mcguffin.stateTicks = 20;
    lg::McGuffinNavigationTarget target =
      lg::selectMcGuffinNavigationTarget(navigationSnapshot, navigationArena, 0);
    failures += expect(
      target.active &&
        target.kind == lg::McGuffinNavigationKind::Objective &&
        target.worldPosition.x == 0.0F,
      "neutral McGuffin navigation should target the objective after spawn"
    );

    navigationSnapshot.mcguffin.state = lg::McGuffinState::Dropped;
    navigationSnapshot.mcguffin.position = {3.0F, -4.0F, 1.0F};
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      target.active &&
        target.kind == lg::McGuffinNavigationKind::RecoverObjective &&
        target.worldPosition.x == 3.0F && target.worldPosition.y == -4.0F,
      "dropped McGuffin navigation should target recovery"
    );

    navigationSnapshot.mcguffin.state = lg::McGuffinState::Carried;
    navigationSnapshot.mcguffin.carrierTeam = lg::Team::Red;
    navigationSnapshot.mcguffin.carrierIndex = 0;
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      target.active &&
        target.kind == lg::McGuffinNavigationKind::InstallBase &&
        target.worldPosition.x == -9.0F && target.worldPosition.z == 1.0F,
      "the local carrier should navigate to its current owned base"
    );
    navigationSnapshot.mcguffinRedBaseOwner = lg::Team::Blue;
    navigationSnapshot.mcguffinBlueBaseOwner = lg::Team::Red;
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      target.active && target.worldPosition.x == 9.0F,
      "the local carrier should follow a dynamically owned enemy-side base"
    );
    navigationSnapshot.mcguffinRedBaseOwner = lg::Team::None;
    navigationSnapshot.mcguffinBlueBaseOwner = lg::Team::None;
    navigationSnapshot.players[0].position = {0.0F, 0.0F, 1.0F};
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      target.active && target.worldPosition.x == -9.0F,
      "base selection should use the red base for an exact nearest-base tie"
    );
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      1
    );
    failures += expect(
      target.active &&
        target.kind == lg::McGuffinNavigationKind::FollowCarrier &&
        target.worldPosition.x == navigationSnapshot.mcguffin.position.x,
      "other players, including a spectated teammate, should follow the carrier"
    );

    navigationSnapshot.mcguffin.state = lg::McGuffinState::InstalledRed;
    navigationSnapshot.mcguffin.position = {-9.0F, 0.0F, 1.0F};
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      target.active && target.kind == lg::McGuffinNavigationKind::DefendBase,
      "the installed objective should guide its team to defend"
    );
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      1
    );
    failures += expect(
      target.active && target.kind == lg::McGuffinNavigationKind::AttackBase,
      "the installed objective should guide the other team to attack"
    );
    navigationSnapshot.mcguffin.position = {
      std::numeric_limits<float>::quiet_NaN(),
      0.0F,
      1.0F,
    };
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      target.active && target.worldPosition.x == -9.0F,
      "installed navigation should fall back to the authoritative base geometry"
    );

    navigationSnapshot.mcguffin.state = lg::McGuffinState::Dropped;
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      0
    );
    failures += expect(
      !target.active,
      "navigation should suppress a dropped objective with missing position data"
    );
    navigationSnapshot.mcguffin.state = lg::McGuffinState::NeutralSpawn;
    navigationSnapshot.mcguffin.stateTicks = 20;
    target = lg::selectMcGuffinNavigationTarget(
      navigationSnapshot,
      navigationArena,
      lg::kDuelPlayerCount
    );
    failures += expect(
      !target.active,
      "a dedicated spectator without a subject should not receive a body target"
    );
    navigationSnapshot.matchPhase = lg::MatchPhase::Countdown;
    failures += expect(
      !lg::selectMcGuffinNavigationTarget(
        navigationSnapshot,
        navigationArena,
        0
      ).active,
      "McGuffin navigation should clear outside live play"
    );
    navigationSnapshot.matchPhase = lg::MatchPhase::Live;
    navigationSnapshot.players[0].health = 0;
    failures += expect(
      !lg::selectMcGuffinNavigationTarget(
        navigationSnapshot,
        navigationArena,
        0
      ).active,
      "McGuffin navigation should clear when the subject is dead"
    );
    failures += expect(
      lg::mcguffinNavigationLabel(lg::McGuffinNavigationKind::InstallBase) ==
        "INSTALL BASE",
      "McGuffin navigation kinds should expose concise HUD labels"
    );
  }

  return failures == 0 ? 0 : 1;
}
