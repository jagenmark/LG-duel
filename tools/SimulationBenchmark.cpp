#include "dev/DevJson.hpp"
#include "sim/ArenaBroadphase.hpp"
#include "sim/Combat.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::string workload = "movement-collision";
  std::string map = "overkill_import";
  std::string mapDirectory = "maps";
  std::filesystem::path outputDirectory;
  std::size_t repetitions = 5;
  std::size_t warmupBatches = 5;
  std::size_t measuredBatches = 40;
  std::size_t operationsPerBatch = 256;
  bool forceLinear = false;
  bool profileBroadphase = false;
};

struct BatchSample {
  std::size_t repetition = 0;
  std::size_t batch = 0;
  double movementMicroseconds = 0.0;
  double hitscanMicroseconds = 0.0;
  double projectileMicroseconds = 0.0;
  std::uint64_t checksum = 0;
  lg::ArenaBroadphaseProfile movementProfile;
  lg::ArenaBroadphaseProfile hitscanProfile;
  lg::ArenaBroadphaseProfile projectileProfile;
};

volatile std::uint64_t gChecksumSink = 0;

[[nodiscard]] std::optional<std::size_t> positiveSize(std::string_view text) {
  if (text.empty() || text.front() == '-' || text.front() == '+') return std::nullopt;
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(std::string(text), &consumed);
    if (consumed != text.size() || value == 0 || value > 1000000ULL ||
        value > std::numeric_limits<std::size_t>::max()) return std::nullopt;
    return static_cast<std::size_t>(value);
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--help") {
      std::cout << "Usage: lg_duel_sim_benchmark --workload movement-collision|trace-projectile "
                   "--output DIR [--map NAME] [--map-directory DIR] [--repetitions N] "
                   "[--warmup-batches N] [--measured-batches N] [--operations-per-batch N] "
                   "[--force-linear] [--profile-broadphase]\n";
      return false;
    }
    if (argument == "--force-linear") {
      options.forceLinear = true;
      continue;
    }
    if (argument == "--profile-broadphase") {
      options.profileBroadphase = true;
      continue;
    }
    if (index + 1 >= argc) {
      error = "missing value for " + std::string(argument);
      return false;
    }
    const std::string value = argv[++index];
    if (argument == "--workload") options.workload = value;
    else if (argument == "--map") options.map = value;
    else if (argument == "--map-directory") options.mapDirectory = value;
    else if (argument == "--output") options.outputDirectory = value;
    else if (argument == "--repetitions" || argument == "--warmup-batches" ||
             argument == "--measured-batches" || argument == "--operations-per-batch") {
      const auto parsed = positiveSize(value);
      if (!parsed) {
        error = std::string(argument) + " must be a positive integer";
        return false;
      }
      if (argument == "--repetitions") options.repetitions = *parsed;
      else if (argument == "--warmup-batches") options.warmupBatches = *parsed;
      else if (argument == "--measured-batches") options.measuredBatches = *parsed;
      else options.operationsPerBatch = *parsed;
    } else {
      error = "unknown argument " + std::string(argument);
      return false;
    }
  }
  if (options.workload != "movement-collision" && options.workload != "trace-projectile") {
    error = "workload must be movement-collision or trace-projectile";
    return false;
  }
  if (options.outputDirectory.empty()) {
    error = "--output is required";
    return false;
  }
  return true;
}

void mix(std::uint64_t& hash, std::uint32_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
}

void mix(std::uint64_t& hash, float value) {
  mix(hash, std::bit_cast<std::uint32_t>(value));
}

void mix(std::uint64_t& hash, lg::Vec3 value) {
  mix(hash, value.x);
  mix(hash, value.y);
  mix(hash, value.z);
}

