#include "scenario/LiveScenarioSession.hpp"

#include "dev/DevJson.hpp"
#include "server/ServerGame.hpp"
#include "shared/Sequence.hpp"
#include "sim/Arena.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>
#include <utility>

namespace lg::scenario {
namespace {

constexpr std::uintmax_t kMaxFinishRequestBytes = 64U * 1024U;
constexpr std::size_t kMaxCheckpointEvents = 256U;

dev::JsonValue vectorJson(Vec3 value) {
  return dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(value.x),
    dev::JsonValue::numberValue(value.y),
    dev::JsonValue::numberValue(value.z),
  });
}

dev::JsonValue eventJson(const EventEvidence& event) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["tick"] = dev::JsonValue::numberValue(event.tick);
  value.object["sequence"] = dev::JsonValue::numberValue(event.sequence);
  value.object["type"] = dev::JsonValue::stringValue(event.type);
  if (event.actor.has_value()) {
    value.object["actor"] = dev::JsonValue::numberValue(*event.actor);
  }
  if (event.target.has_value()) {
    value.object["target"] = dev::JsonValue::numberValue(*event.target);
  }
  if (event.entityId.has_value()) {
    value.object["entity_id"] = dev::JsonValue::numberValue(*event.entityId);
  }
  if (event.weapon.has_value()) {
    value.object["weapon"] = dev::JsonValue::stringValue(
      std::string(weaponName(*event.weapon))
    );
  }
  if (event.damage.has_value()) {
    value.object["damage"] = dev::JsonValue::numberValue(*event.damage);
  }
  if (event.position.has_value()) {
    value.object["position"] = vectorJson(*event.position);
  }
  value.object["details"] = event.details;
  return value;
}

dev::JsonValue eventsJson(const std::vector<EventEvidence>& events) {
  dev::JsonValue value = dev::JsonValue::arrayValue();
  value.array.reserve(events.size());
  for (const EventEvidence& event : events) {
    value.array.push_back(eventJson(event));
  }
  return value;
}

dev::JsonValue commandJson(
  const ScenarioState& state,
  const ServerSnapshot& snapshot,
  std::size_t playerSlot,
  const std::array<std::uint32_t, 6>& consumedEdgeCounts
) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["player_slot"] = dev::JsonValue::numberValue(playerSlot);
  if (playerSlot >= state.players.size()) {
    return value;
  }

  const ScenarioPlayerState& player = state.players[playerSlot];
  const UserCommand& command = player.command;
  value.object["has_command"] =
    dev::JsonValue::booleanValue(player.hasCommand);
  value.object["sequence"] =
    dev::JsonValue::numberValue(command.sequence);
  value.object["client_tick"] =
    dev::JsonValue::numberValue(command.clientTick);
  value.object["viewed_server_tick"] =
    dev::JsonValue::numberValue(player.viewedServerTick);
  value.object["forward"] =
    dev::JsonValue::numberValue(command.forwardMove);
  value.object["right"] =
    dev::JsonValue::numberValue(command.rightMove);
  value.object["up"] =
    dev::JsonValue::numberValue(command.upMove);
  value.object["jump"] = dev::JsonValue::booleanValue(command.jump);
  value.object["crouch"] = dev::JsonValue::booleanValue(command.crouch);
  value.object["dash"] = dev::JsonValue::booleanValue(command.dash);
  value.object["attack"] = dev::JsonValue::booleanValue(command.attack);
  value.object["sneak"] = dev::JsonValue::booleanValue(command.sneak);
  value.object["zoom"] = dev::JsonValue::booleanValue(command.zoomed);

  dev::JsonValue edges = dev::JsonValue::objectValue();
  edges.object["jump"] =
    dev::JsonValue::numberValue(player.consumedActionEdges.jump);
  edges.object["dash"] =
    dev::JsonValue::numberValue(player.consumedActionEdges.dash);
  edges.object["reset"] =
    dev::JsonValue::numberValue(player.consumedActionEdges.reset);
  edges.object["ready"] =
    dev::JsonValue::numberValue(player.consumedActionEdges.ready);
  edges.object["mcguffin_throw"] =
    dev::JsonValue::numberValue(player.consumedActionEdges.mcguffinThrow);
  edges.object["attack"] =
    dev::JsonValue::numberValue(player.consumedActionEdges.attack);
  value.object["consumed_action_edges"] = std::move(edges);
  dev::JsonValue edgeCounts = dev::JsonValue::objectValue();
  edgeCounts.object["jump"] =
    dev::JsonValue::numberValue(consumedEdgeCounts[0]);
  edgeCounts.object["crouch"] =
    dev::JsonValue::numberValue(consumedEdgeCounts[1]);
  edgeCounts.object["dash"] =
    dev::JsonValue::numberValue(consumedEdgeCounts[2]);
  edgeCounts.object["attack"] =
    dev::JsonValue::numberValue(consumedEdgeCounts[3]);
  edgeCounts.object["sneak"] =
    dev::JsonValue::numberValue(consumedEdgeCounts[4]);
  edgeCounts.object["zoom"] =
    dev::JsonValue::numberValue(consumedEdgeCounts[5]);
  value.object["consumed_action_edge_counts"] = std::move(edgeCounts);

  value.object["has_acknowledged_command"] =
    dev::JsonValue::booleanValue(
      snapshot.hasAcknowledgedCommand[playerSlot]
    );
  value.object["acknowledged_command_sequence"] =
    dev::JsonValue::numberValue(
      snapshot.acknowledgedCommand[playerSlot]
    );
  return value;
}

