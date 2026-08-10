#include "replay/ReplayCodec.hpp"
#include "replay/ReplayFile.hpp"

#include "net/NetCodec.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition)
    return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::replay::ReplayDemo sampleDemo() {
  lg::replay::ReplayDemo demo;
  demo.metadata.protocolRevision = lg::kProtocolVersion;
  demo.metadata.initialServerTick = 7U;
  demo.metadata.mapRevision = 1U;
  demo.metadata.mapName = "file_test";
  demo.metadata.mapContentHash = 1U;
  demo.metadata.gameMode = lg::GameMode::Duel;
  demo.metadata.visibility = lg::replay::ReplayVisibility::DuelOnly;
  for (std::size_t index = 0U; index < demo.metadata.players.size(); ++index) {
    demo.metadata.players[index].slot = static_cast<std::uint8_t>(index);
  }
  demo.metadata.players[0].occupied = true;
  demo.metadata.players[0].name = "file";

  lg::replay::ReplayCheckpoint checkpoint;
  checkpoint.serverTick = 7U;
  checkpoint.mapRevision = 1U;
  checkpoint.projectileRevision = 1U;
  checkpoint.spawnRandomState = 1U;
  checkpoint.match.gameMode = lg::GameMode::Duel;
  checkpoint.history.push_back({checkpoint.serverTick, {}});
  demo.checkpoints.push_back(checkpoint);
  demo.hashes.push_back(
      {checkpoint.serverTick, lg::replay::canonicalStateHash(checkpoint)});
  return demo;
}

std::filesystem::path makeTemporaryDirectory() {
  const std::filesystem::path parent = std::filesystem::temp_directory_path();
  const auto seed = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  for (std::uint32_t attempt = 0U; attempt < 64U; ++attempt) {
    const std::filesystem::path candidate =
        parent / ("lg_duel_replay_file_" + std::to_string(seed) + "_" +
                  std::to_string(attempt));
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error))
      return candidate;
  }
  return {};
}

void removeFileAndDirectory(const std::filesystem::path &directory) {
  std::error_code error;
  std::filesystem::remove(directory / "saved.lgdemo", error);
  std::filesystem::remove(directory / "saved.lgdemo.partial", error);
  std::filesystem::remove(directory / "corrupt.lgdemo", error);
  std::filesystem::remove(directory, error);
}

} // namespace

int main() {
  int failures = 0;
  const std::filesystem::path directory = makeTemporaryDirectory();
  failures += expect(!directory.empty(),
                     "file test should create an isolated temporary directory");
  if (directory.empty())
    return 1;

  const lg::replay::ReplayDemo source = sampleDemo();
  const std::filesystem::path savedPath = directory / "saved.lgdemo";
  {
    std::ofstream legacyCollision(savedPath.string() + ".partial",
                                  std::ios::binary | std::ios::out);
    legacyCollision.put('x');
  }
  std::string error;
  failures += expect(
      lg::replay::saveDemoFile(savedPath, source, &error),
      "new demo save should encode and atomically publish a .lgdemo file");
  failures += expect(
      std::filesystem::is_regular_file(savedPath) &&
          std::filesystem::exists(savedPath.string() + ".partial"),
      "exclusive temporary creation should ignore and preserve a predictable "
      "legacy partial-file collision");

  lg::replay::ReplayDemo loaded;
  failures += expect(lg::replay::loadDemoFile(savedPath, loaded, &error),
                     "saved demo should load through the strict codec");
  failures +=
      expect(loaded.metadata.mapName == source.metadata.mapName &&
                 loaded.hashes.size() == 1U &&
                 loaded.hashes[0].value == source.hashes[0].value,
             "loaded demo should retain authoritative metadata and state hash");
  failures += expect(
      !lg::replay::saveDemoFile(savedPath, source, &error) &&
          std::filesystem::exists(savedPath.string() + ".partial"),
      "save should refuse overwrite without touching another writer's partial "
      "path");

  const std::filesystem::path corruptPath = directory / "corrupt.lgdemo";
  {
    std::ofstream corrupt(corruptPath, std::ios::binary | std::ios::out);
    corrupt.put('X');
  }
  lg::replay::ReplayDemo unchanged = loaded;
  failures +=
      expect(!lg::replay::loadDemoFile(corruptPath, unchanged, &error) &&
                 unchanged.metadata.mapName == loaded.metadata.mapName,
             "failed file load should not partly replace the caller's replay");
  failures += expect(!lg::replay::saveDemoFile(
                         directory / "wrong-extension.demo", source, &error),
                     "save should reject a non-.lgdemo path");

  removeFileAndDirectory(directory);
  return failures == 0 ? 0 : 1;
}