[[nodiscard]] std::array<lg::PlayerState, lg::kMaxPlayers> initialPlayers(const lg::Arena& arena) {
  std::array<lg::PlayerState, lg::kMaxPlayers> players = {};
  for (std::size_t index = 0; index < players.size(); ++index) {
    players[index].position = arena.spawnPositions[index];
    players[index].position.z += players[index].bounds.halfHeight + 0.05F;
    const lg::CollisionResult resolved = lg::resolvePlayerArenaCollision(
      arena, players[index], players[index].position, {}
    );
    players[index].position = resolved.position;
    players[index].onGround = resolved.onGround;
    players[index].movementMode = resolved.onGround
      ? lg::MovementMode::Grounded : lg::MovementMode::Airborne;
  }
  return players;
}

[[nodiscard]] std::uint64_t runMovementBatch(
  const lg::Arena& arena,
  std::size_t operations,
  std::size_t batchSeed,
  lg::ArenaBroadphaseProfile* profile = nullptr
) {
  std::optional<lg::ArenaBroadphaseProfileScope> profileScope;
  if (profile != nullptr) profileScope.emplace(*profile);
  auto players = initialPlayers(arena);
  const lg::MovementTuning tuning = {};
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t operation = 0; operation < operations; ++operation) {
    const std::size_t playerIndex = operation % players.size();
    lg::UserCommand command;
    command.sequence = static_cast<std::uint32_t>(operation + batchSeed * operations);
    command.clientTick = command.sequence;
    const std::size_t phase = (operation / players.size() + batchSeed * 7U) % 16U;
    command.forwardMove = phase < 8U ? 1.0F : -0.65F;
    command.rightMove = ((phase / 2U) % 2U) == 0U ? 0.75F : -0.75F;
    command.jump = phase == 2U || phase == 11U;
    command.dash = phase == 5U;
    command.viewYawRadians = static_cast<float>((phase * 3U + playerIndex) % 16U) * 0.3926990817F;
    lg::simulateMovement(players[playerIndex], command, arena, tuning, 1.0F / 125.0F);
    mix(hash, players[playerIndex].position);
    mix(hash, players[playerIndex].velocity);
    mix(hash, static_cast<std::uint32_t>(players[playerIndex].movementMode));
    mix(hash, static_cast<std::uint32_t>(players[playerIndex].onGround));
  }
  return hash;
}

[[nodiscard]] lg::Vec3 deterministicDirection(std::size_t index) {
  const float x = static_cast<float>(static_cast<int>((index * 37U) % 101U) - 50);
  const float y = static_cast<float>(static_cast<int>((index * 61U) % 97U) - 48);
  const float z = static_cast<float>(static_cast<int>((index * 17U) % 43U) - 21) * 0.35F;
  return lg::normalize(lg::Vec3{x + 0.25F, y - 0.5F, z + 0.125F});
}

