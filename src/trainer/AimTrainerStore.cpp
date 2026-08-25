#include "trainer/AimTrainerStore.hpp"

#include "dev/DevJson.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>

namespace lg {
namespace {

using dev::JsonValue;

[[nodiscard]] JsonValue number(std::uint64_t value) {
  // JSON numbers are doubles in the shared parser. Keep IDs, seeds and
  // scores exact instead of silently rounding past 2^53.
  return JsonValue::stringValue(std::to_string(value));
}

[[nodiscard]] JsonValue vec(Vec3 value) {
  JsonValue result = JsonValue::arrayValue();
  result.array = {
    JsonValue::numberValue(value.x), JsonValue::numberValue(value.y),
    JsonValue::numberValue(value.z)
  };
  return result;
}

[[nodiscard]] std::optional<Vec3> readVec(const JsonValue& value) {
  if (value.type != JsonValue::Type::Array || value.array.size() != 3U) return std::nullopt;
  const JsonValue& x = value.array[0];
  const JsonValue& y = value.array[1];
  const JsonValue& z = value.array[2];
  if (x.type != JsonValue::Type::Number || y.type != JsonValue::Type::Number ||
      z.type != JsonValue::Type::Number || !std::isfinite(x.number) ||
      !std::isfinite(y.number) || !std::isfinite(z.number)) return std::nullopt;
  return Vec3{static_cast<float>(x.number), static_cast<float>(y.number), static_cast<float>(z.number)};
}

[[nodiscard]] std::optional<std::uint64_t> readNatural(const JsonValue& object, std::string_view key) {
  const JsonValue* raw = object.find(key);
  if (raw == nullptr) return std::nullopt;
  if (raw->type == JsonValue::Type::String) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(
      raw->string.data(), raw->string.data() + raw->string.size(), value
    );
    if (parsed.ec != std::errc{} || parsed.ptr != raw->string.data() + raw->string.size()) {
      return std::nullopt;
    }
    return value;
  }
  if (raw->type != JsonValue::Type::Number || !std::isfinite(raw->number) || raw->number < 0.0 ||
      std::floor(raw->number) != raw->number ||
      raw->number > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) return std::nullopt;
  return static_cast<std::uint64_t>(raw->number);
}

[[nodiscard]] bool requireObject(const JsonValue& value) {
  return value.type == JsonValue::Type::Object;
}

[[nodiscard]] JsonValue scenarioJson(const AimScenario& scenario) {
  JsonValue value = JsonValue::objectValue();
  value.object["version"] = number(scenario.version);
  value.object["name"] = JsonValue::stringValue(scenario.name);
  value.object["duration_ticks"] = number(scenario.durationTicks);
  value.object["player_movement"] = number(static_cast<std::uint8_t>(scenario.playerMovement));
  value.object["weapon_policy"] = number(static_cast<std::uint8_t>(scenario.weaponPolicy));
  value.object["forced_weapon"] = number(weaponIndex(scenario.forcedWeapon));
  JsonValue allowed = JsonValue::arrayValue();
  for (bool enabled : scenario.allowedWeapons) allowed.array.push_back(JsonValue::booleanValue(enabled));
  value.object["allowed_weapons"] = std::move(allowed);
  value.object["infinite_ammo"] = JsonValue::booleanValue(scenario.infiniteAmmo);
  value.object["score_mode"] = number(static_cast<std::uint8_t>(scenario.scoreMode));
  value.object["hit_score"] = number(scenario.hitScore);
  value.object["damage_score_per_point"] = number(scenario.damageScorePerPoint);
  value.object["clear_score"] = number(scenario.clearScore);
  value.object["seed"] = number(scenario.seed);
  value.object["map_name"] = JsonValue::stringValue(scenario.mapName);
  value.object["map_identity"] = number(scenario.mapIdentity);
  value.object["balance_identity"] = number(scenario.balanceIdentity);
  JsonValue groups = JsonValue::arrayValue();
  for (const AimTargetGroup& group : scenario.groups) {
    JsonValue item = JsonValue::objectValue();
    item.object["name"] = JsonValue::stringValue(group.name);
    item.object["visual"] = number(static_cast<std::uint8_t>(group.visual));
    item.object["life"] = number(static_cast<std::uint8_t>(group.life));
    item.object["spawn_mode"] = number(static_cast<std::uint8_t>(group.spawnMode));
    item.object["motion"] = number(static_cast<std::uint8_t>(group.motion));
    item.object["color"] = JsonValue::arrayValue({
      JsonValue::numberValue(group.color.red), JsonValue::numberValue(group.color.green),
      JsonValue::numberValue(group.color.blue)
    });
    item.object["radius"] = JsonValue::numberValue(group.radius);
    item.object["count"] = number(group.count);
    item.object["health"] = JsonValue::numberValue(group.health);
    item.object["respawn_delay_ticks"] = number(group.respawnDelayTicks);
    JsonValue spawns = JsonValue::arrayValue();
    for (Vec3 spawn : group.fixedSpawns) spawns.array.push_back(vec(spawn));
    item.object["fixed_spawns"] = std::move(spawns);
    item.object["random_minimum"] = vec(group.randomMinimum);
    item.object["random_maximum"] = vec(group.randomMaximum);
    item.object["strafe_direction"] = vec(group.strafeDirection);
    item.object["strafe_speed"] = JsonValue::numberValue(group.strafeSpeed);
    item.object["waypoint_ticks"] = number(group.waypointTicks);
    groups.array.push_back(std::move(item));
  }
  value.object["groups"] = std::move(groups);
  return value;
}

[[nodiscard]] bool enumValue(std::uint64_t value, std::uint64_t count) {
  return value < count;
}

[[nodiscard]] bool validStoredScenario(const AimScenario& scenario) {
  if (scenario.name.empty() || scenario.durationTicks == 0U ||
      scenario.groups.empty() || scenario.groups.size() > AimScenario::kMaxGroups ||
      scenario.hitScore == 0U || scenario.damageScorePerPoint == 0U ||
      scenario.clearScore == 0U) {
    return false;
  }
  if (
    scenario.weaponPolicy == AimWeaponPolicy::All &&
    std::none_of(
      scenario.allowedWeapons.begin(),
      scenario.allowedWeapons.end(),
      [](bool allowed) { return allowed; }
    )
  ) {
    return false;
  }
  std::size_t targetCount = 0U;
  for (const AimTargetGroup& group : scenario.groups) {
    if (group.name.empty() || group.count == 0U ||
        group.count > AimScenario::kMaxTargetsPerGroup ||
        !std::isfinite(group.radius) || group.radius <= 0.0F ||
        (group.life == AimTargetLife::Health && group.health <= 0) ||
        (group.spawnMode == AimSpawnMode::FixedList && group.fixedSpawns.empty()) ||
        !std::isfinite(group.strafeSpeed) || group.strafeSpeed < 0.0F ||
        group.waypointTicks == 0U) {
      return false;
    }
    if (
      group.spawnMode == AimSpawnMode::BoundedRandom &&
      (
        group.randomMinimum.x > group.randomMaximum.x ||
        group.randomMinimum.y > group.randomMaximum.y ||
        group.randomMinimum.z > group.randomMaximum.z
      )
    ) {
      return false;
    }
    targetCount += group.count;
  }
  return targetCount <= AimScenario::kMaxTargets;
}

[[nodiscard]] std::optional<AimScenario> readScenario(const JsonValue& value) {
  if (!requireObject(value)) return std::nullopt;
  AimScenario scenario;
  const auto version = readNatural(value, "version");
  const auto name = dev::stringMember(value, "name");
  const auto duration = readNatural(value, "duration_ticks");
  const auto movement = readNatural(value, "player_movement");
  const auto weaponPolicy = readNatural(value, "weapon_policy");
  const auto forced = readNatural(value, "forced_weapon");
  const auto infiniteAmmo = dev::boolMember(value, "infinite_ammo");
  const auto scoreMode = readNatural(value, "score_mode");
  const auto hitScore = readNatural(value, "hit_score").value_or(1U);
  const auto damageScore = readNatural(value, "damage_score_per_point").value_or(1U);
  const auto clearScore = readNatural(value, "clear_score").value_or(1U);
  const auto seed = readNatural(value, "seed");
  const auto mapName = dev::stringMember(value, "map_name");
  const auto mapIdentity = readNatural(value, "map_identity");
  const auto balanceIdentity = readNatural(value, "balance_identity");
  const JsonValue* allowed = value.find("allowed_weapons");
  const JsonValue* groups = value.find("groups");
  if (!version || *version != AimScenario::kVersion || !name || !duration || !movement ||
      !weaponPolicy || !forced || !infiniteAmmo || !scoreMode || !seed || !mapName ||
      !mapIdentity || !balanceIdentity || !allowed || !groups ||
      *duration > 450000U || hitScore > 1000000U || damageScore > 1000000U ||
      clearScore > 1000000U ||
      allowed->type != JsonValue::Type::Array || allowed->array.size() != kWeaponCount ||
      groups->type != JsonValue::Type::Array ||
      !enumValue(*movement, 2) || !enumValue(*weaponPolicy, 2) ||
      !enumValue(*forced, kWeaponCount) || !enumValue(*scoreMode, 3)) return std::nullopt;
  scenario.version = static_cast<std::uint32_t>(*version);
  scenario.name = *name;
  scenario.durationTicks = static_cast<std::uint32_t>(*duration);
  scenario.playerMovement = static_cast<AimPlayerMovement>(*movement);
  scenario.weaponPolicy = static_cast<AimWeaponPolicy>(*weaponPolicy);
  scenario.forcedWeapon = static_cast<Weapon>(*forced);
  scenario.infiniteAmmo = *infiniteAmmo;
  scenario.scoreMode = static_cast<AimScoreMode>(*scoreMode);
  scenario.hitScore = static_cast<std::uint32_t>(hitScore);
  scenario.damageScorePerPoint = static_cast<std::uint32_t>(damageScore);
  scenario.clearScore = static_cast<std::uint32_t>(clearScore);
  scenario.seed = *seed;
  scenario.mapName = *mapName;
  scenario.mapIdentity = *mapIdentity;
  scenario.balanceIdentity = *balanceIdentity;
  for (std::size_t index = 0; index < kWeaponCount; ++index) {
    if (allowed->array[index].type != JsonValue::Type::Boolean) return std::nullopt;
    scenario.allowedWeapons[index] = allowed->array[index].boolean;
  }
  scenario.groups.clear();
  for (const JsonValue& item : groups->array) {
    if (!requireObject(item)) return std::nullopt;
    AimTargetGroup group;
    const auto groupName = dev::stringMember(item, "name");
    const auto visual = readNatural(item, "visual");
    const auto life = readNatural(item, "life");
    const auto spawnMode = readNatural(item, "spawn_mode");
    const auto motion = readNatural(item, "motion");
    const JsonValue* color = item.find("color");
    const auto radius = dev::numberMember(item, "radius");
    const auto count = readNatural(item, "count");
    const auto health = dev::numberMember(item, "health");
    const auto respawn = readNatural(item, "respawn_delay_ticks");
    const JsonValue* spawns = item.find("fixed_spawns");
    const JsonValue* randomMinimum = item.find("random_minimum");
    const JsonValue* randomMaximum = item.find("random_maximum");
    const JsonValue* strafeDirection = item.find("strafe_direction");
    const auto strafeSpeed = dev::numberMember(item, "strafe_speed");
    const auto waypoint = readNatural(item, "waypoint_ticks");
    if (!groupName || !visual || !life || !spawnMode || !motion || !color || !radius || !count ||
        !health || !respawn || !spawns || !randomMinimum || !randomMaximum || !strafeDirection ||
        !strafeSpeed || !waypoint || !enumValue(*visual, 2) || !enumValue(*life, 3) ||
        !enumValue(*spawnMode, 2) || !enumValue(*motion, 4) ||
        *count > AimScenario::kMaxTargetsPerGroup || *respawn > 450000U ||
        *waypoint > 450000U || *health < 1.0 || *health > 100000.0 ||
        color->type != JsonValue::Type::Array || color->array.size() != 3U ||
        spawns->type != JsonValue::Type::Array) return std::nullopt;
    const auto min = readVec(*randomMinimum);
    const auto max = readVec(*randomMaximum);
    const auto direction = readVec(*strafeDirection);
    if (!min || !max || !direction || !std::isfinite(*radius) || !std::isfinite(*health) ||
        !std::isfinite(*strafeSpeed)) return std::nullopt;
    for (const JsonValue& channel : color->array) {
      if (channel.type != JsonValue::Type::Number || channel.number < 0.0 || channel.number > 255.0 ||
          std::floor(channel.number) != channel.number) return std::nullopt;
    }
    group.name = *groupName;
    group.visual = static_cast<AimTargetVisual>(*visual);
    group.life = static_cast<AimTargetLife>(*life);
    group.spawnMode = static_cast<AimSpawnMode>(*spawnMode);
    group.motion = static_cast<AimTargetMotion>(*motion);
    group.color = {static_cast<std::uint8_t>(color->array[0].number), static_cast<std::uint8_t>(color->array[1].number), static_cast<std::uint8_t>(color->array[2].number)};
    group.radius = static_cast<float>(*radius);
    group.count = static_cast<std::uint32_t>(*count);
    group.health = static_cast<std::int32_t>(*health);
    group.respawnDelayTicks = static_cast<std::uint32_t>(*respawn);
    group.randomMinimum = *min;
    group.randomMaximum = *max;
    group.strafeDirection = *direction;
    group.strafeSpeed = static_cast<float>(*strafeSpeed);
    group.waypointTicks = static_cast<std::uint32_t>(*waypoint);
    for (const JsonValue& spawn : spawns->array) {
      const auto parsed = readVec(spawn);
      if (!parsed) return std::nullopt;
      group.fixedSpawns.push_back(*parsed);
    }
    scenario.groups.push_back(std::move(group));
  }
  return validStoredScenario(scenario)
    ? std::optional<AimScenario>(std::move(scenario))
    : std::nullopt;
}

[[nodiscard]] JsonValue resultJson(const AimTrainerResult& result) {
  JsonValue value = JsonValue::objectValue();
  value.object["scenario_fingerprint"] = number(result.scenarioFingerprint);
  value.object["score"] = number(result.score);
  value.object["damage"] = number(result.damage);
  value.object["clears"] = number(result.clears);
  value.object["attempts"] = number(result.attempts);
  value.object["hits"] = number(result.hits);
  value.object["duration_ticks"] = number(result.durationTicks);
  value.object["seed"] = number(result.seed);
  value.object["ranked"] = JsonValue::booleanValue(result.ranked);
  return value;
}

[[nodiscard]] std::optional<AimTrainerResult> readResult(const JsonValue& value) {
  if (!requireObject(value)) return std::nullopt;
  const auto fingerprint = readNatural(value, "scenario_fingerprint");
  const auto score = readNatural(value, "score");
  const auto damage = readNatural(value, "damage");
  const auto clears = readNatural(value, "clears");
  const auto attempts = readNatural(value, "attempts");
  const auto hits = readNatural(value, "hits");
  const auto duration = readNatural(value, "duration_ticks");
  const auto seed = readNatural(value, "seed");
  const auto ranked = dev::boolMember(value, "ranked");
  if (!fingerprint || !score || !damage || !clears || !attempts || !hits || !duration || !seed || !ranked) return std::nullopt;
  return AimTrainerResult{*fingerprint, *score, *damage, static_cast<std::uint32_t>(*clears), static_cast<std::uint32_t>(*attempts), static_cast<std::uint32_t>(*hits), static_cast<std::uint32_t>(*duration), *seed, *ranked};
}

[[nodiscard]] std::optional<JsonValue> readFile(const std::filesystem::path& path, std::string& warning) {
  std::ifstream input(path);
  if (!input) {
    if (std::filesystem::exists(path)) warning = "could not read " + path.filename().string();
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const dev::JsonParseResult parsed = dev::parseJson(text);
  if (!parsed.ok) warning = path.filename().string() + ": " + parsed.error;
  else if (!requireObject(parsed.value)) warning = path.filename().string() + ": root must be an object";
  else return parsed.value;
  return std::nullopt;
}

[[nodiscard]] std::optional<JsonValue> readFileWithBackup(
  const std::filesystem::path& path,
  std::string& warning,
  bool& recovered
) {
  recovered = false;
  std::string primaryWarning;
  const auto primary = readFile(path, primaryWarning);
  if (primary) return primary;
  const std::filesystem::path backup = path.string() + ".bak";
  if (!std::filesystem::exists(backup)) {
    warning = std::move(primaryWarning);
    return std::nullopt;
  }
  std::string backupWarning;
  const auto saved = readFile(backup, backupWarning);
  if (!saved) {
    warning = primaryWarning.empty() ? std::move(backupWarning) :
      primaryWarning + "; backup: " + backupWarning;
    return std::nullopt;
  }
  recovered = true;
  warning = primaryWarning.empty()
    ? path.filename().string() + ": recovered backup"
    : primaryWarning + "; recovered backup";
  return saved;
}

[[nodiscard]] AimTrainerStoreReply writeFile(const std::filesystem::path& path, const JsonValue& value) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return {false, "could not create trainer storage: " + error.message()};
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return {false, "could not write " + path.filename().string()};
    output << dev::writeJson(value) << '\n';
    if (!output) return {false, "could not finish " + path.filename().string()};
  }
  const std::filesystem::path backup = path.string() + ".bak";
  if (std::filesystem::exists(path)) {
    std::filesystem::copy_file(
      path, backup, std::filesystem::copy_options::overwrite_existing, error
    );
    if (error) {
      std::filesystem::remove(temporary, error);
      return {false, "could not back up " + path.filename().string()};
    }
    std::filesystem::remove(path, error);
    if (error) {
      std::filesystem::remove(temporary, error);
      return {false, "could not replace " + path.filename().string()};
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::error_code ignored;
    if (std::filesystem::exists(backup)) {
      std::filesystem::copy_file(
        backup, path, std::filesystem::copy_options::overwrite_existing, ignored
      );
    }
    std::filesystem::remove(temporary, ignored);
    return {false, "could not replace " + path.filename().string()};
  }
  std::filesystem::copy_file(
    path, backup, std::filesystem::copy_options::overwrite_existing, error
  );
  if (error) return {true, "saved but could not refresh backup " + path.filename().string()};
  return {true, {}};
}

[[nodiscard]] JsonValue document(std::string_view key, JsonValue values) {
  JsonValue root = JsonValue::objectValue();
  root.object["storage_version"] = number(AimTrainerStore::kStorageVersion);
  root.object[std::string(key)] = std::move(values);
  return root;
}

} // namespace

