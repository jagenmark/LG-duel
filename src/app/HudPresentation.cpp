#include "app/HudPresentation.hpp"

#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace lg {
namespace {

std::size_t teamScoreIndex(Team team) {
  return team == Team::Blue ? 1U : 0U;
}

[[nodiscard]] bool finitePosition(Vec3 position) {
  return std::isfinite(position.x) &&
    std::isfinite(position.y) &&
    std::isfinite(position.z);
}

[[nodiscard]] Vec3 baseCenter(const ArenaMcGuffinBase& base) {
  return (base.min + base.max) * 0.5F;
}

[[nodiscard]] const ArenaMcGuffinBase* baseOwnedBy(
  const ArenaMcGuffinLayout& layout,
  Team redBaseOwner,
  Team blueBaseOwner,
  Team team
) {
  if (layout.hasRedBase && redBaseOwner == team) {
    return &layout.redBase;
  }
  if (layout.hasBlueBase && blueBaseOwner == team) {
    return &layout.blueBase;
  }
  return nullptr;
}

[[nodiscard]] const ArenaMcGuffinBase* nearestBase(
  const ArenaMcGuffinLayout& layout,
  Vec3 source
) {
  const ArenaMcGuffinBase* nearest = nullptr;
  float nearestDistance = std::numeric_limits<float>::infinity();
  const auto consider = [&nearest, &nearestDistance, source](
    const ArenaMcGuffinBase& base,
    bool available
  ) {
    if (!available) {
      return;
    }
    const float distance = length(baseCenter(base) - source);
    if (!std::isfinite(distance) || distance >= nearestDistance) {
      return;
    }
    nearest = &base;
    nearestDistance = distance;
  };
  // Red wins exact ties so deciding-round guidance stays deterministic.
  consider(layout.redBase, layout.hasRedBase);
  consider(layout.blueBase, layout.hasBlueBase);
  return nearest;
}

[[nodiscard]] McGuffinNavigationTarget makeNavigationTarget(
  McGuffinNavigationKind kind,
  Vec3 worldPosition
) {
  if (!finitePosition(worldPosition)) {
    return {};
  }
  return {true, kind, worldPosition};
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
    event.headshot,
  };
  if (hasSeen(key)) {
    return;
  }
  remember(key);

  if (
    config.mode == DamageNumbersMode::PerInstance ||
    config.mode == DamageNumbersMode::PerInstanceAndTally
  ) {
    // Damage numbers are a presentation effect, but their anchor still comes
    // from authoritative hit feedback so client prediction never invents hits.
    entries_.push_back({
      event.damageApplied,
      event.headshot,
      event.targetPlayerIndex,
      0.0F,
      nextSequence_++,
      event.hasTargetPosition,
      event.targetPosition,
    });
  }

  if (
    config.mode != DamageNumbersMode::PerInstanceAndTally &&
    config.mode != DamageNumbersMode::TallyOnly
  ) {
    return;
  }

  DamageNumberTally& tally = tallies_[event.targetPlayerIndex];
  const float burstWindow = std::max(0.0F, config.burstWindowSeconds);
  if (tally.active && tally.secondsSinceLastHit <= burstWindow) {
    tally.damage += event.damageApplied;
    tally.headshot = tally.headshot || event.headshot;
  } else {
    tally.active = true;
    tally.damage = event.damageApplied;
    tally.headshot = event.headshot;
    tally.targetPlayerIndex = event.targetPlayerIndex;
  }
  tally.secondsSinceLastHit = 0.0F;
  tally.hasWorldPosition = event.hasTargetPosition;
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
        seen.damageApplied == key.damageApplied &&
        seen.headshot == key.headshot;
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
  return snapshot.gameMode != GameMode::Duel &&
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

  if (snapshot.gameMode == GameMode::McGuffin) {
    const std::size_t localTeamIndex =
      teamScoreIndex(snapshot.teams[localPlayerIndex]);
    const std::size_t opposingTeamIndex = 1U - localTeamIndex;
    return "MCGUFFIN " +
      std::to_string(snapshot.mcguffinScores[localTeamIndex]) + '-' +
      std::to_string(snapshot.mcguffinScores[opposingTeamIndex]) + " / " +
      std::to_string(snapshot.mcguffinConfig.scoreLimit) + "  ROUNDS " +
      std::to_string(snapshot.mcguffinRoundsWon[localTeamIndex]) + '-' +
      std::to_string(snapshot.mcguffinRoundsWon[opposingTeamIndex]);
  }

  return "SCORE " + std::to_string(snapshot.scores[localPlayerIndex]) +
    " / " + std::to_string(snapshot.matchRules.roundLimit);
}