dev::JsonValue commonJson(
  std::string_view token,
  const ScenarioDefinition& scenario,
  std::size_t realPlayerSlot,
  std::uint32_t relativeTick,
  const ScenarioState& state,
  const ServerSnapshot& snapshot,
  const std::vector<EventEvidence>& events,
  const std::array<std::uint32_t, 6>& consumedEdgeCounts
) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["schema_version"] = dev::JsonValue::numberValue(1);
  value.object["token"] =
    dev::JsonValue::stringValue(std::string(token));
  value.object["scenario"] =
    dev::JsonValue::stringValue(scenario.name);
  value.object["real_player_slot"] =
    dev::JsonValue::numberValue(realPlayerSlot);
  value.object["relative_tick"] =
    dev::JsonValue::numberValue(relativeTick);
  value.object["absolute_server_tick"] =
    dev::JsonValue::numberValue(state.serverTick);
  value.object["map"] =
    dev::JsonValue::stringValue(scenario.world.map);
  value.object["authoritative_map"] =
    dev::JsonValue::stringValue(state.mapName);
  value.object["map_revision"] =
    dev::JsonValue::numberValue(state.mapRevision);
  value.object["map_content_hash"] =
    dev::JsonValue::numberValue(state.mapContentHash);
  value.object["state"] = scenarioStateJson(state);
  value.object["state_hash"] =
    dev::JsonValue::stringValue(scenarioStateHash(state));
  value.object["events_since_setup"] = eventsJson(events);
  value.object["latest_consumed_command"] =
    commandJson(state, snapshot, realPlayerSlot, consumedEdgeCounts);
  return value;
}

bool exactLiveRoster(
  const std::array<bool, kDuelPlayerCount>& connectedPlayers,
  std::size_t realPlayerSlot
) {
  for (std::size_t index = 0; index < connectedPlayers.size(); ++index) {
    if (connectedPlayers[index] != (index == realPlayerSlot)) {
      return false;
    }
  }
  return true;
}

bool readSmallFile(
  const std::filesystem::path& path,
  std::string& text,
  std::string& error
) {
  std::error_code fileError;
  const std::uintmax_t bytes = std::filesystem::file_size(path, fileError);
  if (fileError) {
    error = "could not inspect '" + path.string() + "': " +
      fileError.message();
    return false;
  }
  if (bytes > kMaxFinishRequestBytes) {
    error = "finish.request.json exceeds 65536 bytes";
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "could not read '" + path.string() + "'";
    return false;
  }
  text.assign(
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  );
  if (input.bad()) {
    error = "could not finish reading '" + path.string() + "'";
    return false;
  }
  return true;
}

} // namespace

bool isSafeLiveScenarioToken(std::string_view token) {
  return !token.empty() && token.size() <= 128U &&
    std::all_of(token.begin(), token.end(), [](unsigned char character) {
      return
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') ||
        character == '-' || character == '_' || character == '.';
    });
}

