#include "app/HudPresentation.hpp"

#include <algorithm>
#include <string_view>

namespace lg {
namespace {

std::size_t teamScoreIndex(Team team) {
  return team == Team::Blue ? 1U : 0U;
}

} // namespace

void DamageNumberState::reset() {
  entries_.clear();
  tallies_ = {};
  seenEvents_.clear();
  nextSequence_ = 0;
}

void DamageNumberState::update(
  float deltaSeconds,
  const DamageNumbersConfig& config
) {
  const float clampedDelta = std::max(0.0F, deltaSeconds);
  for (DamageNumberEntry& entry : entries_) {
    entry.ageSeconds += clampedDelta;
  }
  const float duration = std::max(0.0F, config.entryDurationSeconds);
  entries_.erase(
    std::remove_if(
      entries_.begin(),
      entries_.end(),
      [duration](const DamageNumberEntry& entry) {
        return entry.ageSeconds >= duration;
      }
    ),
    entries_.end()
  );

  const float burstWindow = std::max(0.0F, config.burstWindowSeconds);
  for (DamageNumberTally& tally : tallies_) {
    if (!tally.active) {
      continue;
    }
    tally.secondsSinceLastHit += clampedDelta;
    if (tally.secondsSinceLastHit > burstWindow) {
      tally = {};
    }
  }
}

void DamageNumberState::addLocalDamageEvent(
  const LocalDamageEvent& event,
  const DamageNumbersConfig& config
) {
  if (
    config.mode == DamageNumbersMode::Disabled ||
    !event.confirmedLocal ||
    event.damageApplied <= 0 ||
    event.sourcePlayerIndex == event.targetPlayerIndex ||
    event.targetPlayerIndex >= kDuelPlayerCount
  ) {
    return;
  }

  const EventKey key{
    event.source,
    event.serverTick,
    event.sourcePlayerIndex,
    event.targetPlayerIndex,
    event.damageApplied,
  };
  if (hasSeen(key)) {
    return;
  }
  remember(key);

  if (
    config.mode == DamageNumbersMode::PerInstance ||
    config.mode == DamageNumbersMode::PerInstanceAndTally
  ) {
    entries_.push_back({
      event.damageApplied,
      event.targetPlayerIndex,
      0.0F,
      nextSequence_++,
    });
  }

  if (
    config.mode != DamageNumbersMode::PerInstanceAndTally &&
    config.mode != DamageNumbersMode::TallyOnly &&
    config.mode != DamageNumbersMode::WorldTallyOnly
  ) {
    return;
  }

  DamageNumberTally& tally = tallies_[event.targetPlayerIndex];
  const float burstWindow = std::max(0.0F, config.burstWindowSeconds);
  if (tally.active && tally.secondsSinceLastHit <= burstWindow) {
    tally.damage += event.damageApplied;
  } else {
    tally.active = true;
    tally.damage = event.damageApplied;
    tally.targetPlayerIndex = event.targetPlayerIndex;
  }
  tally.secondsSinceLastHit = 0.0F;
  tally.hasWorldPosition =
    config.mode == DamageNumbersMode::WorldTallyOnly && event.hasTargetPosition;
  if (tally.hasWorldPosition) {
    tally.worldPosition = event.targetPosition;
  }
}

DamageNumberPresentation DamageNumberState::presentation() const {
  return {entries_, tallies_};
}

bool DamageNumberState::hasSeen(const EventKey& key) const {
  return std::any_of(
    seenEvents_.begin(),
    seenEvents_.end(),
    [&key](const EventKey& seen) {
      return seen.source == key.source &&
        seen.serverTick == key.serverTick &&
        seen.sourcePlayerIndex == key.sourcePlayerIndex &&
        seen.targetPlayerIndex == key.targetPlayerIndex &&
        seen.damageApplied == key.damageApplied;
    }
  );
}

void DamageNumberState::remember(const EventKey& key) {
  constexpr std::size_t kMaxRememberedEvents = 96;
  seenEvents_.push_back(key);
  if (seenEvents_.size() > kMaxRememberedEvents) {
    seenEvents_.erase(seenEvents_.begin());
  }
}

bool warmupPhase(MatchPhase phase) {
  return phase == MatchPhase::WaitingForPlayers ||
    phase == MatchPhase::WaitingForReady;
}

std::size_t opponentPlayerIndex(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (
      index != localPlayerIndex &&
      (snapshot.connectedPlayers[index] || snapshot.botPlayers[index]) &&
      (
        snapshot.gameMode == GameMode::Duel ||
        warmupPhase(snapshot.matchPhase) ||
        snapshot.teams[index] != snapshot.teams[localPlayerIndex]
      )
    ) {
      return index;
    }
  }
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (
      index != localPlayerIndex &&
      snapshot.participatingPlayers[index] &&
      snapshot.players[index].health > 0 &&
      (
        snapshot.gameMode == GameMode::Duel ||
        warmupPhase(snapshot.matchPhase) ||
        snapshot.teams[index] != snapshot.teams[localPlayerIndex]
      )
    ) {
      return index;
    }
  }
  return localPlayerIndex;
}

