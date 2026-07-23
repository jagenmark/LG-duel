#include "scenario/ScenarioEvidence.hpp"
#include "scenario/ScenarioRuntime.hpp"
#include "scenario/ScenarioSchema.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
  std::optional<std::string> suite;
  std::optional<std::filesystem::path> scenario;
  std::optional<std::uint32_t> repeat;
  std::filesystem::path output = "scenario-results";
  std::filesystem::path maps = "maps";
};

struct SuiteEntry {
  std::string name;
  std::string status;
  std::string failure;
  std::string finalHash;
  std::uint32_t repeat = 0;
  std::filesystem::path artifactDirectory;
};

void printHelp() {
  std::cout
    << "Usage: lg_duel_scenarios [--suite smoke | --scenario NAME_OR_PATH]\n"
    << "                         [--repeat N] [--output DIR] [--maps DIR]\n"
    << "\n"
    << "Runs authoritative simulation ticks without SDL or wall-clock waits.\n";
}

bool parseUnsigned(std::string_view text, std::uint32_t& value) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
    value > 0U;
}

bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      printHelp();
      return false;
    }
    if (index + 1 >= argc) {
      error = argument + " needs a value";
      return false;
    }
    const std::string value = argv[++index];
    if (argument == "--suite") options.suite = value;
    else if (argument == "--scenario") options.scenario = value;
    else if (argument == "--repeat") {
      std::uint32_t repeat = 0;
      if (!parseUnsigned(value, repeat)) {
        error = "--repeat must be a positive whole number";
        return false;
      }
      options.repeat = repeat;
    } else if (argument == "--output") options.output = value;
    else if (argument == "--maps") options.maps = value;
    else {
      error = "unknown option '" + argument + "'";
      return false;
    }
  }
  if (options.suite.has_value() == options.scenario.has_value()) {
    error = "choose exactly one of --suite or --scenario";
    return false;
  }
  if (options.suite && *options.suite != "smoke") {
    error = "the only Phase-1 suite is 'smoke'";
    return false;
  }
  return true;
}

std::filesystem::path scenarioPath(std::filesystem::path input) {
  if (input.has_parent_path() || input.has_extension()) return input;
  const std::filesystem::path smoke =
    std::filesystem::path("scenarios") / "smoke" / (input.string() + ".json");
  if (std::filesystem::exists(smoke)) return smoke;
  return std::filesystem::path("scenarios") / (input.string() + ".json");
}

std::string xmlEscape(std::string_view text) {
  std::string escaped;
  for (const char character : text) {
    switch (character) {
    case '&': escaped += "&amp;"; break;
    case '<': escaped += "&lt;"; break;
    case '>': escaped += "&gt;"; break;
    case '"': escaped += "&quot;"; break;
    case '\'': escaped += "&apos;"; break;
    default: escaped.push_back(character); break;
    }
  }
  return escaped;
}

bool writeText(
  const std::filesystem::path& path,
  std::string_view text,
  std::string& error
) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  if (!output) {
    error = "could not write '" + path.string() + "'";
    return false;
  }
  return true;
}