bool validateLiveScenarioOptions(
  const LiveScenarioOptions& options,
  std::string& error
) {
  if (options.scenarioPath.empty()) {
    error = "--live-scenario requires a non-empty path";
    return false;
  }
  if (options.runDirectory.empty()) {
    error = "--scenario-run-dir requires a non-empty path";
    return false;
  }
  if (!isSafeLiveScenarioToken(options.token)) {
    error =
      "--scenario-token must use 1 to 128 ASCII letters, digits, '.', '-', or '_'";
    return false;
  }
  error.clear();
  return true;
}

bool parseLiveScenarioFinishRequest(
  std::string_view json,
  std::string_view expectedToken,
  LiveScenarioFinishRequest& request,
  std::string& error
) {
  const dev::JsonParseResult parsed = dev::parseJson(json);
  if (!parsed.ok) {
    error = "finish.request.json: JSON: " + parsed.error;
    return false;
  }
  if (parsed.value.type != dev::JsonValue::Type::Object) {
    error = "finish.request.json: root must be an object";
    return false;
  }
  static const std::set<std::string_view> allowed = {
    "schema_version",
    "token",
    "minimum_command_sequence",
  };
  for (const auto& [key, unused] : parsed.value.object) {
    (void)unused;
    if (!allowed.contains(key)) {
      error = "finish.request.json." + key + ": unknown field";
      return false;
    }
  }

  const dev::JsonValue* schemaVersion = parsed.value.find("schema_version");
  if (
    schemaVersion == nullptr ||
    schemaVersion->type != dev::JsonValue::Type::Number ||
    schemaVersion->number != 1.0
  ) {
    error = "finish.request.json.schema_version: must be 1";
    return false;
  }
  const dev::JsonValue* token = parsed.value.find("token");
  if (token == nullptr || token->type != dev::JsonValue::Type::String) {
    error = "finish.request.json.token: must be a string";
    return false;
  }
  if (token->string != expectedToken) {
    error = "finish.request.json.token: does not match this server session";
    return false;
  }
  const dev::JsonValue* minimum =
    parsed.value.find("minimum_command_sequence");
  if (
    minimum == nullptr ||
    minimum->type != dev::JsonValue::Type::Number ||
    !std::isfinite(minimum->number) ||
    minimum->number < 0.0 ||
    minimum->number >
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
    std::floor(minimum->number) != minimum->number
  ) {
    error =
      "finish.request.json.minimum_command_sequence: must be a uint32 integer";
    return false;
  }
  request.minimumCommandSequence =
    static_cast<std::uint32_t>(minimum->number);
  error.clear();
  return true;
}

std::optional<LiveScenarioSession> LiveScenarioSession::load(
  LiveScenarioOptions options,
  std::string& error
) {
  if (!validateLiveScenarioOptions(options, error)) {
    return std::nullopt;
  }
  const ScenarioParseResult parsed = loadScenarioFile(options.scenarioPath);
  if (!parsed.ok) {
    error = parsed.error;
    return std::nullopt;
  }
  LiveScenarioSession session(std::move(options), parsed.scenario);
  if (!session.valid_) {
    error = session.error_;
    return std::nullopt;
  }
  error.clear();
  return session;
}

LiveScenarioSession::LiveScenarioSession(
  LiveScenarioOptions options,
  ScenarioDefinition scenario
) : options_(std::move(options)), scenario_(std::move(scenario)) {
  valid_ = validateScenario();
  assertionEvaluated_.resize(scenario_.assertions.size(), false);
}

bool LiveScenarioSession::validateScenario() {
  if (!validateLiveScenarioOptions(options_, error_)) {
    return false;
  }
  if (scenario_.execution.mode != ScenarioExecutionMode::ClientServer) {
    error_ = "live server requires execution.mode client_server";
    return false;
  }

  std::array<bool, kDuelPlayerCount> seen = {};
  std::size_t realPlayerCount = 0;
  for (const PlayerInitialState& player : scenario_.players) {
    if (player.index >= kDuelPlayerCount) {
      error_ = "scenario player index is outside the server roster";
      return false;
    }
    if (seen[player.index]) {
      error_ = "scenario player index is repeated";
      return false;
    }
    seen[player.index] = true;
    if (player.connected && !player.bot) {
      realPlayerSlot_ = player.index;
      ++realPlayerCount;
    }
  }
  if (realPlayerCount != 1U) {
    error_ = "live scenario must configure exactly one connected human player";
    return false;
  }
  error_.clear();
  return true;
}

