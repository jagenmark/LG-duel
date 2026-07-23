#pragma once

#include "scenario/ScenarioSchema.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lg::scenario {

inline constexpr int kScenarioEvidenceSchemaVersion = 1;

struct AssertionEvidence {
  std::size_t assertionIndex = 0;
  std::uint32_t run = 0;
  std::uint32_t tick = 0;
  std::string type;
  bool passed = false;
  std::string message;
  dev::JsonValue expected;
  dev::JsonValue actual;
};

struct EventEvidence {
  std::uint32_t run = 0;
  std::uint32_t tick = 0;
  std::uint64_t sequence = 0;
  std::string type;
  std::optional<std::size_t> actor;
  std::optional<std::size_t> target;
  std::optional<std::uint64_t> entityId;
  std::optional<Weapon> weapon;
  std::optional<int> damage;
  std::optional<Vec3> position;
  dev::JsonValue details = dev::JsonValue::objectValue();
};

struct FinalPlayerEvidence {
  std::size_t index = 0;
  bool connected = false;
  Team team = Team::None;
  Vec3 position = {};
  Vec3 velocity = {};
  float viewYawDegrees = 0.0F;
  float viewPitchDegrees = 0.0F;
  int health = 0;
  bool alive = false;
  Weapon selectedWeapon = Weapon::MachineGun;
  WeaponAmmoArray ammo = {};
};

struct FinalProjectileEvidence {
  std::uint64_t entityId = 0;
  std::size_t owner = 0;
  Weapon weapon = Weapon::RocketLauncher;
  Vec3 position = {};
  Vec3 velocity = {};
};

struct FinalStateEvidence {
  std::uint32_t run = 0;
  std::uint32_t tick = 0;
  std::vector<FinalPlayerEvidence> players;
  std::vector<FinalProjectileEvidence> projectiles;
  dev::JsonValue match = dev::JsonValue::objectValue();
};

struct StateHashEvidence {
  std::uint32_t run = 0;
  std::uint32_t tick = 0;
  std::string stateHash;
  std::optional<std::string> eventHash;
};

struct DivergenceEvidence {
  std::uint32_t run = 0;
  std::uint32_t referenceRun = 0;
  std::uint32_t tick = 0;
  std::string expectedHash;
  std::string actualHash;
  std::string message;
  dev::JsonValue expectedState;
  dev::JsonValue actualState;
};

struct RunEvidence {
  std::uint32_t run = 0;
  std::uint32_t ticksExecuted = 0;
  bool passed = false;
  bool expectedFailureObserved = false;
  std::string failure;
  std::string finalStateHash;
  std::string eventStreamHash;
};

struct ScenarioEvidence {
  int schemaVersion = kScenarioEvidenceSchemaVersion;
  std::string runId;
  bool passed = false;
  bool expectedFailure = false;
  std::string summary;
  std::vector<RunEvidence> runs;
  std::vector<AssertionEvidence> assertions;
  std::vector<EventEvidence> events;
  std::vector<FinalStateEvidence> finalStates;
  std::vector<StateHashEvidence> hashes;
  std::optional<DivergenceEvidence> divergence;
};

[[nodiscard]] dev::JsonValue evidenceJson(
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence
);
[[nodiscard]] dev::JsonValue assertionsEvidenceJson(
  const std::vector<AssertionEvidence>& assertions
);
[[nodiscard]] dev::JsonValue eventsEvidenceJson(
  const std::vector<EventEvidence>& events
);
[[nodiscard]] dev::JsonValue finalStatesEvidenceJson(
  const std::vector<FinalStateEvidence>& states
);
[[nodiscard]] dev::JsonValue hashesEvidenceJson(
  const std::vector<StateHashEvidence>& hashes
);
[[nodiscard]] dev::JsonValue divergenceEvidenceJson(
  const std::optional<DivergenceEvidence>& divergence
);
[[nodiscard]] dev::JsonValue summaryEvidenceJson(
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence
);
[[nodiscard]] std::string junitXml(
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence
);

// Writes to a hidden partial directory, then renames it after every file is closed.
// Existing result directories are never replaced.
[[nodiscard]] bool writeEvidenceArtifacts(
  const std::filesystem::path& evidenceRoot,
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence,
  std::filesystem::path& resultDirectory,
  std::string& error
);

} // namespace lg::scenario
