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

enum class McGuffinNavigationKind : std::uint8_t {
  None = 0,
  Objective,
  RecoverObjective,
  FollowCarrier,
  InstallBase,
  DefendBase,
  AttackBase,
};

struct McGuffinNavigationTarget {
  bool active = false;
  McGuffinNavigationKind kind = McGuffinNavigationKind::None;
  Vec3 worldPosition = {};
};

[[nodiscard]] inline std::string_view mcguffinNavigationLabel(
  McGuffinNavigationKind kind
) {
  switch (kind) {
  case McGuffinNavigationKind::Objective:
    return "OBJECTIVE";
  case McGuffinNavigationKind::RecoverObjective:
    return "RECOVER";
  case McGuffinNavigationKind::FollowCarrier:
    return "CARRIER";
  case McGuffinNavigationKind::InstallBase:
    return "INSTALL BASE";
  case McGuffinNavigationKind::DefendBase:
    return "DEFEND BASE";
  case McGuffinNavigationKind::AttackBase:
    return "ATTACK BASE";
  case McGuffinNavigationKind::None:
    break;
  }
  return {};
}

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

// Incoming damage is kept separate from simulation state. The event stream
// supplies an absolute world bearing; this state turns it into a short-lived
// camera-relative HUD signal.
struct IncomingDirectionalDamageEvent {
  std::uint32_t sequence = 0;
  float sourceBearingRadians = 0.0F;
  float presentationStrength = 1.0F;
  bool directionValid = true;
  bool selfDamage = false;
};

struct DirectionalDamageHudConfig {
  float durationSeconds = 0.8F;
  float maxOpacity = 0.85F;
  float distancePixels = 112.0F;
  float scale = 1.0F;
  float mergeAngleRadians = 0.45F;
};

struct DirectionalDamageIndicator {
  bool active = false;
  std::uint32_t sequence = 0;
  float relativeYawRadians = 0.0F;
  float opacity = 0.0F;
  float strength = 0.0F;
  bool directionValid = false;
  bool selfDamage = false;
};

struct DirectionalDamagePresentation {
  bool enabled = true;
  float distancePixels = 112.0F;
  float scale = 1.0F;
  std::array<DirectionalDamageIndicator, 4> indicators = {};
};

[[nodiscard]] float wrapSignedAngleRadians(float angleRadians);

class DirectionalDamageState {
public:
  void reset();
  void update(float deltaSeconds, const DirectionalDamageHudConfig& config);
  void addIncomingDamageEvent(
    const IncomingDirectionalDamageEvent& event,
    const DirectionalDamageHudConfig& config
  );
  [[nodiscard]] DirectionalDamagePresentation presentation(
    float cameraYawRadians,
    const DirectionalDamageHudConfig& config,
    bool enabled = true
  ) const;
  [[nodiscard]] bool hasSeenSequence(std::uint32_t sequence) const;

private:
  struct StoredIndicator {
    bool active = false;
    std::uint32_t sequence = 0;
    float sourceBearingRadians = 0.0F;
    float strength = 0.0F;
    float ageSeconds = 0.0F;
    bool directionValid = false;
    bool selfDamage = false;
  };

  [[nodiscard]] bool hasSeen(std::uint32_t sequence) const;
  void remember(std::uint32_t sequence);

  std::array<StoredIndicator, 4> indicators_ = {};
  std::vector<std::uint32_t> seenSequences_;
};

struct OrderedDirectionalDamageEvents {
  std::array<IncomingDirectionalDamageEvent, kDamageTakenEventWindow> events = {};
  std::size_t count = 0;
};

[[nodiscard]] OrderedDirectionalDamageEvents
orderedUnseenDirectionalDamageEvents(
  const DamageTakenEventRing& events,
  const DirectionalDamageState& state
);

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

[[nodiscard]] std::string matchTimeLine(const ServerSnapshot& snapshot);

[[nodiscard]] McGuffinNavigationTarget selectMcGuffinNavigationTarget(
  const ServerSnapshot& snapshot,
  const Arena& arena,
  std::size_t subjectPlayerIndex
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