AimTrainerStore::AimTrainerStore(std::filesystem::path preferenceRoot)
  : root_(std::move(preferenceRoot) / "aim_trainer") {}

std::filesystem::path AimTrainerStore::directory() const { return root_; }
std::filesystem::path AimTrainerStore::presetsPath() const { return root_ / "presets.json"; }
std::filesystem::path AimTrainerStore::resultsPath() const { return root_ / "results.json"; }

std::vector<AimScenario> AimTrainerStore::builtInPresets() {
  AimScenario orb;
  orb.name = "60s Orb";
  orb.groups[0].name = "Cyan orb";
  orb.groups[0].life = AimTargetLife::Invincible;
  orb.groups[0].spawnMode = AimSpawnMode::BoundedRandom;
  orb.groups[0].randomMinimum = {5.0F, -3.0F, 1.0F};
  orb.groups[0].randomMaximum = {10.0F, 3.0F, 4.0F};
  AimScenario worker = orb;
  worker.name = "60s Worker strafe";
  worker.weaponPolicy = AimWeaponPolicy::Forced;
  worker.forcedWeapon = Weapon::Railgun;
  worker.scoreMode = AimScoreMode::Clear;
  worker.groups[0].name = "Worker";
  worker.groups[0].visual = AimTargetVisual::Worker;
  worker.groups[0].life = AimTargetLife::OneHit;
  worker.groups[0].motion = AimTargetMotion::Strafe;
  worker.groups[0].strafeSpeed = 2.0F;
  worker.groups[0].count = 3;
  AimScenario air = orb;
  air.name = "60s Air";
  air.weaponPolicy = AimWeaponPolicy::Forced;
  air.forcedWeapon = Weapon::LightningGun;
  air.scoreMode = AimScoreMode::Damage;
  air.groups[0].name = "Air sphere";
  air.groups[0].visual = AimTargetVisual::Orb;
  air.groups[0].radius = 0.65F;
  air.groups[0].life = AimTargetLife::Invincible;
  air.groups[0].motion = AimTargetMotion::Air;
  air.groups[0].randomMinimum = {-11.0F, -7.0F, 1.2F};
  air.groups[0].randomMaximum = {3.0F, 7.0F, 6.0F};
  air.groups[0].strafeSpeed = 8.0F;
  air.groups[0].waypointTicks = 75U;
  air.groups[0].count = 1;
  return {air, orb, worker};
}

