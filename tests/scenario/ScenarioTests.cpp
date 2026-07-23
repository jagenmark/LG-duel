#include "net/LoopbackTransport.hpp"
#include "scenario/ScenarioEvidence.hpp"
#include "scenario/ScenarioRuntime.hpp"
#include "scenario/ScenarioSchema.hpp"
#include "server/ServerGame.hpp"
#include "sim/MapRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

std::filesystem::path fixture(std::string_view name) {
  return std::filesystem::path(LG_DUEL_SCENARIO_TEST_FIXTURES) / name;
}

lg::scenario::ScenarioParseResult load(std::string_view name) {
  return lg::scenario::loadScenarioFile(fixture(name));
}

const lg::scenario::FinalPlayerEvidence* finalPlayer(
  const lg::scenario::ScenarioRunResult& run,
  std::size_t player
) {
  if (run.evidence.finalStates.empty()) return nullptr;
  const auto& players = run.evidence.finalStates.back().players;
  const auto found = std::find_if(
    players.begin(), players.end(),
    [player](const lg::scenario::FinalPlayerEvidence& value) {
      return value.index == player;
    });
  return found == players.end() ? nullptr : &*found;
}

std::string validScenarioJson(
  std::string_view name,
  std::string_view map = "default",
  std::string_view weapon = "machine_gun"
) {
  return std::string{
    R"({"schema_version":1,"name":")"} + std::string(name) +
    R"(","execution":{"mode":"headless","max_ticks":1,"repeat":1},)" +
    R"("world":{"map":")" + std::string(map) +
    R"(","game_mode":"duel","seed":9},"players":[{"index":0,)" +
    R"("connected":true,"bot":false,"team":"none","position":[-4,0,0.9],)" +
    R"("velocity":[0,0,0],"view_yaw_degrees":0,"view_pitch_degrees":0,)" +
    R"("health":100,"alive":true,"selected_weapon":")" +
    std::string(weapon) + R"("}],"timeline":[],"assertions":[]})";
}

int testParsing() {
  int failures = 0;
  const auto valid = load("divergence_control.json");
  failures += expect(valid.ok, "valid scenario fixture should parse");
  failures += expect(
    valid.ok && valid.scenario.timeline.size() == 1U &&
      valid.scenario.execution.repeat == 3U,
    "valid parse should keep execution and timeline data");

  const auto malformed = lg::scenario::parseScenarioJson("{");
  failures += expect(
    !malformed.ok && malformed.error.find("JSON:") != std::string::npos,
    "malformed JSON should give a JSON error");

  std::string unknownJson = validScenarioJson("unknown_field");
  unknownJson.insert(1, R"("extra":true,)");
  const auto unknown = lg::scenario::parseScenarioJson(unknownJson);
  failures += expect(
    !unknown.ok && unknown.error.find("extra: unknown field") != std::string::npos,
    "unknown fields should name the rejected field");

  const auto version = load("invalid_schema.json");
  failures += expect(
    !version.ok &&
      version.error.find("schema_version") != std::string::npos,
    "unsupported schema versions should fail at schema_version");

  const auto map = lg::scenario::parseScenarioJson(
    validScenarioJson("bad_map", "../unsafe"));
  failures += expect(
    !map.ok && map.error.find("world.map") != std::string::npos,
    "unsafe map names should fail");

  const auto weapon = lg::scenario::parseScenarioJson(
    validScenarioJson("bad_weapon", "default", "not_a_weapon"));
  failures += expect(
    !weapon.ok && weapon.error.find("unknown weapon") != std::string::npos,
    "unknown weapons should fail");

  std::string badEventJson = validScenarioJson("bad_event");
  const std::string emptyAssertions = R"("assertions":[])";
  const std::size_t assertionsAt = badEventJson.find(emptyAssertions);
  badEventJson.replace(
    assertionsAt,
    emptyAssertions.size(),
    R"("assertions":[{"type":"event","at_completion":true,)"
    R"("event":{"type":"damage_appllied","count":1}}])");
  const auto badEvent = lg::scenario::parseScenarioJson(badEventJson);
  failures += expect(
    !badEvent.ok && badEvent.error.find("unsupported event type") !=
      std::string::npos,
    "unknown event concepts should fail during parsing");

  std::string botInputJson = validScenarioJson("bot_input");
  const std::string humanFlags = R"("connected":true,"bot":false)";
  botInputJson.replace(
    botInputJson.find(humanFlags),
    humanFlags.size(),
    R"("connected":false,"bot":true)");
  const std::string emptyTimeline = R"("timeline":[])";
  const std::size_t timelineAt = botInputJson.find(emptyTimeline);
  botInputJson.replace(
    timelineAt,
    emptyTimeline.size(),
    R"("timeline":[{"at_tick":0,"player":0,"duration_ticks":1,)"
    R"("one_tick_edges":[],"input":{"forward":1}}])");
  const auto botInput = lg::scenario::parseScenarioJson(botInputJson);
  failures += expect(
    !botInput.ok && botInput.error.find("non-bot scripted player") !=
      std::string::npos,
    "timeline input for a bot should fail instead of being ignored");
  return failures;
}

