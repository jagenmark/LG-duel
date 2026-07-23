#pragma once

#include "dev/DevJson.hpp"
#include "shared/Math.hpp"
#include "sim/GameMode.hpp"
#include "sim/UserCommand.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lg::scenario {

inline constexpr int kScenarioSchemaVersion = 1;

enum class ScenarioExecutionMode {
  Headless,
  ClientServer,
};

struct ScenarioExecution {
  ScenarioExecutionMode mode = ScenarioExecutionMode::Headless;
  std::uint32_t maxTicks = 1;
  std::uint32_t repeat = 1;
};

struct ScenarioWorld {
  std::string map;
  GameMode gameMode = GameMode::Duel;
  std::uint32_t seed = 0;
};

struct ScenarioNetwork {
  int latencyMs = 0;
  int jitterMs = 0;
  int packetLossPercent = 0;
  int reorderPercent = 0;
  std::uint32_t seed = 0;
};

struct PlayerInitialState {
  std::size_t index = 0;
  bool connected = true;
  bool bot = false;
  Team team = Team::None;
  Vec3 position = {};
  Vec3 velocity = {};
  float viewYawDegrees = 0.0F;
  float viewPitchDegrees = 0.0F;
  int health = 100;
  bool alive = true;
  Weapon selectedWeapon = Weapon::MachineGun;
  WeaponAmmoArray ammo = {};
};

enum class OneTickEdge {
  Jump,
  Crouch,
  Dash,
  Attack,
  Sneak,
  Zoom,
};

struct TimelineInput {
  float forward = 0.0F;
  float right = 0.0F;
  float up = 0.0F;
  bool jump = false;
  bool crouch = false;
  bool dash = false;
  bool attack = false;
  bool sneak = false;
  bool zoom = false;
  std::optional<Weapon> weapon;
  std::optional<float> yawDegrees;
  std::optional<float> pitchDegrees;
};

struct TimelineEntry {
  std::uint32_t atTick = 0;
  std::size_t player = 0;
  std::uint32_t durationTicks = 1;
  std::vector<OneTickEdge> oneTickEdges;
  TimelineInput input;
};

struct PlayerVectorAssertion {
  std::size_t player = 0;
  Vec3 value = {};
  float tolerance = 0.0F;
};

struct PlayerHealthAssertion {
  std::size_t player = 0;
  int health = 0;
};

struct PlayerAliveAssertion {
  std::size_t player = 0;
  bool alive = false;
};

struct PlayerWeaponAssertion {
  std::size_t player = 0;
  Weapon weapon = Weapon::MachineGun;
};

struct ProjectileAssertion {
  std::optional<std::size_t> owner;
  std::optional<Weapon> weapon;
};

struct EventAssertion {
  std::string type;
  std::optional<std::size_t> actor;
  std::optional<std::size_t> target;
  std::optional<Weapon> weapon;
  std::optional<int> damage;
  std::optional<std::uint32_t> count;
};

struct StateHashAssertion {
  std::string hash;
};

enum class AssertionClassification {
  AuthoritativeDeterministic,
  ClientBounded,
  RendererAttested,
  VisualReview,
};

struct CommandAcknowledgedAssertion {
  std::size_t timelineIndex = 0;
  std::uint32_t maxTicks = 0;
};

struct InputEdgeCountAssertion {
  OneTickEdge edge = OneTickEdge::Attack;
  std::uint32_t count = 0;
};

struct ClientPendingCommandsMaxAssertion {
  std::uint32_t max = 0;
};

struct ClientCorrectionMagnitudeMaxAssertion {
  float max = 0.0F;
};

struct ClientCorrectionCountAssertion {
  std::uint32_t min = 0;
  std::optional<std::uint32_t> max;
};

struct ClientConvergedAssertion {
  std::size_t player = 0;
  float tolerance = 0.0F;
  std::uint32_t withinTicks = 0;
};

struct ClientConnectedAssertion {
  bool expected = true;
};

struct RendererBackendAssertion {
  std::string backend;
};

struct ScreenshotCheckpointAssertion {
  std::string capture;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

enum class AssertionType {
  PlayerPosition,
  PlayerVelocity,
  PlayerHealth,
  PlayerAlive,
  PlayerWeapon,
  ProjectileExists,
  ProjectileRemoved,
  Event,
  StateHash,
  CommandAcknowledged,
  InputEdgeCount,
  ClientPendingCommandsMax,
  ClientCorrectionMagnitudeMax,
  ClientCorrectionCount,
  ClientConverged,
  ClientConnected,
  RendererBackend,
  ScreenshotCheckpoint,
};

using AssertionPayload = std::variant<
  PlayerVectorAssertion,
  PlayerHealthAssertion,
  PlayerAliveAssertion,
  PlayerWeaponAssertion,
  ProjectileAssertion,
  EventAssertion,
  StateHashAssertion,
  CommandAcknowledgedAssertion,
  InputEdgeCountAssertion,
  ClientPendingCommandsMaxAssertion,
  ClientCorrectionMagnitudeMaxAssertion,
  ClientCorrectionCountAssertion,
  ClientConvergedAssertion,
  ClientConnectedAssertion,
  RendererBackendAssertion,
  ScreenshotCheckpointAssertion
>;

struct ScenarioAssertion {
  AssertionType type = AssertionType::StateHash;
  std::optional<AssertionClassification> classification;
  std::optional<std::uint32_t> atTick;
  bool atCompletion = false;
  AssertionPayload payload = StateHashAssertion{};
};

struct CaptureAfterEvent {
  std::string type;
  std::optional<std::size_t> actor;
  std::optional<std::size_t> target;
  std::optional<Weapon> weapon;
};

struct ScenarioCapture {
  std::string name;
  std::optional<std::uint32_t> atServerTick;
  std::optional<CaptureAfterEvent> afterEvent;
  std::uint32_t waitRenderedFrames = 0;
};

struct ExpectedFailure {
  std::string issue;
  std::size_t assertionIndex = 0;
  std::string reason;
};

struct ScenarioDefinition {
  int schemaVersion = kScenarioSchemaVersion;
  std::string name;
  std::string description;
  ScenarioExecution execution;
  ScenarioWorld world;
  std::optional<ScenarioNetwork> network;
  std::vector<PlayerInitialState> players;
  std::vector<TimelineEntry> timeline;
  std::vector<ScenarioAssertion> assertions;
  std::vector<ScenarioCapture> captures;
  std::optional<ExpectedFailure> expectedFailure;
};

struct ScenarioParseResult {
  ScenarioDefinition scenario;
  bool ok = false;
  std::string error;
};

[[nodiscard]] ScenarioParseResult parseScenario(const dev::JsonValue& value);
[[nodiscard]] ScenarioParseResult parseScenarioJson(std::string_view text);
[[nodiscard]] ScenarioParseResult loadScenarioFile(const std::filesystem::path& path);

[[nodiscard]] dev::JsonValue scenarioJson(const ScenarioDefinition& scenario);
[[nodiscard]] std::string_view gameModeName(GameMode mode);
[[nodiscard]] std::string_view teamName(Team team);
[[nodiscard]] std::string_view weaponName(Weapon weapon);
[[nodiscard]] std::string_view assertionTypeName(AssertionType type);
[[nodiscard]] std::string_view assertionClassificationName(
  AssertionClassification classification);

} // namespace lg::scenario
