#include "trainer/AimTrainerEditor.hpp"

#include "shared/Constants.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace lg {
namespace {

[[nodiscard]] std::string floatText(float value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << value;
  return output.str();
}

[[nodiscard]] const char* movementName(AimPlayerMovement value) {
  return value == AimPlayerMovement::Locked ? "locked" : "normal";
}

[[nodiscard]] const char* weaponPolicyName(AimWeaponPolicy value) {
  return value == AimWeaponPolicy::All ? "all" : "forced";
}

[[nodiscard]] const char* scoreName(AimScoreMode value) {
  switch (value) {
  case AimScoreMode::Hit: return "hit";
  case AimScoreMode::Damage: return "damage";
  case AimScoreMode::Clear: return "clear";
  }
  return "hit";
}

[[nodiscard]] const char* visualName(AimTargetVisual value) {
  return value == AimTargetVisual::Orb ? "orb" : "worker";
}

[[nodiscard]] const char* lifeName(AimTargetLife value) {
  switch (value) {
  case AimTargetLife::Invincible: return "invincible";
  case AimTargetLife::OneHit: return "one hit";
  case AimTargetLife::Health: return "fixed health";
  }
  return "one hit";
}

[[nodiscard]] const char* spawnName(AimSpawnMode value) {
  return value == AimSpawnMode::FixedList ? "fixed list" : "random bounds";
}

[[nodiscard]] const char* motionName(AimTargetMotion value) {
  switch (value) {
  case AimTargetMotion::Stationary: return "stationary";
  case AimTargetMotion::Strafe: return "strafe";
  case AimTargetMotion::RandomWaypoint: return "random waypoint";
  }
  return "stationary";
}

[[nodiscard]] std::string fixedSpawnText(const std::vector<Vec3>& spawns) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2);
  for (std::size_t index = 0; index < spawns.size(); ++index) {
    if (index > 0U) output << "; ";
    output << spawns[index].x << ',' << spawns[index].y << ',' << spawns[index].z;
  }
  return output.str();
}

template <typename T>
[[nodiscard]] bool parseInteger(std::string_view text, T& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

[[nodiscard]] bool parseFloat(std::string_view text, float& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(value);
}

[[nodiscard]] bool parseFixedSpawns(
  std::string_view text,
  std::vector<Vec3>& spawns
) {
  spawns.clear();
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t separator = text.find(';', start);
    std::string item(text.substr(start, separator == std::string_view::npos
      ? text.size() - start : separator - start));
    std::replace(item.begin(), item.end(), ',', ' ');
    std::istringstream input(item);
    Vec3 value;
    if (!(input >> value.x >> value.y >> value.z) ||
        !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
      return false;
    }
    std::string extra;
    if (input >> extra) return false;
    spawns.push_back(value);
    if (separator == std::string_view::npos) break;
    start = separator + 1U;
  }
  return !spawns.empty();
}

} // namespace

AimTrainerEditor::AimTrainerEditor(AimTrainerMenu& menu) : menu_(menu) {}

bool AimTrainerEditor::open() const { return open_; }
void AimTrainerEditor::setOpen(bool open) {
  open_ = open;
  if (!open) cancelText();
}
std::size_t AimTrainerEditor::selectedRow() const { return selectedRow_; }
std::size_t AimTrainerEditor::scrollRows() const { return scrollRows_; }
void AimTrainerEditor::setScrollRows(std::size_t rows) { scrollRows_ = rows; }
bool AimTrainerEditor::editingText() const { return textRow_.has_value(); }
const std::string& AimTrainerEditor::textInput() const { return textInput_; }
const std::string& AimTrainerEditor::message() const { return message_; }