int testInputPlaybackAndEdges() {
  int failures = 0;
  auto shortParsed = load("divergence_control.json");
  failures += expect(shortParsed.ok, "input fixture should parse");
  if (!shortParsed.ok) return failures;

  shortParsed.scenario.execution.repeat = 1;
  auto longScenario = shortParsed.scenario;
  shortParsed.scenario.timeline[0].durationTicks = 1;
  longScenario.timeline[0].durationTicks = 3;
  const auto shortRun = lg::scenario::runScenario(shortParsed.scenario);
  const auto longRun = lg::scenario::runScenario(longScenario);
  const auto* shortPlayer = finalPlayer(shortRun, 0);
  const auto* longPlayer = finalPlayer(longRun, 0);
  failures += expect(
    shortRun.passed && longRun.passed && shortPlayer != nullptr &&
      longPlayer != nullptr && longPlayer->position.x > shortPlayer->position.x,
    "held input should play for its full duration");

  const auto edges = lg::scenario::parseScenarioJson(R"({
    "schema_version":1,
    "name":"jump_attack_edges",
    "execution":{"mode":"headless","max_ticks":2,"repeat":1},
    "world":{"map":"default","game_mode":"duel","seed":12},
    "players":[
      {"index":0,"connected":true,"bot":false,"team":"none",
       "position":[-4,0,0.9],"velocity":[0,0,0],"view_yaw_degrees":0,
       "view_pitch_degrees":0,"health":100,"alive":true,
       "selected_weapon":"rocket_launcher"},
      {"index":1,"connected":true,"bot":false,"team":"none",
       "position":[4,5,0.9],"velocity":[0,0,0],"view_yaw_degrees":180,
       "view_pitch_degrees":0,"health":100,"alive":true,
       "selected_weapon":"machine_gun"}
    ],
    "timeline":[
      {"at_tick":0,"player":0,"duration_ticks":1,
       "one_tick_edges":["jump","attack"],
       "input":{"forward":0,"right":0,"jump":true,"crouch":false,
                "dash":false,"attack":true,"weapon":"rocket_launcher",
                "yaw":0,"pitch":0}}
    ],
    "assertions":[
      {"type":"projectile_exists","at_completion":true,"owner":0,
       "weapon":"rocket_launcher"},
      {"type":"event","at_completion":true,
       "event":{"type":"projectile_spawned","actor":0,
                "weapon":"rocket_launcher","count":1}}
    ]
  })");
  failures += expect(edges.ok, "edge scenario should parse");
  if (!edges.ok) return failures;
  const auto edgeRun = lg::scenario::runScenario(edges.scenario);
  const auto* edgePlayer = finalPlayer(edgeRun, 0);
  const std::size_t spawnCount = static_cast<std::size_t>(std::count_if(
    edgeRun.evidence.events.begin(), edgeRun.evidence.events.end(),
    [](const lg::scenario::EventEvidence& event) {
      return event.type == "projectile_spawned";
    }));
  failures += expect(edgeRun.passed, "jump and attack edge scenario should pass");
  failures += expect(
    edgePlayer != nullptr && edgePlayer->velocity.z > 0.0F,
    "one-tick jump edge should launch the player");
  failures += expect(
    spawnCount == 1U,
    "one-tick attack edge should create one projectile event");

  auto pitched = lg::scenario::loadScenarioFile(
    std::filesystem::path("scenarios") / "smoke" / "rocket_splash_open.json");
  failures += expect(pitched.ok, "pitched attack scenario should parse");
  if (pitched.ok) {
    pitched.scenario.execution.repeat = 1;
    pitched.scenario.players[0].viewPitchDegrees = -20.0F;
    pitched.scenario.timeline[0].input.pitchDegrees.reset();
    const auto pitchedRun = lg::scenario::runScenario(pitched.scenario);
    failures += expect(
      pitchedRun.passed,
      "initial view pitch should control an attack without a timeline pitch");
  }
  return failures;
}