bool writeSuiteEvidence(
  const std::filesystem::path& root,
  const std::vector<SuiteEntry>& entries,
  std::string& error
) {
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) {
    error = "could not create output directory: " + ec.message();
    return false;
  }

  std::size_t passed = 0;
  std::size_t failed = 0;
  std::size_t expectedFailures = 0;
  std::string firstFailure;
  lg::dev::JsonValue scenarioValues = lg::dev::JsonValue::arrayValue();
  for (const SuiteEntry& entry : entries) {
    if (entry.status == "PASS") ++passed;
    else if (entry.status == "XFAIL") ++expectedFailures;
    else {
      ++failed;
      if (firstFailure.empty()) {
        firstFailure = entry.name + ": " + entry.failure;
      }
    }
    lg::dev::JsonValue value = lg::dev::JsonValue::objectValue();
    value.object["name"] = lg::dev::JsonValue::stringValue(entry.name);
    value.object["status"] = lg::dev::JsonValue::stringValue(entry.status);
    value.object["repeat_count"] =
      lg::dev::JsonValue::numberValue(entry.repeat);
    value.object["final_hash"] =
      lg::dev::JsonValue::stringValue(entry.finalHash);
    value.object["failure"] =
      lg::dev::JsonValue::stringValue(entry.failure);
    value.object["artifact_path"] =
      lg::dev::JsonValue::stringValue(entry.artifactDirectory.generic_string());
    scenarioValues.array.push_back(std::move(value));
  }

  lg::dev::JsonValue summary = lg::dev::JsonValue::objectValue();
  summary.object["schema_version"] = lg::dev::JsonValue::numberValue(1);
  summary.object["passed_scenarios"] =
    lg::dev::JsonValue::numberValue(passed);
  summary.object["failed_scenarios"] =
    lg::dev::JsonValue::numberValue(failed);
  summary.object["expected_failures"] =
    lg::dev::JsonValue::numberValue(expectedFailures);
  summary.object["first_failure"] =
    lg::dev::JsonValue::stringValue(firstFailure);
  summary.object["scenarios"] = scenarioValues;
  if (!writeText(
        root / "summary.json", lg::dev::writeJson(summary) + "\n", error)) {
    return false;
  }

  std::ostringstream junit;
  junit << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<testsuite name=\"lg.scenarios\" tests=\"" << entries.size()
        << "\" failures=\"" << failed << "\" skipped=\""
        << expectedFailures << "\">\n";
  for (const SuiteEntry& entry : entries) {
    junit << "  <testcase classname=\"lg.scenario\" name=\""
          << xmlEscape(entry.name) << "\">\n";
    if (entry.status == "XFAIL") {
      junit << "    <skipped message=\"" << xmlEscape(entry.failure)
            << "\"/>\n";
    } else if (entry.status != "PASS") {
      junit << "    <failure message=\"" << xmlEscape(entry.failure) << "\">"
            << xmlEscape(entry.failure) << "</failure>\n";
    }
    junit << "  </testcase>\n";
  }
  junit << "</testsuite>\n";
  if (!writeText(root / "junit.xml", junit.str(), error)) return false;

  lg::dev::JsonValue manifest = lg::dev::JsonValue::objectValue();
  manifest.object["schema_version"] = lg::dev::JsonValue::numberValue(1);
  manifest.object["complete"] = lg::dev::JsonValue::booleanValue(true);
  manifest.object["passed"] = lg::dev::JsonValue::booleanValue(failed == 0U);
  manifest.object["summary"] =
    lg::dev::JsonValue::stringValue("summary.json");
  manifest.object["junit"] =
    lg::dev::JsonValue::stringValue("junit.xml");
  manifest.object["scenario_root"] =
    lg::dev::JsonValue::stringValue("scenarios");
  return writeText(
    root / "manifest.json", lg::dev::writeJson(manifest) + "\n", error);
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!parseOptions(argc, argv, options, error)) {
    if (!error.empty()) {
      std::cerr << "error: " << error << '\n';
      printHelp();
      return 2;
    }
    return 0;
  }

  std::vector<std::filesystem::path> paths;
  if (options.suite) {
    const std::filesystem::path directory = "scenarios/smoke";
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
      std::cerr << "error: suite directory '" << directory.string()
                << "' does not exist\n";
      return 2;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json")
        paths.push_back(entry.path());
    }
    if (ec) {
      std::cerr << "error: could not scan suite: " << ec.message() << '\n';
      return 2;
    }
    std::sort(paths.begin(), paths.end());
    if (paths.empty()) {
      std::cerr << "error: suite has no JSON scenarios\n";
      return 2;
    }
  } else {
    paths.push_back(scenarioPath(*options.scenario));
  }

  bool allPassed = true;
  std::vector<SuiteEntry> suiteEntries;
  for (const std::filesystem::path& path : paths) {
    const lg::scenario::ScenarioParseResult parsed =
      lg::scenario::loadScenarioFile(path);
    if (!parsed.ok) {
      std::cerr << path.string() << ": parse failed: " << parsed.error << '\n';
      suiteEntries.push_back({
        path.stem().string(), "ERROR", parsed.error, "", 0U, {}
      });
      allPassed = false;
      continue;
    }
    lg::scenario::ScenarioRunOptions runOptions;
    runOptions.mapsDirectory = options.maps;
    runOptions.repeat = options.repeat;
    lg::scenario::ScenarioRunResult run =
      lg::scenario::runScenario(parsed.scenario, runOptions);
    std::filesystem::path artifactDirectory;
    std::string writeError;
    const bool wrote = lg::scenario::writeEvidenceArtifacts(
      options.output / "scenarios", parsed.scenario, run.evidence,
      artifactDirectory, writeError);
    std::cout << parsed.scenario.name << ": " << run.evidence.summary;
    if (wrote) std::cout << " [" << artifactDirectory.string() << "]";
    std::cout << '\n';
    if (!wrote) {
      std::cerr << parsed.scenario.name
                << ": could not write evidence: " << writeError << '\n';
    }
    SuiteEntry suiteEntry;
    suiteEntry.name = parsed.scenario.name;
    suiteEntry.status = run.evidence.summary.rfind("XFAIL:", 0) == 0
      ? "XFAIL"
      : run.passed && wrote ? "PASS"
      : run.evidence.summary == "PASS" ? "ERROR" : run.evidence.summary;
    suiteEntry.failure = run.error.empty()
      ? run.evidence.summary
      : run.error;
    suiteEntry.repeat = static_cast<std::uint32_t>(run.evidence.runs.size());
    if (!run.evidence.runs.empty()) {
      suiteEntry.finalHash = run.evidence.runs.front().finalStateHash;
    }
    suiteEntry.artifactDirectory = artifactDirectory;
    suiteEntries.push_back(std::move(suiteEntry));
    if (!run.passed || !wrote) allPassed = false;
  }
  std::string suiteError;
  if (!writeSuiteEvidence(options.output, suiteEntries, suiteError)) {
    std::cerr << "error: could not write suite evidence: "
              << suiteError << '\n';
    allPassed = false;
  }
  return allPassed ? 0 : 1;
}