std::vector<AimTrainerEditorRow> AimTrainerEditor::rows() const {
  std::vector<AimTrainerEditorRow> result;
  const AimScenario& draft = menu_.draft();
  const AimTrainerFrame& frame = menu_.frame();
  const bool hasGroup = !draft.groups.empty();
  const std::size_t groupIndex = hasGroup
    ? std::min(menu_.selectedGroupIndex(), draft.groups.size() - 1U)
    : 0U;
  const AimTargetGroup fallbackGroup;
  const AimTargetGroup& group = hasGroup ? draft.groups[groupIndex] : fallbackGroup;
  const auto add = [&result](
    AimTrainerEditorField field,
    std::string label,
    std::string value,
    bool command = false,
    bool editable = false,
    std::uint8_t component = 0U
  ) {
    result.push_back({field, component, std::move(label), std::move(value), command, editable});
  };

  add(AimTrainerEditorField::Preset, "Preset", draft.name + "  (" +
    std::to_string(menu_.selectedPresetIndex() + 1U) + "/" +
    std::to_string(menu_.presets().size()) + ")");
  add(AimTrainerEditorField::PresetName, "Preset name", draft.name, false, true);
  add(AimTrainerEditorField::SaveAs, "Save named copy", "ENTER", true);
  add(AimTrainerEditorField::Overwrite, "Save / overwrite", "ENTER", true);
  add(AimTrainerEditorField::DeletePreset, "Delete local preset", "ENTER", true);
  add(AimTrainerEditorField::Start,
    frame.phase == AimTrainerPhase::Results ? "Repeat run" : "Start run", "ENTER", true);
  add(AimTrainerEditorField::Abort, "Abort run", "ENTER", true);
  add(AimTrainerEditorField::Duration, "Duration (ticks)",
    std::to_string(draft.durationTicks) + " / " +
      std::to_string(draft.durationTicks / kFixedTickRate) + "s", false, true);
  add(AimTrainerEditorField::PlayerMovement, "Player movement", movementName(draft.playerMovement));
  add(AimTrainerEditorField::WeaponPolicy, "Weapon mode", weaponPolicyName(draft.weaponPolicy));
  add(AimTrainerEditorField::ForcedWeapon, "Forced weapon",
    std::string(weaponShortName(draft.forcedWeapon)));
  add(AimTrainerEditorField::InfiniteAmmo, "Infinite ammo", draft.infiniteAmmo ? "yes" : "no");
  add(AimTrainerEditorField::ScoreMode, "Score mode", scoreName(draft.scoreMode));
  add(AimTrainerEditorField::HitScore, "Score per hit", std::to_string(draft.hitScore), false, true);
  add(AimTrainerEditorField::DamageScore, "Score per damage", std::to_string(draft.damageScorePerPoint), false, true);
  add(AimTrainerEditorField::ClearScore, "Score per clear", std::to_string(draft.clearScore), false, true);
  add(AimTrainerEditorField::Seed, "Seed", std::to_string(draft.seed), false, true);
  for (std::size_t index = 0; index < kWeaponCount; ++index) {
    add(AimTrainerEditorField::AllowedWeapon,
      "Allow " + std::string(weaponShortName(static_cast<Weapon>(index))),
      draft.allowedWeapons[index] ? "yes" : "no", false, false,
      static_cast<std::uint8_t>(index));
  }
  add(AimTrainerEditorField::MapIdentity, "Map ID", std::to_string(draft.mapIdentity), true);
  add(AimTrainerEditorField::BalanceIdentity, "Balance ID", std::to_string(draft.balanceIdentity), true);
  add(AimTrainerEditorField::TargetCap, "Target render limit",
    std::to_string(AimScenario::kMaxTargets) + " (all valid targets)", true);
  add(AimTrainerEditorField::Group, "Selected target group",
    group.name + "  (" + std::to_string(groupIndex + 1U) + "/" +
      std::to_string(draft.groups.size()) + ")");
  add(AimTrainerEditorField::AddGroup, "Add target group", "ENTER", true);
  add(AimTrainerEditorField::RemoveGroup, "Remove target group", "ENTER", true);
  add(AimTrainerEditorField::GroupName, "Group name", group.name, false, true);
  add(AimTrainerEditorField::Visual, "Target visual", visualName(group.visual));
  add(AimTrainerEditorField::Count, "Target count", std::to_string(group.count), false, true);
  add(AimTrainerEditorField::Radius, "Target radius", floatText(group.radius), false, true);
  add(AimTrainerEditorField::Color, "Target red", std::to_string(group.color.red), false, true, 0U);
  add(AimTrainerEditorField::Color, "Target green", std::to_string(group.color.green), false, true, 1U);
  add(AimTrainerEditorField::Color, "Target blue", std::to_string(group.color.blue), false, true, 2U);
  add(AimTrainerEditorField::Life, "Target life", lifeName(group.life));
  add(AimTrainerEditorField::Health, "Target health", std::to_string(group.health), false, true);
  add(AimTrainerEditorField::RespawnDelay, "Respawn delay (ticks)",
    std::to_string(group.respawnDelayTicks), false, true);
  add(AimTrainerEditorField::SpawnMode, "Spawn mode", spawnName(group.spawnMode));
  add(AimTrainerEditorField::FixedSpawns, "Fixed spawns x,y,z;...",
    fixedSpawnText(group.fixedSpawns), false, true);
  static constexpr const char* axes[] = {"X", "Y", "Z"};
  const float minimum[] = {group.randomMinimum.x, group.randomMinimum.y, group.randomMinimum.z};
  const float maximum[] = {group.randomMaximum.x, group.randomMaximum.y, group.randomMaximum.z};
  const float direction[] = {group.strafeDirection.x, group.strafeDirection.y, group.strafeDirection.z};
  for (std::uint8_t axis = 0; axis < 3U; ++axis) {
    add(AimTrainerEditorField::RandomMinimum, "Random min " + std::string(axes[axis]),
      floatText(minimum[axis]), false, true, axis);
    add(AimTrainerEditorField::RandomMaximum, "Random max " + std::string(axes[axis]),
      floatText(maximum[axis]), false, true, axis);
  }
  add(AimTrainerEditorField::Motion, "Target motion", motionName(group.motion));
  add(AimTrainerEditorField::StrafeSpeed, "Motion speed", floatText(group.strafeSpeed), false, true);
  for (std::uint8_t axis = 0; axis < 3U; ++axis) {
    add(AimTrainerEditorField::StrafeDirection, "Strafe dir " + std::string(axes[axis]),
      floatText(direction[axis]), false, true, axis);
  }
  add(AimTrainerEditorField::WaypointInterval, "Waypoint interval (ticks)",
    std::to_string(group.waypointTicks), false, true);
  add(AimTrainerEditorField::Result, "Last result",
    "score " + std::to_string(frame.result.score) + "  hits " +
      std::to_string(frame.result.hits) + "/" + std::to_string(frame.result.attempts) +
      "  damage " + std::to_string(frame.result.damage), true);
  if (menu_.leaderboard().empty()) {
    add(AimTrainerEditorField::Leaderboard, "Local leaderboard", "no saved runs", true);
  } else {
    for (std::size_t index = 0; index < menu_.leaderboard().size(); ++index) {
      const AimTrainerResult& saved = menu_.leaderboard()[index];
      add(AimTrainerEditorField::Leaderboard,
        "Local #" + std::to_string(index + 1U),
        "score " + std::to_string(saved.score) + "  hits " +
          std::to_string(saved.hits) + "/" + std::to_string(saved.attempts),
        true, false, static_cast<std::uint8_t>(std::min<std::size_t>(index, 255U)));
    }
  }
  return result;
}

