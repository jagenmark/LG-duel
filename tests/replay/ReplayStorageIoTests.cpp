#include "net/NetCodec.hpp"
#include "replay/ReplayCodec.hpp"
#include "replay/ReplayFile.hpp"
#include "replay/ReplayIoService.hpp"
#include "replay/KillcamServerCoordinator.hpp"
#include "replay/ReplayStorage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

std::filesystem::path temporaryDirectory() {
  const auto seed = static_cast<std::uint64_t>(
    std::chrono::steady_clock::now().time_since_epoch().count()
  );
  const std::filesystem::path root = std::filesystem::temp_directory_path();
  for (std::uint32_t attempt = 0; attempt < 64U; ++attempt) {
    const std::filesystem::path path = root / (
      "lg_duel_replay_storage_" + std::to_string(seed) + "_" +
      std::to_string(attempt)
    );
    std::error_code error;
    if (std::filesystem::create_directory(path, error)) return path;
  }
  return {};
}

lg::replay::ReplayDemo sampleDemo(std::uint32_t tick = 7U) {
  lg::replay::ReplayDemo demo;
  demo.metadata.initialServerTick = tick;
  demo.metadata.mapName = "storage_test";
  demo.metadata.mapContentHash = 1U;
  demo.metadata.gameplayConfigHash =
    lg::replay::canonicalGameplayConfigHash(demo.metadata.gameplayConfig);
  for (std::size_t index = 0; index < demo.metadata.players.size(); ++index) {
    demo.metadata.players[index].slot = static_cast<std::uint8_t>(index);
  }
  demo.metadata.players[0].occupied = true;
  demo.metadata.players[0].name = "storage";
  lg::replay::ReplayCheckpoint checkpoint;
  checkpoint.serverTick = tick;
  checkpoint.mapRevision = 1U;
  checkpoint.gameplayConfigHash = demo.metadata.gameplayConfigHash;
  checkpoint.history.push_back({tick, {}});
  demo.checkpoints.push_back(checkpoint);
  demo.hashes.push_back({tick, lg::replay::canonicalStateHash(checkpoint)});
  demo.ticks.push_back({tick, {}});
  return demo;
}