AimTrainerPresetList AimTrainerStore::loadPresets() const {
  AimTrainerPresetList result;
  result.presets = builtInPresets();
  std::string warning;
  bool recovered = false;
  const auto root = readFileWithBackup(presetsPath(), warning, recovered);
  if (!root) {
    result.warning = std::move(warning);
    result.safeToWrite = result.warning.empty();
    return result;
  }
  const auto version = readNatural(*root, "storage_version");
  const JsonValue* presets = root->find("presets");
  if (!version || *version != kStorageVersion || !presets || presets->type != JsonValue::Type::Array) {
    result.warning = "presets.json: unsupported storage format";
    result.safeToWrite = false;
    return result;
  }
  for (const JsonValue& item : presets->array) {
    const auto scenario = readScenario(item);
    if (!scenario) {
      result.warning = "presets.json: skipped invalid preset";
      result.safeToWrite = false;
      continue;
    }
    const auto duplicate = std::find_if(result.presets.begin(), result.presets.end(),
      [&scenario](const AimScenario& existing) { return existing.name == scenario->name; });
    if (duplicate == result.presets.end()) result.presets.push_back(*scenario);
    else *duplicate = *scenario;
  }
  if (recovered && result.warning.empty()) result.warning = std::move(warning);
  return result;
}

