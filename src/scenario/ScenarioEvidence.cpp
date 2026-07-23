#include "scenario/ScenarioEvidence.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace lg::scenario {
namespace {

[[nodiscard]] dev::JsonValue vectorJson(Vec3 value) {
  return dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(value.x),
    dev::JsonValue::numberValue(value.y),
    dev::JsonValue::numberValue(value.z),
  });
}

[[nodiscard]] dev::JsonValue ammoJson(const WeaponAmmoArray& ammo) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  for (std::size_t index = 0; index < kWeaponCount; ++index) {
    const Weapon weapon = static_cast<Weapon>(index);
    value.object[std::string(weaponName(weapon))] =
      dev::JsonValue::numberValue(ammo[index]);
  }
  return value;
}

[[nodiscard]] dev::JsonValue assertionJson(const AssertionEvidence& assertion) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["assertion_index"] =
    dev::JsonValue::numberValue(assertion.assertionIndex);
  value.object["run"] = dev::JsonValue::numberValue(assertion.run);
  value.object["tick"] = dev::JsonValue::numberValue(assertion.tick);
  value.object["type"] = dev::JsonValue::stringValue(assertion.type);
  value.object["passed"] = dev::JsonValue::booleanValue(assertion.passed);
  value.object["message"] = dev::JsonValue::stringValue(assertion.message);
  value.object["expected"] = assertion.expected;
  value.object["actual"] = assertion.actual;
  return value;
}

[[nodiscard]] dev::JsonValue eventJson(const EventEvidence& event) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["run"] = dev::JsonValue::numberValue(event.run);
  value.object["tick"] = dev::JsonValue::numberValue(event.tick);
  value.object["sequence"] = dev::JsonValue::numberValue(
    static_cast<double>(event.sequence));
  value.object["type"] = dev::JsonValue::stringValue(event.type);
  if (event.actor) value.object["actor"] = dev::JsonValue::numberValue(*event.actor);
  if (event.target) value.object["target"] = dev::JsonValue::numberValue(*event.target);
  if (event.entityId) {
    value.object["entity_id"] =
      dev::JsonValue::numberValue(static_cast<double>(*event.entityId));
  }
  if (event.weapon) {
    value.object["weapon"] =
      dev::JsonValue::stringValue(std::string(weaponName(*event.weapon)));
  }
  if (event.damage) value.object["damage"] = dev::JsonValue::numberValue(*event.damage);
  if (event.position) value.object["position"] = vectorJson(*event.position);
  value.object["details"] = event.details;
  return value;
}

[[nodiscard]] dev::JsonValue playerJson(const FinalPlayerEvidence& player) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["index"] = dev::JsonValue::numberValue(player.index);
  value.object["connected"] = dev::JsonValue::booleanValue(player.connected);
  value.object["team"] = dev::JsonValue::stringValue(std::string(teamName(player.team)));
  value.object["position"] = vectorJson(player.position);
  value.object["velocity"] = vectorJson(player.velocity);
  value.object["view_yaw_degrees"] =
    dev::JsonValue::numberValue(player.viewYawDegrees);
  value.object["view_pitch_degrees"] =
    dev::JsonValue::numberValue(player.viewPitchDegrees);
  value.object["health"] = dev::JsonValue::numberValue(player.health);
  value.object["alive"] = dev::JsonValue::booleanValue(player.alive);
  value.object["selected_weapon"] =
    dev::JsonValue::stringValue(std::string(weaponName(player.selectedWeapon)));
  value.object["ammo"] = ammoJson(player.ammo);
  return value;
}

[[nodiscard]] dev::JsonValue projectileJson(const FinalProjectileEvidence& projectile) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["entity_id"] =
    dev::JsonValue::numberValue(static_cast<double>(projectile.entityId));
  value.object["owner"] = dev::JsonValue::numberValue(projectile.owner);
  value.object["weapon"] =
    dev::JsonValue::stringValue(std::string(weaponName(projectile.weapon)));
  value.object["position"] = vectorJson(projectile.position);
  value.object["velocity"] = vectorJson(projectile.velocity);
  return value;
}

[[nodiscard]] dev::JsonValue finalStateJson(const FinalStateEvidence& state) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["run"] = dev::JsonValue::numberValue(state.run);
  value.object["tick"] = dev::JsonValue::numberValue(state.tick);
  dev::JsonValue players = dev::JsonValue::arrayValue();
  for (const FinalPlayerEvidence& player : state.players) {
    players.array.push_back(playerJson(player));
  }
  value.object["players"] = std::move(players);
  dev::JsonValue projectiles = dev::JsonValue::arrayValue();
  for (const FinalProjectileEvidence& projectile : state.projectiles) {
    projectiles.array.push_back(projectileJson(projectile));
  }
  value.object["projectiles"] = std::move(projectiles);
  value.object["match"] = state.match;
  return value;
}