LiveScenarioUpdate LiveScenarioSession::beforeServerTick(
  ServerGame& game,
  const std::array<bool, kDuelPlayerCount>& connectedPlayers,
  const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
) {
  if (!valid_ || failed_) {
    return LiveScenarioUpdate::Failed;
  }
  if (complete_) {
    return LiveScenarioUpdate::Complete;
  }
  if (!started_) {
    if (!exactLiveRoster(connectedPlayers, realPlayerSlot_)) {
      return LiveScenarioUpdate::WaitingForRoster;
    }
    if (!begin(game, connectedPlayers, playerSessions)) {
      return LiveScenarioUpdate::Failed;
    }
  }
  if (!armed_) {
    if (!pollStartRequest(game, connectedPlayers, playerSessions)) {
      return failed_
        ? LiveScenarioUpdate::Failed
        : LiveScenarioUpdate::WaitingForStart;
    }
  }

  const ScenarioState state = game.captureScenarioState();
  if (!pollFinishRequest(state, game.snapshot())) {
    return failed_ ? LiveScenarioUpdate::Failed : LiveScenarioUpdate::Complete;
  }
  return complete_
    ? LiveScenarioUpdate::Complete
    : LiveScenarioUpdate::Running;
}

LiveScenarioUpdate LiveScenarioSession::afterServerTick(ServerGame& game) {
  if (!valid_ || failed_) {
    return LiveScenarioUpdate::Failed;
  }
  if (!started_ || !armed_ || complete_) {
    return complete_
      ? LiveScenarioUpdate::Complete
      : LiveScenarioUpdate::WaitingForRoster;
  }

  const ScenarioState state = game.captureScenarioState();
  observeConsumedEdges(state);
  std::string eventError;
  if (!events_.observe(
        0U,
        relativeTick_ + 1U,
        priorState_,
        state,
        game.snapshot(),
        eventError)) {
    fail(eventError);
    (void)writeResult(state, game.snapshot(), error_, false);
    return LiveScenarioUpdate::Failed;
  }
  ++relativeTick_;
  priorState_ = state;
  evaluateAuthoritativeAssertions(state, false);

  if (!writeCheckpoint(state, game.snapshot())) {
    (void)writeResult(state, game.snapshot(), error_, false);
    return LiveScenarioUpdate::Failed;
  }
  if (relativeTick_ >= scenario_.execution.maxTicks) {
    if (!writeResult(state, game.snapshot(), "max_ticks", true)) {
      return LiveScenarioUpdate::Failed;
    }
    complete_ = true;
    return LiveScenarioUpdate::Complete;
  }
  if (!pollFinishRequest(state, game.snapshot())) {
    return failed_ ? LiveScenarioUpdate::Failed : LiveScenarioUpdate::Complete;
  }
  return LiveScenarioUpdate::Running;
}

bool LiveScenarioSession::begin(
  ServerGame& game,
  const std::array<bool, kDuelPlayerCount>& connectedPlayers,
  const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
) {
  std::error_code directoryError;
  std::filesystem::create_directories(options_.runDirectory, directoryError);
  if (directoryError) {
    fail(
      "could not create scenario run directory '" +
      options_.runDirectory.string() + "': " + directoryError.message()
    );
    return false;
  }
  if (!std::filesystem::is_directory(options_.runDirectory, directoryError) ||
      directoryError) {
    fail("scenario run path is not a directory");
    return false;
  }

  if (scenario_.world.map == "default") {
    game.setArena(makeDefaultServerArena());
  } else if (!game.loadRequestedMap(scenario_.world.map)) {
    fail(
      "could not load scenario map '" + scenario_.world.map +
      "' from '" + game.mapDirectory() + "'"
    );
    return false;
  }
  if (
    scenario_.world.gameMode == GameMode::McGuffin &&
    !hasValidMcGuffinLayout(game.arena())
  ) {
    fail(
      "McGuffin scenario map needs one neutral spawn, two bases, and team spawns"
    );
    return false;
  }

  if (!applySetup(game, connectedPlayers, playerSessions)) {
    return false;
  }

  priorState_ = game.captureScenarioState();
  relativeTick_ = 0;
  started_ = true;
  if (!writeReady(priorState_)) {
    return false;
  }
  return true;
}