bool playerPresentedAsTeammate(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  std::size_t remotePlayerIndex
) {
  return snapshot.gameMode == GameMode::ClanArena &&
    !warmupPhase(snapshot.matchPhase) &&
    localPlayerIndex < kDuelPlayerCount &&
    remotePlayerIndex < kDuelPlayerCount &&
    isPlayableTeam(snapshot.teams[localPlayerIndex]) &&
    snapshot.teams[remotePlayerIndex] == snapshot.teams[localPlayerIndex];
}

std::string hudScoreLine(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  if (snapshot.gameMode == GameMode::ClanArena) {
    const std::size_t localTeamIndex =
      teamScoreIndex(snapshot.teams[localPlayerIndex]);
    const std::size_t opposingTeamIndex = 1U - localTeamIndex;
    return "SCORE " + std::to_string(snapshot.teamScores[localTeamIndex]) +
      '-' + std::to_string(snapshot.teamScores[opposingTeamIndex]) + " / " +
      std::to_string(snapshot.matchRules.roundLimit);
  }

  return "SCORE " + std::to_string(snapshot.scores[localPlayerIndex]) +
    " / " + std::to_string(snapshot.matchRules.roundLimit);
}

bool localPlayerWonResult(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  bool matchResult
) {
  if (snapshot.gameMode == GameMode::ClanArena) {
    const Team winningTeam =
      matchResult ? snapshot.matchWinningTeam : snapshot.roundWinningTeam;
    return isPlayableTeam(winningTeam) &&
      snapshot.teams[localPlayerIndex] == winningTeam;
  }

  const std::uint8_t winner =
    matchResult ? snapshot.matchWinner : snapshot.roundWinner;
  return winner == localPlayerIndex;
}

std::string roundStatsLine(
  std::string_view label,
  const RoundCombatStats& stats
) {
  const std::uint32_t accuracyPercent =
    stats.lightningActiveTicks == 0
    ? 0
    : (
        stats.lightningHitTicks * 100U +
        (stats.lightningActiveTicks / 2U)
      ) / stats.lightningActiveTicks;
  return std::string(label) +
    " LG " + std::to_string(accuracyPercent) +
    "%  DMG " + std::to_string(stats.damageDealt);
}

std::string playerRoundStatsLine(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
) {
  return roundStatsLine(
    snapshot.playerNames[playerIndex],
    snapshot.roundCombatStats[playerIndex]
  );
}

float matchPhaseMessageOffsetY(MatchPhase phase) {
  constexpr float topMessageOffsetY = -220.0F;
  return phase == MatchPhase::Live ? 0.0F : topMessageOffsetY;
}

} // namespace lg