[[nodiscard]] dev::JsonValue hashJson(const StateHashEvidence& hash) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["run"] = dev::JsonValue::numberValue(hash.run);
  value.object["tick"] = dev::JsonValue::numberValue(hash.tick);
  value.object["state_hash"] = dev::JsonValue::stringValue(hash.stateHash);
  if (hash.eventHash)
    value.object["event_hash"] = dev::JsonValue::stringValue(*hash.eventHash);
  return value;
}

[[nodiscard]] dev::JsonValue runJson(const RunEvidence& run) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["run"] = dev::JsonValue::numberValue(run.run);
  value.object["ticks_executed"] = dev::JsonValue::numberValue(run.ticksExecuted);
  value.object["passed"] = dev::JsonValue::booleanValue(run.passed);
  value.object["expected_failure_observed"] =
    dev::JsonValue::booleanValue(run.expectedFailureObserved);
  value.object["failure"] = dev::JsonValue::stringValue(run.failure);
  value.object["final_state_hash"] = dev::JsonValue::stringValue(run.finalStateHash);
  value.object["event_stream_hash"] = dev::JsonValue::stringValue(run.eventStreamHash);
  return value;
}

[[nodiscard]] std::string xmlEscape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text) {
    switch (character) {
    case '&': escaped += "&amp;"; break;
    case '<': escaped += "&lt;"; break;
    case '>': escaped += "&gt;"; break;
    case '"': escaped += "&quot;"; break;
    case '\'': escaped += "&apos;"; break;
    default:
      if (static_cast<unsigned char>(character) >= 0x20U ||
          character == '\n' || character == '\r' || character == '\t') {
        escaped += character;
      }
      break;
    }
  }
  return escaped;
}

[[nodiscard]] bool safeName(std::string_view value) {
  return !value.empty() && value.size() <= 128U &&
    std::all_of(value.begin(), value.end(), [](unsigned char character) {
      return std::isalnum(character) || character == '_' || character == '-';
    });
}

[[nodiscard]] bool writeJsonFile(
  const std::filesystem::path& path,
  const dev::JsonValue& value,
  std::string& error
) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open evidence file '" + path.string() + "'";
    return false;
  }
  output << dev::writeJson(value) << '\n';
  output.close();
  if (!output) {
    error = "could not write evidence file '" + path.string() + "'";
    return false;
  }
  return true;
}

[[nodiscard]] bool writeTextFile(
  const std::filesystem::path& path,
  std::string_view text,
  std::string& error
) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open evidence file '" + path.string() + "'";
    return false;
  }
  output << text;
  output.close();
  if (!output) {
    error = "could not write evidence file '" + path.string() + "'";
    return false;
  }
  return true;
}

} // namespace

dev::JsonValue assertionsEvidenceJson(
  const std::vector<AssertionEvidence>& assertions
) {
  dev::JsonValue values = dev::JsonValue::arrayValue();
  for (const AssertionEvidence& assertion : assertions)
    values.array.push_back(assertionJson(assertion));
  return values;
}

dev::JsonValue eventsEvidenceJson(const std::vector<EventEvidence>& events) {
  dev::JsonValue values = dev::JsonValue::arrayValue();
  for (const EventEvidence& event : events) values.array.push_back(eventJson(event));
  return values;
}

dev::JsonValue finalStatesEvidenceJson(const std::vector<FinalStateEvidence>& states) {
  dev::JsonValue values = dev::JsonValue::arrayValue();
  for (const FinalStateEvidence& state : states)
    values.array.push_back(finalStateJson(state));
  return values;
}

dev::JsonValue hashesEvidenceJson(const std::vector<StateHashEvidence>& hashes) {
  dev::JsonValue values = dev::JsonValue::arrayValue();
  for (const StateHashEvidence& hash : hashes) values.array.push_back(hashJson(hash));
  return values;
}

dev::JsonValue divergenceEvidenceJson(
  const std::optional<DivergenceEvidence>& divergence
) {
  if (!divergence) return {};
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["run"] = dev::JsonValue::numberValue(divergence->run);
  value.object["reference_run"] =
    dev::JsonValue::numberValue(divergence->referenceRun);
  value.object["tick"] = dev::JsonValue::numberValue(divergence->tick);
  value.object["expected_hash"] =
    dev::JsonValue::stringValue(divergence->expectedHash);
  value.object["actual_hash"] =
    dev::JsonValue::stringValue(divergence->actualHash);
  value.object["message"] = dev::JsonValue::stringValue(divergence->message);
  value.object["expected_state"] = divergence->expectedState;
  value.object["actual_state"] = divergence->actualState;
  return value;
}