void AimTrainerEditor::selectRow(std::size_t row) {
  selectedRow_ = row;
  clampSelection();
}

void AimTrainerEditor::moveSelection(int amount) {
  const std::vector<AimTrainerEditorRow> current = rows();
  if (current.empty()) return;
  const int count = static_cast<int>(current.size());
  int next = static_cast<int>(selectedRow_) + amount;
  next %= count;
  if (next < 0) next += count;
  selectedRow_ = static_cast<std::size_t>(next);
}

bool AimTrainerEditor::adjustSelected(int direction) {
  const std::vector<AimTrainerEditorRow> current = rows();
  if (selectedRow_ >= current.size()) return false;
  return adjust(current[selectedRow_], direction < 0 ? -1 : 1);
}

bool AimTrainerEditor::activateSelected() {
  const std::vector<AimTrainerEditorRow> current = rows();
  if (selectedRow_ >= current.size()) return false;
  return activate(current[selectedRow_]);
}

void AimTrainerEditor::insertText(std::string_view text) {
  if (!textRow_) return;
  if (textInput_.size() >= 256U) return;
  textInput_.append(text.substr(
    0,
    std::min<std::size_t>(text.size(), 256U - textInput_.size())
  ));
}

void AimTrainerEditor::backspace() {
  if (!textRow_ || textInput_.empty()) return;
  std::size_t begin = textInput_.size() - 1U;
  while (
    begin > 0U &&
    (static_cast<unsigned char>(textInput_[begin]) & 0xC0U) == 0x80U
  ) {
    --begin;
  }
  textInput_.erase(begin);
}