bool LiveScenarioSession::applySetup(
  ServerGame& game,
  const std::array<bool, kDuelPlayerCount>& connectedPlayers,
  const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
) {
  ScenarioSetup setup = scenarioSetup(scenario_);
  // Keep the live server clock intact. Relative tick zero marks the setup
  // boundary while packets and snapshots retain their real server tick labels.
  setup.serverTick = game.snapshot().serverTick;
  std::string setupError;
  if (!game.applyScenarioSetup(setup, &setupError)) {
    fail("scenario setup failed: " + setupError);
    return false;
  }
  // applyScenarioSetup clears old command identity as part of its clean state.
  // Restore only the UDP-owned session IDs before any command can be consumed.
  game.setConnectedPlayers(connectedPlayers, playerSessions);
  return true;
}

bool LiveScenarioSession::pollStartRequest(
  ServerGame& game,
  const std::array<bool, kDuelPlayerCount>& connectedPlayers,
  const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
) {
  const std::filesystem::path path =
    options_.runDirectory / "start.request.json";
  std::error_code existsError;
  const bool exists = std::filesystem::exists(path, existsError);
  if (existsError) {
    fail("could not inspect start.request.json: " + existsError.message());
    return false;
  }
  if (!exists) {
    return false;
  }

  std::string text;
  std::string requestError;
  if (!readSmallFile(path, text, requestError)) {
    if (++startReadFailures_ <= 16U) {
      return false;
    }
    fail(requestError);
    return false;
  }
  startReadFailures_ = 0;
  const dev::JsonParseResult parsed = dev::parseJson(text);
  if (!parsed.ok || parsed.value.type != dev::JsonValue::Type::Object) {
    fail(
      parsed.ok
        ? "start.request.json: root must be an object"
        : "start.request.json: JSON: " + parsed.error
    );
    return false;
  }
  static const std::set<std::string_view> allowed = {
    "schema_version",
    "token",
  };
  for (const auto& [key, unused] : parsed.value.object) {
    (void)unused;
    if (!allowed.contains(key)) {
      fail("start.request.json." + key + ": unknown field");
      return false;
    }
  }
  const dev::JsonValue* version = parsed.value.find("schema_version");
  const dev::JsonValue* token = parsed.value.find("token");
  if (
    version == nullptr ||
    version->type != dev::JsonValue::Type::Number ||
    version->number != 1.0 ||
    token == nullptr ||
    token->type != dev::JsonValue::Type::String ||
    token->string != options_.token
  ) {
    fail("start.request.json: schema_version or token does not match");
    return false;
  }
  if (!applySetup(game, connectedPlayers, playerSessions)) {
    return false;
  }
  events_ = ScenarioEventJournal{};
  assertions_.clear();
  std::fill(assertionEvaluated_.begin(), assertionEvaluated_.end(), false);
  completionAssertionsEvaluated_ = false;
  priorState_ = game.captureScenarioState();
  relativeTick_ = 0;
  consumedEdgeCounts_.fill(0U);
  armed_ = true;
  if (!writeCheckpoint(priorState_, game.snapshot())) {
    return false;
  }
  return true;
}

bool LiveScenarioSession::writeReady(const ScenarioState& state) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["schema_version"] = dev::JsonValue::numberValue(1);
  value.object["token"] =
    dev::JsonValue::stringValue(options_.token);
  value.object["scenario"] =
    dev::JsonValue::stringValue(scenario_.name);
  value.object["real_player_slot"] =
    dev::JsonValue::numberValue(realPlayerSlot_);
  value.object["relative_tick"] = dev::JsonValue::numberValue(0);
  value.object["absolute_server_tick"] =
    dev::JsonValue::numberValue(state.serverTick);
  value.object["max_ticks"] =
    dev::JsonValue::numberValue(scenario_.execution.maxTicks);
  value.object["map"] =
    dev::JsonValue::stringValue(scenario_.world.map);
  value.object["authoritative_map"] =
    dev::JsonValue::stringValue(state.mapName);
  value.object["map_revision"] =
    dev::JsonValue::numberValue(state.mapRevision);
  value.object["map_content_hash"] =
    dev::JsonValue::numberValue(state.mapContentHash);
  value.object["state_hash"] =
    dev::JsonValue::stringValue(scenarioStateHash(state));
  return writeImmutableJson(options_.runDirectory / "ready.json", value);
}