int testServerSetupAndHashes() {
  int failures = 0;
  lg::ScenarioSetup setup;
  setup.seed = 1234;
  setup.serverTick = 17;
  setup.match.phase = lg::MatchPhase::Live;
  setup.players[0].connected = true;
  setup.players[0].ready = true;
  setup.players[0].alive = true;
  setup.players[0].health = 73;
  setup.players[0].position = {1.25F, -2.5F, 0.9F};
  setup.players[0].velocity = {0.5F, 0.25F, 0.0F};
  setup.players[0].selectedWeapon = lg::Weapon::Railgun;

  lg::LoopbackTransport transportA;
  lg::LoopbackTransport transportB;
  lg::ServerGame gameA(transportA);
  lg::ServerGame gameB(transportB);
  gameA.setArena(lg::makeDefaultServerArena());
  gameB.setArena(lg::makeDefaultServerArena());
  std::string errorA;
  std::string errorB;
  failures += expect(
    gameA.applyScenarioSetup(setup, &errorA) &&
      gameB.applyScenarioSetup(setup, &errorB),
    "ServerGame should accept a valid deterministic setup");
  const lg::ScenarioState stateA = gameA.captureScenarioState();
  const lg::ScenarioState stateB = gameB.captureScenarioState();
  failures += expect(
    stateA.serverTick == 17U && stateA.players[0].player.health == 73 &&
      stateA.players[0].weapon.selectedWeapon == lg::Weapon::Railgun,
    "capture should keep applied tick, health, and weapon");

  const std::string hashA = lg::scenario::scenarioStateHash(stateA);
  const std::string hashB = lg::scenario::scenarioStateHash(stateB);
  failures += expect(hashA == hashB, "equal setup states should have equal hashes");
  failures += expect(hashA.size() == 16U, "state hash should use a stable fixed width");
  auto changed = stateA;
  changed.players[0].player.health -= 1;
  failures += expect(
    lg::scenario::scenarioStateHash(changed) != hashA,
    "state hash should change when authoritative health changes");
  return failures;
}

int testEventsAssertionsAndRepeat() {
  int failures = 0;
  const auto control = load("divergence_control.json");
  failures += expect(control.ok, "repeat control fixture should parse");
  if (!control.ok) return failures;
  const auto run = lg::scenario::runScenario(control.scenario);
  failures += expect(
    run.passed && run.evidence.runs.size() == 3U,
    "repeat control should complete three runs");
  if (run.evidence.runs.size() == 3U) {
    failures += expect(
      run.evidence.runs[0].finalStateHash ==
          run.evidence.runs[1].finalStateHash &&
        run.evidence.runs[0].finalStateHash ==
          run.evidence.runs[2].finalStateHash &&
        run.evidence.runs[0].eventStreamHash ==
          run.evidence.runs[2].eventStreamHash,
      "repeat runs should have equal state and event hashes");
  }
  failures += expect(
    !run.evidence.assertions.empty() &&
      std::all_of(
        run.evidence.assertions.begin(), run.evidence.assertions.end(),
        [](const lg::scenario::AssertionEvidence& item) {
          return item.passed && item.message.find("health") != std::string::npos;
        }),
    "successful assertions should keep useful evidence");

  const auto expectedFailure = load("assertion_failure.json");
  failures += expect(expectedFailure.ok, "assertion failure fixture should parse");
  if (expectedFailure.ok) {
    const auto failed = lg::scenario::runScenario(expectedFailure.scenario);
    failures += expect(
      failed.passed && failed.evidence.summary.find("XFAIL") == 0U &&
        failed.evidence.assertions.size() == 1U &&
        !failed.evidence.assertions[0].passed &&
        failed.evidence.assertions[0].message.find("health expected 99") !=
          std::string::npos,
      "expected assertion failure should pass as XFAIL with diagnostics");
  }

  lg::scenario::EventEvidence later;
  later.run = 0;
  later.tick = 2;
  later.sequence = 1;
  later.type = "second";
  later.actor = 1;
  lg::scenario::EventEvidence earlier;
  earlier.run = 0;
  earlier.tick = 1;
  earlier.sequence = 0;
  earlier.type = "first";
  earlier.weapon = lg::Weapon::RocketLauncher;
  const lg::dev::JsonValue eventJson =
    lg::scenario::eventsEvidenceJson({earlier, later});
  failures += expect(
    eventJson.type == lg::dev::JsonValue::Type::Array &&
      eventJson.array.size() == 2U &&
      eventJson.array[0].find("type")->string == "first" &&
      eventJson.array[1].find("type")->string == "second" &&
      eventJson.array[0].find("weapon")->string == "rocket_launcher",
    "event JSON should keep journal order and named fields");

  // The runner has no fault hook, so force one hash mismatch in a copied
  // journal and check the same first-difference rule used by repeat runs.
  std::vector<std::string> reference;
  for (const auto& hash : run.evidence.hashes) {
    if (hash.run == 0U) reference.push_back(hash.stateHash);
  }
  auto altered = reference;
  if (altered.size() > 2U) altered[2] = "forced-divergence";
  const auto mismatch = std::mismatch(
    reference.begin(), reference.end(), altered.begin(), altered.end());
  const std::size_t mismatchTick =
    static_cast<std::size_t>(mismatch.first - reference.begin());
  lg::scenario::DivergenceEvidence divergence;
  divergence.run = 1;
  divergence.referenceRun = 0;
  divergence.tick = static_cast<std::uint32_t>(mismatchTick);
  divergence.expectedHash = mismatch.first == reference.end()
    ? "" : *mismatch.first;
  divergence.actualHash = mismatch.second == altered.end()
    ? "" : *mismatch.second;
  divergence.message = "forced mismatch";
  const auto divergenceJson =
    lg::scenario::divergenceEvidenceJson(divergence);
  failures += expect(
    mismatchTick == 2U && divergenceJson.find("tick")->number == 2.0 &&
      divergenceJson.find("expected_hash")->string !=
        divergenceJson.find("actual_hash")->string,
    "controlled divergence should report its first differing tick and hashes");
  return failures;
}