dev::JsonValue summaryEvidenceJson(
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence
) {
  const std::size_t passedAssertions = static_cast<std::size_t>(std::count_if(
    evidence.assertions.begin(), evidence.assertions.end(),
    [](const AssertionEvidence& assertion) { return assertion.passed; }));
  const std::size_t passedRuns = static_cast<std::size_t>(std::count_if(
    evidence.runs.begin(), evidence.runs.end(),
    [](const RunEvidence& run) { return run.passed; }));
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["schema_version"] =
    dev::JsonValue::numberValue(kScenarioEvidenceSchemaVersion);
  value.object["scenario"] = dev::JsonValue::stringValue(scenario.name);
  value.object["run_id"] = dev::JsonValue::stringValue(evidence.runId);
  value.object["passed"] = dev::JsonValue::booleanValue(evidence.passed);
  value.object["expected_failure"] =
    dev::JsonValue::booleanValue(evidence.expectedFailure);
  value.object["summary"] = dev::JsonValue::stringValue(evidence.summary);
  value.object["run_count"] = dev::JsonValue::numberValue(evidence.runs.size());
  value.object["passed_runs"] = dev::JsonValue::numberValue(passedRuns);
  value.object["assertion_count"] =
    dev::JsonValue::numberValue(evidence.assertions.size());
  value.object["passed_assertions"] =
    dev::JsonValue::numberValue(passedAssertions);
  value.object["failed_assertions"] =
    dev::JsonValue::numberValue(evidence.assertions.size() - passedAssertions);
  value.object["event_count"] = dev::JsonValue::numberValue(evidence.events.size());
  value.object["hash_count"] = dev::JsonValue::numberValue(evidence.hashes.size());
  value.object["has_divergence"] =
    dev::JsonValue::booleanValue(evidence.divergence.has_value());
  return value;
}

dev::JsonValue evidenceJson(
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence
) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["schema_version"] = dev::JsonValue::numberValue(evidence.schemaVersion);
  value.object["scenario"] = scenarioJson(scenario);
  value.object["summary"] = summaryEvidenceJson(scenario, evidence);
  dev::JsonValue runs = dev::JsonValue::arrayValue();
  for (const RunEvidence& run : evidence.runs)
    runs.array.push_back(runJson(run));
  value.object["runs"] = std::move(runs);
  value.object["assertions"] = assertionsEvidenceJson(evidence.assertions);
  value.object["events"] = eventsEvidenceJson(evidence.events);
  value.object["final_states"] = finalStatesEvidenceJson(evidence.finalStates);
  value.object["hashes"] = hashesEvidenceJson(evidence.hashes);
  value.object["divergence"] = divergenceEvidenceJson(evidence.divergence);
  return value;
}

std::string junitXml(
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence
) {
  const bool expectedFailureObserved =
    scenario.expectedFailure.has_value() &&
    evidence.summary.rfind("XFAIL:", 0U) == 0U;
  std::vector<const AssertionEvidence*> failures;
  std::size_t skipped = 0;
  for (const AssertionEvidence& assertion : evidence.assertions) {
    if (assertion.passed) continue;
    if (
      expectedFailureObserved &&
      assertion.assertionIndex == scenario.expectedFailure->assertionIndex
    ) {
      ++skipped;
    } else {
      failures.push_back(&assertion);
    }
  }
  std::size_t infrastructureFailures = 0;
  for (const RunEvidence& run : evidence.runs) {
    if (!run.passed && run.failure.size() > 0U) ++infrastructureFailures;
  }
  const std::size_t testCount =
    std::max<std::size_t>(1U, evidence.assertions.size() + infrastructureFailures);
  const std::size_t failureCount =
    failures.size() + infrastructureFailures +
    ((!evidence.passed && failures.empty() && infrastructureFailures == 0U) ? 1U : 0U);

  std::ostringstream output;
  output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  output << "<testsuite name=\"" << xmlEscape(scenario.name)
         << "\" tests=\"" << testCount
         << "\" failures=\"" << failureCount
         << "\" skipped=\"" << skipped << "\">\n";
  for (const AssertionEvidence& assertion : evidence.assertions) {
    output << "  <testcase classname=\"lg.scenario." << xmlEscape(scenario.name)
           << "\" name=\"assertion_" << assertion.assertionIndex << "_"
           << xmlEscape(assertion.type) << "_run_" << assertion.run << "\">\n";
    if (!assertion.passed) {
      if (
        expectedFailureObserved &&
        assertion.assertionIndex == scenario.expectedFailure->assertionIndex
      ) {
        output << "    <skipped message=\""
               << xmlEscape(scenario.expectedFailure->reason) << "\"/>\n";
      } else {
        const std::string detail = assertion.message + "\nexpected: " +
          dev::writeJson(assertion.expected) + "\nactual: " +
          dev::writeJson(assertion.actual);
        output << "    <failure message=\"" << xmlEscape(assertion.message)
               << "\">" << xmlEscape(detail) << "</failure>\n";
      }
    }
    output << "  </testcase>\n";
  }
  for (const RunEvidence& run : evidence.runs) {
    if (run.passed || run.failure.empty()) continue;
    output << "  <testcase classname=\"lg.scenario." << xmlEscape(scenario.name)
           << "\" name=\"run_" << run.run << "\">\n"
           << "    <failure message=\"" << xmlEscape(run.failure) << "\">"
           << xmlEscape(run.failure) << "</failure>\n"
           << "  </testcase>\n";
  }
  if (evidence.assertions.empty() && infrastructureFailures == 0U) {
    output << "  <testcase classname=\"lg.scenario." << xmlEscape(scenario.name)
           << "\" name=\"scenario\">\n";
    if (!evidence.passed) {
      output << "    <failure message=\"" << xmlEscape(evidence.summary) << "\">"
             << xmlEscape(evidence.summary) << "</failure>\n";
    }
    output << "  </testcase>\n";
  }
  output << "</testsuite>\n";
  return output.str();
}