AimTrainerStoreReply AimTrainerStore::savePreset(const AimScenario& scenario, bool overwrite) {
  if (scenario.name.empty()) return {false, "preset name is required"};
  AimTrainerPresetList loaded = loadPresets();
  if (!loaded.safeToWrite) {
    return {false, loaded.warning + "; refusing to erase recoverable presets"};
  }
  std::vector<AimScenario> saved;
  const std::vector<AimScenario> builtins = builtInPresets();
  for (const AimScenario& current : loaded.presets) {
    const auto builtinMatch = std::find_if(builtins.begin(), builtins.end(),
      [&current](const AimScenario& value) { return current.name == value.name; });
    const bool builtin = builtinMatch != builtins.end() &&
      AimTrainer::scenarioFingerprint(current) == AimTrainer::scenarioFingerprint(*builtinMatch);
    if (!builtin) saved.push_back(current);
  }
  auto match = std::find_if(saved.begin(), saved.end(),
    [&scenario](const AimScenario& current) { return current.name == scenario.name; });
  if (match != saved.end() && !overwrite) return {false, "preset already exists"};
  if (match == saved.end()) saved.push_back(scenario); else *match = scenario;
  JsonValue values = JsonValue::arrayValue();
  for (const AimScenario& current : saved) values.array.push_back(scenarioJson(current));
  AimTrainerStoreReply reply = writeFile(presetsPath(), document("presets", std::move(values)));
  if (reply.ok && std::any_of(builtins.begin(), builtins.end(),
        [&scenario](const AimScenario& value) { return value.name == scenario.name; })) {
    reply.warning = "saved a local override of built-in preset '" + scenario.name + "'";
  }
  return reply;
}