bool AimTrainerEditor::commitText() {
  if (!textRow_) return false;
  const AimTrainerEditorRow row = *textRow_;
  const bool applied = applyText(row, textInput_);
  if (applied) {
    textRow_.reset();
    textInput_.clear();
  }
  return applied;
}

void AimTrainerEditor::cancelText() {
  textRow_.reset();
  textInput_.clear();
}

bool AimTrainerEditor::adjust(const AimTrainerEditorRow& row, int direction) {
  if (menu_.frame().phase == AimTrainerPhase::Running) return false;
  if (row.field == AimTrainerEditorField::Preset) {
    const std::size_t count = menu_.presets().size();
    if (count == 0U) return false;
    const std::size_t current = menu_.selectedPresetIndex();
    return menu_.selectPreset(direction < 0
      ? (current + count - 1U) % count : (current + 1U) % count);
  }
  if (row.field == AimTrainerEditorField::Group) {
    const std::size_t count = menu_.draft().groups.size();
    if (count == 0U) return false;
    const std::size_t current = menu_.selectedGroupIndex();
    return menu_.selectGroup(direction < 0
      ? (current + count - 1U) % count : (current + 1U) % count);
  }

  AimScenario draft = menu_.draft();
  if (draft.groups.empty()) draft.groups.push_back(AimTargetGroup{});
  const std::size_t groupIndex = std::min(
    menu_.selectedGroupIndex(), draft.groups.size() - 1U
  );
  AimTargetGroup& group = draft.groups[groupIndex];
  switch (row.field) {
  case AimTrainerEditorField::PlayerMovement:
    draft.playerMovement = draft.playerMovement == AimPlayerMovement::Locked
      ? AimPlayerMovement::Normal : AimPlayerMovement::Locked; break;
  case AimTrainerEditorField::WeaponPolicy:
    draft.weaponPolicy = draft.weaponPolicy == AimWeaponPolicy::All
      ? AimWeaponPolicy::Forced : AimWeaponPolicy::All; break;
  case AimTrainerEditorField::ForcedWeapon:
    draft.forcedWeapon = static_cast<Weapon>((
      static_cast<int>(weaponIndex(draft.forcedWeapon)) +
      static_cast<int>(kWeaponCount) + direction
    ) % static_cast<int>(kWeaponCount)); break;
  case AimTrainerEditorField::InfiniteAmmo: draft.infiniteAmmo = !draft.infiniteAmmo; break;
  case AimTrainerEditorField::ScoreMode:
    draft.scoreMode = static_cast<AimScoreMode>(
      (static_cast<int>(draft.scoreMode) + 3 + direction) % 3); break;
  case AimTrainerEditorField::AllowedWeapon:
    if (row.component >= draft.allowedWeapons.size()) return false;
    draft.allowedWeapons[row.component] = !draft.allowedWeapons[row.component]; break;
  case AimTrainerEditorField::Visual:
    group.visual = group.visual == AimTargetVisual::Orb
      ? AimTargetVisual::Worker : AimTargetVisual::Orb; break;
  case AimTrainerEditorField::Life:
    group.life = static_cast<AimTargetLife>(
      (static_cast<int>(group.life) + 3 + direction) % 3); break;
  case AimTrainerEditorField::SpawnMode:
    group.spawnMode = group.spawnMode == AimSpawnMode::FixedList
      ? AimSpawnMode::BoundedRandom : AimSpawnMode::FixedList; break;
  case AimTrainerEditorField::Motion:
    group.motion = static_cast<AimTargetMotion>(
      (static_cast<int>(group.motion) + 3 + direction) % 3); break;
  case AimTrainerEditorField::Duration:
    draft.durationTicks = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(draft.durationTicks) + direction * kFixedTickRate,
      1, 450000)); break;
  case AimTrainerEditorField::HitScore:
    draft.hitScore = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(draft.hitScore) + direction, 1, 1000000)); break;
  case AimTrainerEditorField::DamageScore:
    draft.damageScorePerPoint = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(draft.damageScorePerPoint) + direction, 1, 1000000)); break;
  case AimTrainerEditorField::ClearScore:
    draft.clearScore = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(draft.clearScore) + direction, 1, 1000000)); break;
  case AimTrainerEditorField::Count:
    group.count = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(group.count) + direction, 1,
      AimScenario::kMaxTargetsPerGroup)); break;
  case AimTrainerEditorField::Radius:
    group.radius = std::clamp(group.radius + direction * 0.05F, 0.05F, 5.0F); break;
  case AimTrainerEditorField::Color: {
    if (row.component >= 3U) return false;
    std::uint8_t* channels[] = {&group.color.red, &group.color.green, &group.color.blue};
    *channels[row.component] = static_cast<std::uint8_t>(std::clamp(
      static_cast<int>(*channels[row.component]) + direction * 5, 0, 255));
    break;
  }
  case AimTrainerEditorField::Health:
    group.health = std::clamp(group.health + direction * 10, 1, 100000); break;
  case AimTrainerEditorField::RespawnDelay:
    group.respawnDelayTicks = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(group.respawnDelayTicks) + direction, 0, 450000)); break;
  case AimTrainerEditorField::StrafeSpeed:
    group.strafeSpeed = std::clamp(group.strafeSpeed + direction * 0.1F, 0.0F, 100.0F); break;
  case AimTrainerEditorField::WaypointInterval:
    group.waypointTicks = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(group.waypointTicks) + direction, 1, 450000)); break;
  default: return false;
  }
  menu_.edit(std::move(draft));
  return true;
}