[[nodiscard]] std::uint64_t runTraceBatch(
  const lg::Arena& arena,
  std::size_t operations,
  std::size_t batchSeed,
  double& hitscanMicroseconds,
  double& projectileMicroseconds,
  lg::ArenaBroadphaseProfile* hitscanProfile = nullptr,
  lg::ArenaBroadphaseProfile* projectileProfile = nullptr
) {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto hitscanStart = Clock::now();
  {
    std::optional<lg::ArenaBroadphaseProfileScope> profileScope;
    if (hitscanProfile != nullptr) profileScope.emplace(*hitscanProfile);
    for (std::size_t operation = 0; operation < operations; ++operation) {
      const std::size_t key = operation + batchSeed * operations;
      const lg::Vec3 origin = arena.spawnPositions[key % lg::kMaxPlayers] +
        lg::Vec3{0.0F, 0.0F, 0.65F};
      const lg::WorldTrace trace = lg::traceWorld(arena, origin, deterministicDirection(key), 180.0F);
      mix(hash, static_cast<std::uint32_t>(trace.hit));
      mix(hash, trace.distance);
      mix(hash, trace.end);
      mix(hash, trace.normal);
    }
  }
  const auto hitscanEnd = Clock::now();

  std::array<lg::Vec3, lg::kMaxRocketProjectiles> projectiles = {};
  for (std::size_t index = 0; index < projectiles.size(); ++index) {
    projectiles[index] = arena.spawnPositions[index % lg::kMaxPlayers] + lg::Vec3{0.0F, 0.0F, 0.7F};
  }
  const auto projectileStart = Clock::now();
  {
    std::optional<lg::ArenaBroadphaseProfileScope> profileScope;
    if (projectileProfile != nullptr) profileScope.emplace(*projectileProfile);
    for (std::size_t operation = 0; operation < operations; ++operation) {
      const std::size_t projectileIndex = operation % projectiles.size();
      const std::size_t key = operation + batchSeed * operations + 0x9e37U;
      const lg::Vec3 direction = deterministicDirection(key);
      const lg::WorldTrace trace = lg::traceWorld(arena, projectiles[projectileIndex], direction, 0.4F);
      projectiles[projectileIndex] = trace.hit
        ? arena.spawnPositions[(projectileIndex + operation + 1U) % lg::kMaxPlayers] + lg::Vec3{0.0F, 0.0F, 0.7F}
        : trace.end;
      mix(hash, static_cast<std::uint32_t>(trace.hit));
      mix(hash, trace.distance);
      mix(hash, projectiles[projectileIndex]);
    }
  }
  const auto projectileEnd = Clock::now();
  hitscanMicroseconds = std::chrono::duration<double, std::micro>(hitscanEnd - hitscanStart).count() /
    static_cast<double>(operations);
  projectileMicroseconds = std::chrono::duration<double, std::micro>(projectileEnd - projectileStart).count() /
    static_cast<double>(operations);
  return hash;
}

void mergeProfile(
  lg::ArenaBroadphaseProfile& aggregate,
  const lg::ArenaBroadphaseProfile& sample
) {
  aggregate.queryCount += sample.queryCount;
  aggregate.totalStaticSolids = std::max(
    aggregate.totalStaticSolids,
    sample.totalStaticSolids
  );
  aggregate.nodesVisited += sample.nodesVisited;
  aggregate.candidatesReturned += sample.candidatesReturned;
  aggregate.candidatesTested += sample.candidatesTested;
  aggregate.fallbackCount += sample.fallbackCount;
  aggregate.maxNodesVisited = std::max(
    aggregate.maxNodesVisited,
    sample.maxNodesVisited
  );
  aggregate.maxCandidatesReturned = std::max(
    aggregate.maxCandidatesReturned,
    sample.maxCandidatesReturned
  );
  aggregate.maxCandidatesTested = std::max(
    aggregate.maxCandidatesTested,
    sample.maxCandidatesTested
  );
}

[[nodiscard]] lg::dev::JsonValue profileJson(
  const lg::ArenaBroadphaseProfile& profile
) {
  lg::dev::JsonValue result = lg::dev::JsonValue::objectValue();
  const double queryCount = static_cast<double>(profile.queryCount);
  result.object["query_count"] = lg::dev::JsonValue::numberValue(queryCount);
  result.object["total_static_solids"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.totalStaticSolids)
  );
  result.object["nodes_visited"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.nodesVisited)
  );
  result.object["nodes_visited_per_query"] = lg::dev::JsonValue::numberValue(
    queryCount > 0.0 ? static_cast<double>(profile.nodesVisited) / queryCount : 0.0
  );
  result.object["max_nodes_visited"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.maxNodesVisited)
  );
  result.object["bvh_candidates_returned"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.candidatesReturned)
  );
  result.object["bvh_candidates_per_query"] = lg::dev::JsonValue::numberValue(
    queryCount > 0.0 ? static_cast<double>(profile.candidatesReturned) / queryCount : 0.0
  );
  result.object["max_bvh_candidates_returned"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.maxCandidatesReturned)
  );
  result.object["candidates_actually_tested"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.candidatesTested)
  );
  result.object["candidates_tested_per_query"] = lg::dev::JsonValue::numberValue(
    queryCount > 0.0 ? static_cast<double>(profile.candidatesTested) / queryCount : 0.0
  );
  result.object["max_candidates_tested"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.maxCandidatesTested)
  );
  result.object["fallback_count"] = lg::dev::JsonValue::numberValue(
    static_cast<double>(profile.fallbackCount)
  );
  return result;
}