AimTrainerStoreReply AimTrainerStore::deletePreset(const std::string& name) {
  AimTrainerPresetList loaded = loadPresets();
  if (!loaded.safeToWrite) {
    return {false, loaded.warning + "; refusing to erase recoverable presets"};
  }
  std::vector<AimScenario> saved;
  bool removed = false;
  const std::vector<AimScenario> builtins = builtInPresets();
  for (const AimScenario& scenario : loaded.presets) {
    const auto builtinMatch = std::find_if(builtins.begin(), builtins.end(),
      [&scenario](const AimScenario& value) { return scenario.name == value.name; });
    const bool builtin = builtinMatch != builtins.end() &&
      AimTrainer::scenarioFingerprint(scenario) ==
        AimTrainer::scenarioFingerprint(*builtinMatch);
    if (builtin) {
      if (scenario.name == name) return {false, "built-in presets cannot be deleted"};
      continue;
    }
    if (scenario.name == name) { removed = true; continue; }
    saved.push_back(scenario);
  }
  if (!removed) return {false, "preset was not found"};
  JsonValue values = JsonValue::arrayValue();
  for (const AimScenario& scenario : saved) values.array.push_back(scenarioJson(scenario));
  return writeFile(presetsPath(), document("presets", std::move(values)));
}

std::vector<AimTrainerResult> AimTrainerStore::leaderboard(std::uint64_t fingerprint, std::string* warning) const {
  std::string localWarning;
  bool recovered = false;
  const auto root = readFileWithBackup(resultsPath(), localWarning, recovered);
  std::vector<AimTrainerResult> results;
  if (!root) { if (warning) *warning = std::move(localWarning); return results; }
  const auto version = readNatural(*root, "storage_version");
  const JsonValue* records = root->find("results");
  if (!version || *version != kStorageVersion || !records || records->type != JsonValue::Type::Array) {
    if (warning) *warning = "results.json: unsupported storage format";
    return results;
  }
  for (const JsonValue& item : records->array) {
    const auto result = readResult(item);
    if (result && result->ranked && result->scenarioFingerprint == fingerprint) results.push_back(*result);
  }
  std::sort(results.begin(), results.end(), [](const AimTrainerResult& left, const AimTrainerResult& right) {
    if (left.score != right.score) return left.score > right.score;
    if (left.hits != right.hits) return left.hits > right.hits;
    return left.seed < right.seed;
  });
  return results;
}

AimTrainerStoreReply AimTrainerStore::recordNaturalResult(const AimTrainerResult& result) {
  if (!result.ranked) return {true, {}};
  std::string warning;
  bool recovered = false;
  const auto root = readFileWithBackup(resultsPath(), warning, recovered);
  JsonValue records = JsonValue::arrayValue();
  if (root) {
    const auto version = readNatural(*root, "storage_version");
    const JsonValue* saved = root->find("results");
    if (!version || *version != kStorageVersion || !saved || saved->type != JsonValue::Type::Array) {
      return {false, "results.json: unsupported storage format"};
    }
    records = *saved;
  } else if (!warning.empty()) {
    return {false, warning};
  }
  records.array.push_back(resultJson(result));
  return writeFile(resultsPath(), document("results", std::move(records)));
}

} // namespace lg