bool AimTrainerEditor::activate(const AimTrainerEditorRow& row) {
  if (row.editable && !row.command) {
    beginText(row);
    return true;
  }
  switch (row.field) {
  case AimTrainerEditorField::SaveAs: {
    const AimTrainerStoreReply reply = menu_.saveAs(menu_.draft().name);
    message_ = reply.ok ? "Preset saved" : reply.warning;
    return reply.ok;
  }
  case AimTrainerEditorField::Overwrite: {
    const AimTrainerStoreReply reply = menu_.overwrite();
    message_ = reply.ok ? (reply.warning.empty() ? "Preset saved" : reply.warning) : reply.warning;
    return reply.ok;
  }
  case AimTrainerEditorField::DeletePreset: {
    const AimTrainerStoreReply reply = menu_.deleteSelected();
    message_ = reply.ok ? "Preset deleted" : reply.warning;
    return reply.ok;
  }
  case AimTrainerEditorField::Start: {
    const AimTrainerArmResult started = menu_.frame().phase == AimTrainerPhase::Results
      ? menu_.repeat() : menu_.start();
    message_ = started.ok ? "Run started" : started.error;
    if (started.ok) open_ = false;
    return started.ok;
  }
  case AimTrainerEditorField::Abort:
    menu_.abort(); message_ = "Run aborted"; return true;
  case AimTrainerEditorField::AddGroup:
    return menu_.addGroup();
  case AimTrainerEditorField::RemoveGroup:
    return menu_.removeSelectedGroup();
  default:
    return adjust(row, 1);
  }
}