bool LiveScenarioSession::writeCheckpoint(
  const ScenarioState& state,
  const ServerSnapshot& snapshot
) {
  const std::vector<EventEvidence>& allEvents = events_.events();
  const auto first = allEvents.begin() + static_cast<std::ptrdiff_t>(
    allEvents.size() > kMaxCheckpointEvents
      ? allEvents.size() - kMaxCheckpointEvents
      : 0U
  );
  const std::vector<EventEvidence> recentEvents(first, allEvents.end());
  dev::JsonValue value = commonJson(
    options_.token,
    scenario_,
    realPlayerSlot_,
    relativeTick_,
    state,
    snapshot,
    recentEvents,
    consumedEdgeCounts_
  );
  return writeImmutableJson(
    options_.runDirectory /
      ("checkpoint-" + std::to_string(relativeTick_) + ".json"),
    value
  );
}

bool LiveScenarioSession::writeResult(
  const ScenarioState& state,
  const ServerSnapshot& snapshot,
  std::string_view reason,
  bool passed
) {
  evaluateAuthoritativeAssertions(state, true);
  const bool assertionsPassed = std::all_of(
    assertions_.begin(),
    assertions_.end(),
    [](const AssertionEvidence& assertion) { return assertion.passed; }
  );
  dev::JsonValue value = commonJson(
    options_.token,
    scenario_,
    realPlayerSlot_,
    relativeTick_,
    state,
    snapshot,
    events_.events(),
    consumedEdgeCounts_
  );
  value.object["passed"] =
    dev::JsonValue::booleanValue(passed && assertionsPassed);
  value.object["completion_reason"] =
    dev::JsonValue::stringValue(std::string(reason));
  value.object["ticks_executed"] =
    dev::JsonValue::numberValue(relativeTick_);
  value.object["assertions"] = assertionsEvidenceJson(assertions_);
  return writeImmutableJson(options_.runDirectory / "result.json", value);
}

void LiveScenarioSession::observeConsumedEdges(const ScenarioState& state) {
  if (
    realPlayerSlot_ >= state.players.size() ||
    realPlayerSlot_ >= priorState_.players.size()
  ) {
    return;
  }
  const ScenarioPlayerState& current = state.players[realPlayerSlot_];
  const ScenarioPlayerState& prior = priorState_.players[realPlayerSlot_];
  const auto sequenceAdvanced = [](std::uint32_t next, std::uint32_t previous) {
    return next != 0U &&
      (previous == 0U || isSequenceNewer(next, previous));
  };
  if (sequenceAdvanced(
        current.consumedActionEdges.jump,
        prior.consumedActionEdges.jump)) {
    ++consumedEdgeCounts_[0];
  }
  if (!prior.command.crouch && current.command.crouch) {
    ++consumedEdgeCounts_[1];
  }
  if (sequenceAdvanced(
        current.consumedActionEdges.dash,
        prior.consumedActionEdges.dash)) {
    ++consumedEdgeCounts_[2];
  }
  if (sequenceAdvanced(
        current.consumedActionEdges.attack,
        prior.consumedActionEdges.attack)) {
    ++consumedEdgeCounts_[3];
  }
  if (!prior.command.sneak && current.command.sneak) {
    ++consumedEdgeCounts_[4];
  }
  if (!prior.command.zoomed && current.command.zoomed) {
    ++consumedEdgeCounts_[5];
  }
}

void LiveScenarioSession::evaluateAuthoritativeAssertions(
  const ScenarioState& state,
  bool completion
) {
  if (completion && completionAssertionsEvaluated_) {
    return;
  }
  for (std::size_t index = 0; index < scenario_.assertions.size(); ++index) {
    if (assertionEvaluated_[index]) {
      continue;
    }
    const ScenarioAssertion& assertion = scenario_.assertions[index];
    if (assertion.type > AssertionType::StateHash) {
      continue;
    }
    const bool due = completion
      ? assertion.atCompletion
      : assertion.atTick.has_value() &&
        *assertion.atTick == relativeTick_;
    if (!due) {
      continue;
    }
    assertions_.push_back(evaluateScenarioAssertion(
      index,
      0U,
      relativeTick_,
      assertion,
      state,
      events_.events()
    ));
    assertionEvaluated_[index] = true;
  }
  if (completion) {
    completionAssertionsEvaluated_ = true;
  }
}