int testArtifactsAndJunit() {
  int failures = 0;
  const auto parsed = load("assertion_failure.json");
  failures += expect(parsed.ok, "artifact fixture should parse");
  if (!parsed.ok) return failures;
  auto run = lg::scenario::runScenario(parsed.scenario);
  run.evidence.runId = "artifact-test";
  const auto stamp =
    std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("lg-duel-scenario-tests-" + std::to_string(stamp));
  std::filesystem::path resultDirectory;
  std::string error;
  const bool wrote = lg::scenario::writeEvidenceArtifacts(
    root, parsed.scenario, run.evidence, resultDirectory, error);
  failures += expect(wrote, "evidence artifacts should write as one result set");
  for (const std::string_view name : {
         "scenario.json", "result.json", "assertions.json", "events.json",
         "final-state.json", "hashes.json", "divergence.json", "summary.json",
         "junit.xml", "manifest.json"}) {
    failures += expect(
      wrote && std::filesystem::is_regular_file(resultDirectory / name),
      std::string("artifact should exist: ") + std::string(name));
  }
  const std::string expectedFailureXml =
    lg::scenario::junitXml(parsed.scenario, run.evidence);
  failures += expect(
    expectedFailureXml.find("failures=\"0\"") != std::string::npos &&
      expectedFailureXml.find("skipped=\"1\"") != std::string::npos &&
      expectedFailureXml.find("<skipped ") != std::string::npos,
    "JUnit should report the named expected failure as skipped");
  std::error_code cleanupError;
  std::filesystem::remove_all(root, cleanupError);

  lg::scenario::ScenarioDefinition xmlScenario;
  xmlScenario.name = "suite<&\"'";
  lg::scenario::ScenarioEvidence xmlEvidence;
  xmlEvidence.passed = false;
  xmlEvidence.summary = "bad <tag> & \"quote\"";
  lg::scenario::AssertionEvidence assertion;
  assertion.type = "health<&";
  assertion.message = "wrong < & > \" '";
  assertion.expected = lg::dev::JsonValue::stringValue("<expected>");
  assertion.actual = lg::dev::JsonValue::stringValue("&actual");
  xmlEvidence.assertions.push_back(assertion);
  const std::string xml = lg::scenario::junitXml(xmlScenario, xmlEvidence);
  failures += expect(
    xml.find("suite&lt;&amp;&quot;&apos;") != std::string::npos &&
      xml.find("wrong &lt; &amp; &gt; &quot; &apos;") != std::string::npos &&
      xml.find("<testsuite") != std::string::npos &&
      xml.find("<failure") != std::string::npos,
    "JUnit should include suite and failure data with XML escaping");
  return failures;
}

} // namespace

int main() {
  int failures = 0;
  failures += testParsing();
  failures += testInputPlaybackAndEdges();
  failures += testServerSetupAndHashes();
  failures += testEventsAssertionsAndRepeat();
  failures += testArtifactsAndJunit();
  if (failures == 0) std::cout << "Scenario tests passed\n";
  return failures == 0 ? 0 : 1;
}