std::string matchTimeLine(const ServerSnapshot& snapshot) {
  if (snapshot.gameMode == GameMode::McGuffin) {
    return {};
  }
  if (snapshot.overtime) {
    return "TIME OVERTIME";
  }
  if (snapshot.matchRules.timeLimitMinutes == 0) {
    return {};
  }

  const std::uint32_t limitTicks =
    static_cast<std::uint32_t>(snapshot.matchRules.timeLimitMinutes) *
    60U * 125U;
  const std::uint32_t remainingTicks =
    snapshot.liveTicksElapsed < limitTicks
    ? limitTicks - snapshot.liveTicksElapsed
    : 0U;
  const std::uint32_t remainingSeconds = remainingTicks / 125U;
  return "TIME " + std::to_string(remainingSeconds / 60U) + ':' +
    (remainingSeconds % 60U < 10U ? "0" : "") +
    std::to_string(remainingSeconds % 60U);
}

McGuffinNavigationTarget selectMcGuffinNavigationTarget(
  const ServerSnapshot& snapshot,
  const Arena& arena,
  std::size_t subjectPlayerIndex
) {
  if (
    snapshot.gameMode != GameMode::McGuffin ||
    snapshot.matchPhase != MatchPhase::Live ||
    subjectPlayerIndex >= kDuelPlayerCount ||
    snapshot.players[subjectPlayerIndex].health <= 0
  ) {
    return {};
  }

  const McGuffinSnapshot& objective = snapshot.mcguffin;
  switch (objective.state) {
  case McGuffinState::NeutralSpawn:
    if (objective.stateTicks < snapshot.mcguffinConfig.initialSpawnTicks) {
      return {};
    }
    return makeNavigationTarget(
      McGuffinNavigationKind::Objective,
      objective.position
    );
  case McGuffinState::Dropped:
    return makeNavigationTarget(
      McGuffinNavigationKind::RecoverObjective,
      objective.position
    );
  case McGuffinState::Carried:
    if (
      objective.carrierIndex == subjectPlayerIndex &&
      isPlayableTeam(objective.carrierTeam)
    ) {
      const Vec3 source = snapshot.players[subjectPlayerIndex].position;
      const ArenaMcGuffinBase* base = baseOwnedBy(
        arena.mcguffin,
        snapshot.mcguffinRedBaseOwner,
        snapshot.mcguffinBlueBaseOwner,
        objective.carrierTeam
      );
      if (base == nullptr) {
        base = nearestBase(arena.mcguffin, source);
      }
      return base == nullptr
        ? McGuffinNavigationTarget{}
        : makeNavigationTarget(
            McGuffinNavigationKind::InstallBase,
            baseCenter(*base)
          );
    }
    return makeNavigationTarget(
      McGuffinNavigationKind::FollowCarrier,
      objective.position
    );
  case McGuffinState::InstalledRed:
  case McGuffinState::InstalledBlue: {
    const Team installedTeam = objective.state == McGuffinState::InstalledRed
      ? Team::Red
      : Team::Blue;
    const McGuffinNavigationKind kind =
      snapshot.teams[subjectPlayerIndex] == installedTeam
        ? McGuffinNavigationKind::DefendBase
        : McGuffinNavigationKind::AttackBase;
    Vec3 position = objective.position;
    if (!finitePosition(position)) {
      const ArenaMcGuffinBase* base = installedTeam == Team::Red
        ? (arena.mcguffin.hasRedBase ? &arena.mcguffin.redBase : nullptr)
        : (arena.mcguffin.hasBlueBase ? &arena.mcguffin.blueBase : nullptr);
      if (base == nullptr) {
        return {};
      }
      position = baseCenter(*base);
    }
    return makeNavigationTarget(kind, position);
  }
  }
  return {};
}

bool localPlayerWonResult(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  bool matchResult
) {
  if (snapshot.gameMode != GameMode::Duel) {
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
  Weapon bestWeapon = Weapon::LightningGun;
  std::uint32_t bestDamage = 0;
  std::uint32_t totalDamage = 0;
  for (Weapon weapon : kWeaponSlotOrder) {
    const WeaponCombatStats& weaponStats = stats.weapons[weaponIndex(weapon)];
    totalDamage += weaponStats.damageDealt;
    if (weaponStats.damageDealt > bestDamage) {
      bestWeapon = weapon;
      bestDamage = weaponStats.damageDealt;
    }
  }

  const WeaponCombatStats& bestStats = stats.weapons[weaponIndex(bestWeapon)];
  const std::uint32_t accuracyPercent = bestStats.attempts == 0
    ? 0
    : (
        static_cast<std::uint32_t>(bestStats.hits) * 100U +
        (static_cast<std::uint32_t>(bestStats.attempts) / 2U)
      ) / static_cast<std::uint32_t>(bestStats.attempts);
  return std::string(label) +
    " " + std::string(weaponShortName(bestWeapon)) + " " +
    std::to_string(accuracyPercent) +
    "%  DMG " + std::to_string(totalDamage);
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
