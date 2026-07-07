#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

namespace lg {

enum class DamageNumbersMode : int {
  Disabled = 0,
  PerInstance = 1,
  PerInstanceAndTally = 2,
  TallyOnly = 3,
};

enum class LocalDamageSource : std::uint8_t {
  LightningGun,
  WeaponFire,
  RocketExplosion,
};

struct LocalDamageEvent {
  LocalDamageSource source = LocalDamageSource::LightningGun;
  std::uint32_t serverTick = 0;
  std::uint8_t sourcePlayerIndex = 255;
  std::uint8_t targetPlayerIndex = 255;
  int damageApplied = 0;
  bool confirmedLocal = true;
  Weapon weapon = Weapon::LightningGun;
  bool headshot = false;
  bool hasTargetPosition = false;
  Vec3 targetPosition = {};
};

struct DamageNumbersConfig {
  DamageNumbersMode mode = DamageNumbersMode::Disabled;
  float burstWindowSeconds = 0.4F;
  float entryDurationSeconds = 0.65F;
};

struct DamageNumberEntry {
  int damage = 0;
  bool headshot = false;
  std::uint8_t targetPlayerIndex = 255;
  float ageSeconds = 0.0F;
  std::uint32_t sequence = 0;
  bool hasWorldPosition = false;
  Vec3 worldPosition = {};
};

struct DamageNumberTally {
  bool active = false;
  int damage = 0;
  bool headshot = false;
  std::uint8_t targetPlayerIndex = 255;
  float secondsSinceLastHit = 0.0F;
  bool hasWorldPosition = false;
  Vec3 worldPosition = {};
};

struct DamageNumberPresentation {
  std::vector<DamageNumberEntry> entries;
  std::array<DamageNumberTally, kDuelPlayerCount> tallies = {};
};

class DamageNumberState {
public:
  void reset();
  void update(float deltaSeconds, const DamageNumbersConfig& config);
  void addLocalDamageEvent(
    const LocalDamageEvent& event,
    const DamageNumbersConfig& config
  );
  [[nodiscard]] DamageNumberPresentation presentation() const;

private:
  struct EventKey {
    LocalDamageSource source = LocalDamageSource::LightningGun;
    std::uint32_t serverTick = 0;
    std::uint8_t sourcePlayerIndex = 255;
    std::uint8_t targetPlayerIndex = 255;
    int damageApplied = 0;
    bool headshot = false;
  };

  [[nodiscard]] bool hasSeen(const EventKey& key) const;
  void remember(const EventKey& key);

  std::vector<DamageNumberEntry> entries_;
  std::array<DamageNumberTally, kDuelPlayerCount> tallies_ = {};
  std::vector<EventKey> seenEvents_;
  std::uint32_t nextSequence_ = 0;
};

[[nodiscard]] std::size_t opponentPlayerIndex(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
);

[[nodiscard]] bool playerPresentedAsTeammate(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  std::size_t remotePlayerIndex
);

[[nodiscard]] std::string hudScoreLine(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
);

[[nodiscard]] bool localPlayerWonResult(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  bool matchResult
);

[[nodiscard]] std::string roundStatsLine(
  std::string_view label,
  const RoundCombatStats& stats
);

[[nodiscard]] std::string playerRoundStatsLine(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
);

[[nodiscard]] float matchPhaseMessageOffsetY(MatchPhase phase);

} // namespace lg