bool LiveScenarioSession::pollFinishRequest(
  const ScenarioState& state,
  const ServerSnapshot& snapshot
) {
  const std::filesystem::path path =
    options_.runDirectory / "finish.request.json";
  if (!finishRequest_.has_value()) {
    std::error_code existsError;
    const bool exists = std::filesystem::exists(path, existsError);
    if (existsError) {
      fail(
        "could not inspect finish.request.json: " + existsError.message()
      );
      (void)writeResult(state, snapshot, error_, false);
      return false;
    }
    if (!exists) {
      return true;
    }
    std::string text;
    std::string requestError;
    LiveScenarioFinishRequest request;
    if (!readSmallFile(path, text, requestError)) {
      if (++finishReadFailures_ <= 16U) {
        return true;
      }
      fail(requestError);
      (void)writeResult(state, snapshot, error_, false);
      return false;
    }
    finishReadFailures_ = 0;
    if (!parseLiveScenarioFinishRequest(
          text,
          options_.token,
          request,
          requestError)) {
      fail(requestError);
      (void)writeResult(state, snapshot, error_, false);
      return false;
    }
    finishRequest_ = request;
  }

  const bool acknowledged =
    finishRequest_->minimumCommandSequence == 0U ||
    (
      snapshot.hasAcknowledgedCommand[realPlayerSlot_] &&
      isSequenceAcknowledged(
        finishRequest_->minimumCommandSequence,
        snapshot.acknowledgedCommand[realPlayerSlot_]
      )
    );
  if (!acknowledged) {
    return true;
  }
  if (!writeResult(state, snapshot, "finish_request", true)) {
    return false;
  }
  complete_ = true;
  return false;
}

bool LiveScenarioSession::writeImmutableJson(
  const std::filesystem::path& path,
  const dev::JsonValue& value
) {
  std::error_code existsError;
  if (std::filesystem::exists(path, existsError)) {
    fail("refusing to replace immutable scenario file '" + path.string() + "'");
    return false;
  }
  if (existsError) {
    fail("could not inspect scenario file '" + path.string() + "'");
    return false;
  }

  const std::filesystem::path temporary =
    path.parent_path() /
    ("." + path.filename().string() + "." + options_.token + ".tmp");
  if (std::filesystem::exists(temporary, existsError)) {
    fail("scenario temp file already exists: '" + temporary.string() + "'");
    return false;
  }
  if (existsError) {
    fail("could not inspect scenario temp file '" + temporary.string() + "'");
    return false;
  }

  {
    std::ofstream output(temporary, std::ios::binary);
    if (!output) {
      fail("could not create scenario temp file '" + temporary.string() + "'");
      return false;
    }
    output << dev::writeJson(value) << '\n';
    output.flush();
    if (!output) {
      fail("could not write scenario temp file '" + temporary.string() + "'");
      output.close();
      std::error_code removeError;
      std::filesystem::remove(temporary, removeError);
      return false;
    }
  }

  std::error_code renameError;
  std::filesystem::rename(temporary, path, renameError);
  if (renameError) {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    fail(
      "could not publish immutable scenario file '" + path.string() +
      "': " + renameError.message()
    );
    return false;
  }
  return true;
}

void LiveScenarioSession::fail(std::string message) {
  if (error_.empty()) {
    error_ = std::move(message);
  }
  failed_ = true;
}

bool LiveScenarioSession::started() const {
  return started_;
}

bool LiveScenarioSession::complete() const {
  return complete_;
}

bool LiveScenarioSession::failed() const {
  return failed_;
}

std::uint32_t LiveScenarioSession::relativeTick() const {
  return relativeTick_;
}

std::size_t LiveScenarioSession::realPlayerSlot() const {
  return realPlayerSlot_;
}

const std::string& LiveScenarioSession::error() const {
  return error_;
}

} // namespace lg::scenario