template <typename Predicate>
std::optional<lg::replay::ReplayIoService::Result> waitFor(
  lg::replay::ReplayIoService& service,
  Predicate predicate
) {
  for (int attempt = 0; attempt < 3000; ++attempt) {
    if (const auto result = service.poll(); result.has_value()) {
      if (predicate(*result)) return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return std::nullopt;
}

std::vector<lg::replay::ReplayIoService::Result> waitForResults(
  lg::replay::ReplayIoService& service,
  std::size_t count
) {
  std::vector<lg::replay::ReplayIoService::Result> results;
  for (int attempt = 0; attempt < 3000 && results.size() < count; ++attempt) {
    while (auto result = service.poll()) {
      results.push_back(std::move(*result));
    }
    if (results.size() < count) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return results;
}

} // namespace

int main() {
  int failures = 0;
  const std::filesystem::path defaultDirectory =
    lg::replay::ReplayStorage::defaultDirectory();
  failures += expect(defaultDirectory.filename() == "demos" &&
                     defaultDirectory.parent_path().filename() == "LG Duel" &&
                     defaultDirectory.parent_path().parent_path().filename() == "LG Duel",
    "default demo path should follow the shared SDL preference policy");

  const std::filesystem::path directory = temporaryDirectory();
  failures += expect(!directory.empty(), "test should create a private demo directory");
  if (directory.empty()) return 1;

  lg::replay::ReplayStorage storage(directory / "demos");
  std::string error;
  std::filesystem::path resolved;
  failures += expect(storage.resolveDemoPath("match.lgdemo", resolved, &error),
    "valid names should resolve");
  failures += expect(resolved.filename() == "match.lgdemo",
    "the extension should be added once");
  for (const std::string_view bad : {
         "", ".", "..", "../escape", "nested/name", "nested\\name",
         "C:escape", "CON", "match.lgdemo.lgdemo", "bad.name"
       }) {
    failures += expect(!storage.resolveDemoPath(bad, resolved, &error),
      "unsafe demo names should be rejected");
  }
  failures += expect(storage.ensureDirectory(&error), "storage should create its directory");

  const std::string firstAutomaticStem = storage.automaticStem("dev_cuboids", "duel");
  const std::string secondAutomaticStem = storage.automaticStem("dev_cuboids", "duel");
  failures += expect(
    firstAutomaticStem != secondAutomaticStem &&
      lg::replay::ReplayStorage::sanitizeStem(firstAutomaticStem, &error).has_value() &&
      lg::replay::ReplayStorage::sanitizeStem(secondAutomaticStem, &error).has_value(),
    "automatic demo names should remain unique within one clock tick"
  );

  failures += expect(lg::replay::saveDemoFile(
    storage.directory() / "alpha.lgdemo", sampleDemo(), &error
  ), "fixture demo should save");
  failures += expect(lg::replay::saveDemoFile(
    storage.directory() / "beta.lgdemo", sampleDemo(8U), &error
  ), "second fixture demo should save");
  {
    std::ofstream ignored(storage.directory() / "ignored.lgdemo.partial");
    ignored << 'x';
    std::ofstream duplicate(storage.directory() / "duplicate.lgdemo.lgdemo");
    duplicate << 'x';
  }
  const auto files = storage.list(&error);
  failures += expect(files.size() == 2U && files[0].name == "alpha" && files[1].name == "beta",
    "list should return only regular demos in stable name order");
  failures += expect(storage.deleteDemo("alpha", &error), "delete should remove a safe demo");
  failures += expect(!std::filesystem::exists(storage.directory() / "alpha.lgdemo"),
    "delete should remove only the resolved file");
  failures += expect(!storage.deleteDemo("../beta", &error),
    "delete should apply the same path policy");

  const std::filesystem::path asyncPath = storage.directory() / "async.lgdemo";
  std::vector<std::uint8_t> bytes;
  failures += expect(lg::replay::encodeDemo(sampleDemo(9U), bytes, &error),
    "fixture should encode for the decode job");
  lg::replay::ReplayIoService service;
  lg::replay::ReplayIoService::JobId saveJob = 0;
  failures += expect(service.enqueueSave(asyncPath, sampleDemo(9U), saveJob, &error),
    "save job should enter the worker queue");
  const auto saveResult = waitFor(service, [saveJob](const auto& result) {
    return result.id == saveJob;
  });
  failures += expect(saveResult.has_value() && saveResult->ok,
    "save job should report success without a callback");

  lg::replay::ReplayIoService::JobId loadJob = 0;
  failures += expect(service.enqueueLoad(asyncPath, loadJob, &error),
    "load job should enter the worker queue");
  const auto loadResult = waitFor(service, [loadJob](const auto& result) {
    return result.id == loadJob;
  });
  failures += expect(loadResult.has_value() && loadResult->ok && loadResult->demo.has_value(),
    "load job should return a decoded demo");

  lg::replay::ReplayIoService::JobId decodeJob = 0;
  failures += expect(service.enqueueDecode(std::move(bytes), decodeJob, &error),
    "decode job should enter the worker queue");
  const auto decodeResult = waitFor(service, [decodeJob](const auto& result) {
    return result.id == decodeJob;
  });
  failures += expect(decodeResult.has_value() && decodeResult->ok,
    "decode job should complete in the worker");

  lg::replay::ReplayIoService::JobId listJob = 0;
  failures += expect(service.enqueueList(storage.directory(), listJob, &error),
    "list job should enter the worker queue");
  const auto listResult = waitFor(service, [listJob](const auto& result) {
    return result.id == listJob;
  });
  failures += expect(listResult.has_value() && listResult->ok &&
    std::any_of(listResult->files.begin(), listResult->files.end(), [](const auto& file) {
      return file.name == "async";
    }), "list job should return saved demo metadata");

  lg::replay::ReplayIoService::JobId missingJob = 0;
  failures += expect(service.enqueueLoad(storage.directory() / "missing.lgdemo", missingJob, &error),
    "missing load should still be queued");
  const auto missingResult = waitFor(service, [missingJob](const auto& result) {
    return result.id == missingJob;
  });
  failures += expect(missingResult.has_value() && !missingResult->ok && !missingResult->error.empty(),
    "worker failures should return an error result");
  service.shutdown();
  lg::replay::ReplayIoService::JobId stoppedJob = 0;
  failures += expect(!service.enqueueLoad(asyncPath, stoppedJob, &error),
    "stopped worker should reject new jobs");
  lg::replay::ReplayDemo retainedDemo = sampleDemo(10U);
  failures += expect(
    !service.enqueueSave(asyncPath, retainedDemo, stoppedJob, &error) &&
      retainedDemo.ticks.size() == 1U &&
      retainedDemo.metadata.initialServerTick == 10U,
    "rejected save admission must retain the caller-owned completed demo"
  );

  {
    lg::replay::ReplayIoService queued(
      lg::replay::ReplayIoService::Config{1U, false}
    );
    const std::filesystem::path firstQueuedPath =
      storage.directory() / "queue_pressure_first.lgdemo";
    const std::filesystem::path retryQueuedPath =
      storage.directory() / "queue_pressure_retry.lgdemo";
    lg::replay::ReplayDemo first = sampleDemo(10U);
    lg::replay::ReplayDemo retained = sampleDemo(11U);
    lg::replay::ReplayIoService::JobId firstJob = 0U;
    lg::replay::ReplayIoService::JobId rejectedJob = 0U;
    failures += expect(
      queued.enqueueSave(firstQueuedPath, first, firstJob, &error),
      "a paused worker should admit its first save"
    );
    failures += expect(
      !queued.enqueueSave(retryQueuedPath, retained, rejectedJob, &error) &&
        error == "replay I/O queue is full" &&
        retained.metadata.initialServerTick == 11U &&
        retained.ticks.size() == 1U,
      "queue-pressure rejection must retain the completed demo for retry"
    );
    queued.start();
    const auto firstResult = waitFor(queued, [firstJob](const auto& result) {
      return result.id == firstJob;
    });
    failures += expect(firstResult.has_value() && firstResult->ok,
      "the first queued save should run after the worker starts");
    lg::replay::ReplayIoService::JobId retryJob = 0U;
    failures += expect(
      queued.enqueueSave(retryQueuedPath, retained, retryJob, &error),
      "the completed demo rejected under queue pressure should be retryable"
    );
    const auto retryResult = waitFor(queued, [retryJob](const auto& result) {
      return result.id == retryJob;
    });
    failures += expect(
      retryResult.has_value() && retryResult->ok,
      "the retried completed demo should eventually save"
    );
    queued.shutdown();
  }

  {
    lg::replay::ReplayIoService sameTick(
      lg::replay::ReplayIoService::Config{
        lg::replay::kKillcamEncodeQueueCapacity, false
      }
    );
    lg::replay::ReplayDemo first = sampleDemo(12U);
    lg::replay::ReplayDemo second = sampleDemo(12U);
    lg::replay::ReplayIoService::JobId firstJob = 0U;
    lg::replay::ReplayIoService::JobId secondJob = 0U;
    failures += expect(
      sameTick.enqueueEncode(first, 256U * 1024U, firstJob, &error) &&
        sameTick.enqueueEncode(second, 256U * 1024U, secondJob, &error),
      "the bounded worker must admit both duel deaths ready on the same tick"
    );
    sameTick.start();
    const auto results = waitForResults(sameTick, 2U);
    failures += expect(
      results.size() == 2U &&
        std::all_of(results.begin(), results.end(), [](const auto& result) {
          return result.ok &&
            result.kind == lg::replay::ReplayIoService::JobKind::Encode &&
            !result.bytes.empty();
        }),
      "both same-tick killcam encodes should complete independently"
    );
    sameTick.shutdown();
  }

  std::error_code cleanupError;
  std::filesystem::remove_all(directory, cleanupError);
  return failures == 0 ? 0 : 1;
}