[[nodiscard]] double nearestRank(std::vector<double> values, double fraction) {
  std::sort(values.begin(), values.end());
  const std::size_t rank = std::max<std::size_t>(1U, static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(values.size()))
  ));
  return values[std::min(rank - 1U, values.size() - 1U)];
}

[[nodiscard]] lg::dev::JsonValue summaryJson(const std::vector<double>& values) {
  lg::dev::JsonValue result = lg::dev::JsonValue::objectValue();
  if (values.empty()) return result;
  double sum = 0.0;
  for (const double value : values) sum += value;
  const double mean = sum / static_cast<double>(values.size());
  double squaredDeviation = 0.0;
  for (const double value : values) {
    const double delta = value - mean;
    squaredDeviation += delta * delta;
  }
  const double stddev = values.size() > 1U
    ? std::sqrt(squaredDeviation / static_cast<double>(values.size() - 1U)) : 0.0;
  result.object["count"] = lg::dev::JsonValue::numberValue(static_cast<double>(values.size()));
  result.object["mean"] = lg::dev::JsonValue::numberValue(mean);
  result.object["median"] = lg::dev::JsonValue::numberValue(nearestRank(values, 0.5));
  result.object["p95"] = lg::dev::JsonValue::numberValue(nearestRank(values, 0.95));
  result.object["p99"] = lg::dev::JsonValue::numberValue(nearestRank(values, 0.99));
  result.object["min"] = lg::dev::JsonValue::numberValue(*std::min_element(values.begin(), values.end()));
  result.object["max"] = lg::dev::JsonValue::numberValue(*std::max_element(values.begin(), values.end()));
  result.object["stddev"] = lg::dev::JsonValue::numberValue(stddev);
  result.object["cv_percent"] = lg::dev::JsonValue::numberValue(mean != 0.0 ? stddev / mean * 100.0 : 0.0);
  return result;
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!parseOptions(argc, argv, options, error)) {
    if (!error.empty()) std::cerr << "LG simulation benchmark error: " << error << '\n';
    return error.empty() ? 0 : 2;
  }
  lg::LocalMapLoadResult loaded = lg::loadLocalMap(options.map, options.mapDirectory);
  if (!loaded.ok) {
    std::cerr << "LG simulation benchmark error: " << loaded.error << '\n';
    return 3;
  }
  if (options.forceLinear) loaded.arena.collisionIndex.reset();
  std::error_code directoryError;
  std::filesystem::create_directories(options.outputDirectory, directoryError);
  if (directoryError) {
    std::cerr << "LG simulation benchmark error: " << directoryError.message() << '\n';
    return 4;
  }

  for (std::size_t batch = 0; batch < options.warmupBatches; ++batch) {
    if (options.workload == "movement-collision") {
      gChecksumSink = runMovementBatch(loaded.arena, options.operationsPerBatch, batch);
    } else {
      double hitscan = 0.0;
      double projectile = 0.0;
      gChecksumSink = runTraceBatch(loaded.arena, options.operationsPerBatch, batch, hitscan, projectile);
    }
  }

  std::vector<BatchSample> samples;
  std::vector<double> movementSamples;
  std::vector<double> hitscanSamples;
  std::vector<double> projectileSamples;
  std::optional<std::uint64_t> expectedChecksum;
  bool deterministic = true;
  for (std::size_t repetition = 0; repetition < options.repetitions; ++repetition) {
    std::uint64_t repetitionChecksum = 1469598103934665603ULL;
    for (std::size_t batch = 0; batch < options.measuredBatches; ++batch) {
      BatchSample sample;
      sample.repetition = repetition + 1U;
      sample.batch = batch + 1U;
      if (options.workload == "movement-collision") {
        const auto start = Clock::now();
        sample.checksum = runMovementBatch(
          loaded.arena,
          options.operationsPerBatch,
          batch,
          options.profileBroadphase ? &sample.movementProfile : nullptr
        );
        const auto end = Clock::now();
        sample.movementMicroseconds = std::chrono::duration<double, std::micro>(end - start).count() /
          static_cast<double>(options.operationsPerBatch);
        movementSamples.push_back(sample.movementMicroseconds);
      } else {
        sample.checksum = runTraceBatch(
          loaded.arena, options.operationsPerBatch, batch,
          sample.hitscanMicroseconds, sample.projectileMicroseconds,
          options.profileBroadphase ? &sample.hitscanProfile : nullptr,
          options.profileBroadphase ? &sample.projectileProfile : nullptr
        );
        hitscanSamples.push_back(sample.hitscanMicroseconds);
        projectileSamples.push_back(sample.projectileMicroseconds);
      }
      mix(repetitionChecksum, static_cast<std::uint32_t>(sample.checksum));
      mix(repetitionChecksum, static_cast<std::uint32_t>(sample.checksum >> 32U));
      samples.push_back(sample);
    }
    if (!expectedChecksum) expectedChecksum = repetitionChecksum;
    else deterministic = deterministic && *expectedChecksum == repetitionChecksum;
    gChecksumSink = repetitionChecksum;
  }

  const std::filesystem::path csvPath = options.outputDirectory / "samples.csv";
  std::ofstream csv(csvPath, std::ios::binary | std::ios::trunc);
  csv << "repetition,batch,movement_us_per_operation,hitscan_us_per_trace,projectile_us_per_trace,checksum\n";
  for (const BatchSample& sample : samples) {
    csv << sample.repetition << ',' << sample.batch << ',' << sample.movementMicroseconds << ','
        << sample.hitscanMicroseconds << ',' << sample.projectileMicroseconds << ','
        << sample.checksum << '\n';
  }
  if (!csv) {
    std::cerr << "LG simulation benchmark error: could not write " << csvPath << '\n';
    return 5;
  }

  std::filesystem::path profilePath;
  if (options.profileBroadphase) {
    profilePath = options.outputDirectory / "broadphase-profile.csv";
    std::ofstream profileCsv(profilePath, std::ios::binary | std::ios::trunc);
    profileCsv << "repetition,batch,phase,query_count,total_static_solids,nodes_visited,"
      "bvh_candidates_returned,candidates_actually_tested,fallback_count,"
      "max_nodes_visited,max_bvh_candidates_returned,max_candidates_tested\n";
    const auto writeProfile = [&profileCsv](
      const BatchSample& sample,
      std::string_view phase,
      const lg::ArenaBroadphaseProfile& profile
    ) {
      profileCsv << sample.repetition << ',' << sample.batch << ',' << phase << ','
        << profile.queryCount << ',' << profile.totalStaticSolids << ','
        << profile.nodesVisited << ',' << profile.candidatesReturned << ','
        << profile.candidatesTested << ',' << profile.fallbackCount << ','
        << profile.maxNodesVisited << ',' << profile.maxCandidatesReturned << ','
        << profile.maxCandidatesTested << '\n';
    };
    for (const BatchSample& sample : samples) {
      if (options.workload == "movement-collision") {
        writeProfile(sample, "movement", sample.movementProfile);
      } else {
        writeProfile(sample, "hitscan", sample.hitscanProfile);
        writeProfile(sample, "projectile", sample.projectileProfile);
      }
    }
    if (!profileCsv) {
      std::cerr << "LG simulation benchmark error: could not write " << profilePath << '\n';
      return 5;
    }
  }

  lg::dev::JsonValue root = lg::dev::JsonValue::objectValue();
  root.object["schema_version"] = lg::dev::JsonValue::numberValue(1.0);
  root.object["benchmark_kind"] = lg::dev::JsonValue::stringValue("shared-simulation-microbenchmark");
  root.object["workload"] = lg::dev::JsonValue::stringValue(options.workload);
  root.object["collision_query_mode"] = lg::dev::JsonValue::stringValue(
    options.forceLinear ? "forced-linear" : "indexed-when-available"
  );
  root.object["broadphase_profiling_enabled"] =
    lg::dev::JsonValue::booleanValue(options.profileBroadphase);
  root.object["map"] = lg::dev::JsonValue::stringValue(options.map);
  root.object["map_content_hash"] = lg::dev::JsonValue::numberValue(loaded.descriptor.contentHash);
  root.object["wall_count"] = lg::dev::JsonValue::numberValue(static_cast<double>(loaded.arena.wallCount));
  root.object["brush_count"] = lg::dev::JsonValue::numberValue(static_cast<double>(loaded.arena.brushCount));
  root.object["repetitions"] = lg::dev::JsonValue::numberValue(static_cast<double>(options.repetitions));
  root.object["warmup_batches"] = lg::dev::JsonValue::numberValue(static_cast<double>(options.warmupBatches));
  root.object["measured_batches"] = lg::dev::JsonValue::numberValue(static_cast<double>(options.measuredBatches));
  root.object["operations_per_batch"] = lg::dev::JsonValue::numberValue(static_cast<double>(options.operationsPerBatch));
  root.object["percentile_method"] = lg::dev::JsonValue::stringValue("nearest-rank");
  root.object["deterministic_replay"] = lg::dev::JsonValue::booleanValue(deterministic);
  root.object["valid"] = lg::dev::JsonValue::booleanValue(deterministic && !samples.empty());
  root.object["checksum"] = lg::dev::JsonValue::stringValue(std::to_string(expectedChecksum.value_or(0U)));
  lg::dev::JsonValue metrics = lg::dev::JsonValue::objectValue();
  if (!movementSamples.empty()) metrics.object["movement_collision_us_per_operation"] = summaryJson(movementSamples);
  if (!hitscanSamples.empty()) metrics.object["hitscan_us_per_trace"] = summaryJson(hitscanSamples);
  if (!projectileSamples.empty()) metrics.object["projectile_segment_us_per_trace"] = summaryJson(projectileSamples);
  root.object["metrics"] = std::move(metrics);
  if (options.profileBroadphase) {
    lg::ArenaBroadphaseProfile movement;
    lg::ArenaBroadphaseProfile hitscan;
    lg::ArenaBroadphaseProfile projectile;
    for (const BatchSample& sample : samples) {
      mergeProfile(movement, sample.movementProfile);
      mergeProfile(hitscan, sample.hitscanProfile);
      mergeProfile(projectile, sample.projectileProfile);
    }
    lg::dev::JsonValue profile = lg::dev::JsonValue::objectValue();
    profile.object["instrumentation_note"] = lg::dev::JsonValue::stringValue(
      "Explicit profiling adds counter overhead; use unprofiled runs for timing comparisons."
    );
    if (movement.queryCount > 0U) {
      profile.object["movement"] = profileJson(movement);
    }
    if (hitscan.queryCount > 0U) {
      profile.object["hitscan"] = profileJson(hitscan);
    }
    if (projectile.queryCount > 0U) {
      profile.object["projectile"] = profileJson(projectile);
    }
    root.object["broadphase_profile"] = std::move(profile);
  }
  lg::dev::JsonValue artifacts = lg::dev::JsonValue::objectValue();
  artifacts.object["samples_csv"] = lg::dev::JsonValue::stringValue(csvPath.string());
  if (options.profileBroadphase) {
    artifacts.object["broadphase_profile_csv"] =
      lg::dev::JsonValue::stringValue(profilePath.string());
  }
  root.object["artifacts"] = std::move(artifacts);

  const std::filesystem::path resultPath = options.outputDirectory / "result.json";
  std::ofstream result(resultPath, std::ios::binary | std::ios::trunc);
  result << lg::dev::writeJson(root) << '\n';
  if (!result) {
    std::cerr << "LG simulation benchmark error: could not write " << resultPath << '\n';
    return 6;
  }
  std::cout << lg::dev::writeJson(root) << '\n';
  return deterministic ? 0 : 7;
}