bool AimTrainerEditor::applyText(
  const AimTrainerEditorRow& row,
  std::string_view text
) {
  if (menu_.frame().phase == AimTrainerPhase::Running) return false;
  AimScenario draft = menu_.draft();
  if (draft.groups.empty()) draft.groups.push_back(AimTargetGroup{});
  const std::size_t groupIndex = std::min(
    menu_.selectedGroupIndex(), draft.groups.size() - 1U
  );
  AimTargetGroup& group = draft.groups[groupIndex];
  std::uint64_t natural = 0;
  std::int32_t integer = 0;
  float real = 0.0F;
  bool ok = true;
  switch (row.field) {
  case AimTrainerEditorField::PresetName:
    if (text.empty()) ok = false; else draft.name = std::string(text); break;
  case AimTrainerEditorField::GroupName:
    if (text.empty()) ok = false; else group.name = std::string(text); break;
  case AimTrainerEditorField::Duration:
    ok = parseInteger(text, natural) && natural >= 1U && natural <= 450000U;
    if (ok) draft.durationTicks = static_cast<std::uint32_t>(natural);
    break;
  case AimTrainerEditorField::HitScore:
  case AimTrainerEditorField::DamageScore:
  case AimTrainerEditorField::ClearScore:
    ok = parseInteger(text, natural) && natural >= 1U && natural <= 1000000U;
    if (ok && row.field == AimTrainerEditorField::HitScore) draft.hitScore = static_cast<std::uint32_t>(natural);
    if (ok && row.field == AimTrainerEditorField::DamageScore) draft.damageScorePerPoint = static_cast<std::uint32_t>(natural);
    if (ok && row.field == AimTrainerEditorField::ClearScore) draft.clearScore = static_cast<std::uint32_t>(natural);
    break;
  case AimTrainerEditorField::Seed:
    ok = parseInteger(text, natural); if (ok) draft.seed = natural; break;
  case AimTrainerEditorField::Count:
    ok = parseInteger(text, natural) && natural >= 1U &&
      natural <= AimScenario::kMaxTargetsPerGroup;
    if (ok) group.count = static_cast<std::uint32_t>(natural);
    break;
  case AimTrainerEditorField::Radius:
    ok = parseFloat(text, real) && real >= 0.05F && real <= 5.0F;
    if (ok) group.radius = real;
    break;
  case AimTrainerEditorField::Color:
    ok = row.component < 3U && parseInteger(text, natural) && natural <= 255U;
    if (ok) {
      std::uint8_t* channels[] = {&group.color.red, &group.color.green, &group.color.blue};
      *channels[row.component] = static_cast<std::uint8_t>(natural);
    }
    break;
  case AimTrainerEditorField::Health:
    ok = parseInteger(text, integer) && integer >= 1 && integer <= 100000;
    if (ok) group.health = integer;
    break;
  case AimTrainerEditorField::RespawnDelay:
    ok = parseInteger(text, natural) && natural <= 450000U;
    if (ok) group.respawnDelayTicks = static_cast<std::uint32_t>(natural);
    break;
  case AimTrainerEditorField::FixedSpawns: {
    std::vector<Vec3> parsed;
    ok = parseFixedSpawns(text, parsed);
    if (ok) group.fixedSpawns = std::move(parsed);
    break;
  }
  case AimTrainerEditorField::RandomMinimum:
  case AimTrainerEditorField::RandomMaximum:
  case AimTrainerEditorField::StrafeDirection: {
    ok = row.component < 3U && parseFloat(text, real);
    if (ok) {
      Vec3* value = row.field == AimTrainerEditorField::RandomMinimum
        ? &group.randomMinimum : row.field == AimTrainerEditorField::RandomMaximum
          ? &group.randomMaximum : &group.strafeDirection;
      float* components[] = {&value->x, &value->y, &value->z};
      *components[row.component] = real;
    }
    break;
  }
  case AimTrainerEditorField::StrafeSpeed:
    ok = parseFloat(text, real) && real >= 0.0F && real <= 100.0F;
    if (ok) group.strafeSpeed = real;
    break;
  case AimTrainerEditorField::WaypointInterval:
    ok = parseInteger(text, natural) && natural >= 1U && natural <= 450000U;
    if (ok) group.waypointTicks = static_cast<std::uint32_t>(natural);
    break;
  default: ok = false; break;
  }
  if (!ok) {
    message_ = "Invalid value for " + row.label;
    return false;
  }
  menu_.edit(std::move(draft));
  message_ = row.label + " updated";
  return true;
}

void AimTrainerEditor::beginText(const AimTrainerEditorRow& row) {
  textRow_ = row;
  textInput_ = row.value;
  if (row.field == AimTrainerEditorField::Duration) {
    textInput_ = std::to_string(menu_.draft().durationTicks);
  }
}

void AimTrainerEditor::clampSelection() {
  const std::size_t count = rows().size();
  selectedRow_ = count == 0U ? 0U : std::min(selectedRow_, count - 1U);
}

} // namespace lg