bool writeEvidenceArtifacts(
  const std::filesystem::path& evidenceRoot,
  const ScenarioDefinition& scenario,
  const ScenarioEvidence& evidence,
  std::filesystem::path& resultDirectory,
  std::string& error
) {
  if (!safeName(scenario.name) || !safeName(evidence.runId)) {
    error = "scenario name and evidence run_id must use letters, numbers, '_' or '-'";
    return false;
  }
  const std::filesystem::path scenarioDirectory = evidenceRoot / scenario.name;
  resultDirectory = scenarioDirectory / evidence.runId;
  const std::filesystem::path partial =
    scenarioDirectory / ("." + evidence.runId + ".partial");
  std::error_code ec;
  if (std::filesystem::exists(resultDirectory, ec) ||
      std::filesystem::exists(partial, ec)) {
    error = "evidence result or partial directory already exists";
    return false;
  }
  std::filesystem::create_directories(partial, ec);
  if (ec) {
    error = "could not create evidence directory: " + ec.message();
    return false;
  }

  const auto fail = [&](std::string message) {
    error = std::move(message);
    std::error_code cleanupError;
    std::filesystem::remove_all(partial, cleanupError);
    return false;
  };
  std::string writeError;
  if (!writeJsonFile(partial / "scenario.json", scenarioJson(scenario), writeError) ||
      !writeJsonFile(
        partial / "result.json", evidenceJson(scenario, evidence), writeError) ||
      !writeJsonFile(
        partial / "assertions.json",
        assertionsEvidenceJson(evidence.assertions), writeError) ||
      !writeJsonFile(
        partial / "events.json", eventsEvidenceJson(evidence.events), writeError) ||
      !writeJsonFile(
        partial / "final-state.json",
        finalStatesEvidenceJson(evidence.finalStates), writeError) ||
      !writeJsonFile(
        partial / "hashes.json", hashesEvidenceJson(evidence.hashes), writeError) ||
      !writeJsonFile(
        partial / "divergence.json",
        divergenceEvidenceJson(evidence.divergence), writeError) ||
      !writeJsonFile(
        partial / "summary.json",
        summaryEvidenceJson(scenario, evidence), writeError) ||
      !writeTextFile(
        partial / "junit.xml", junitXml(scenario, evidence), writeError)) {
    return fail(std::move(writeError));
  }

  dev::JsonValue manifest = dev::JsonValue::objectValue();
  manifest.object["schema_version"] =
    dev::JsonValue::numberValue(kScenarioEvidenceSchemaVersion);
  manifest.object["scenario"] = dev::JsonValue::stringValue(scenario.name);
  manifest.object["run_id"] = dev::JsonValue::stringValue(evidence.runId);
  manifest.object["complete"] = dev::JsonValue::booleanValue(true);
  manifest.object["passed"] = dev::JsonValue::booleanValue(evidence.passed);
  manifest.object["files"] = dev::JsonValue::arrayValue({
    dev::JsonValue::stringValue("scenario.json"),
    dev::JsonValue::stringValue("result.json"),
    dev::JsonValue::stringValue("assertions.json"),
    dev::JsonValue::stringValue("events.json"),
    dev::JsonValue::stringValue("final-state.json"),
    dev::JsonValue::stringValue("hashes.json"),
    dev::JsonValue::stringValue("divergence.json"),
    dev::JsonValue::stringValue("summary.json"),
    dev::JsonValue::stringValue("junit.xml"),
    dev::JsonValue::stringValue("manifest.json"),
  });
  if (!writeJsonFile(partial / "manifest.json", manifest, writeError))
    return fail(std::move(writeError));

  std::filesystem::rename(partial, resultDirectory, ec);
  if (ec) return fail("could not finish evidence directory: " + ec.message());
  return true;
}

} // namespace lg::scenario
