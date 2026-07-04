#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace lg {
namespace {

struct AccuracyStat {
  Weapon weapon = Weapon::LightningGun;
  std::uint32_t percent = 0;
  std::uint32_t damage = 0;
};

constexpr std::size_t kScoreboardNameTextWidth = 14;
constexpr std::size_t kScoreboardNameColumnWidth =
  2U + kScoreboardNameTextWidth;
constexpr std::size_t kNameToScoreGap = 4;
constexpr std::size_t kScoreColumnWidth = 5;
constexpr std::size_t kAccuracyColumnWidth = 7;
constexpr std::size_t kDamageColumnWidth = 6;

std::uint32_t accuracyPercent(const WeaponCombatStats& stats) {
  return stats.attempts == 0
    ? 0
    : (
        static_cast<std::uint32_t>(stats.hits) * 100U +
        (static_cast<std::uint32_t>(stats.attempts) / 2U)
      ) / static_cast<std::uint32_t>(stats.attempts);
}

std::uint32_t totalDamage(const RoundCombatStats& stats) {
  std::uint32_t damage = 0;
  for (const WeaponCombatStats& weaponStats : stats.weapons) {
    damage += weaponStats.damageDealt;
  }
  return damage;
}

AccuracyStat bestDamageWeaponAccuracy(const RoundCombatStats& stats) {
  AccuracyStat best;
  for (Weapon weapon : kWeaponSlotOrder) {
    const WeaponCombatStats& weaponStats = stats.weapons[weaponIndex(weapon)];
    if (weaponStats.damageDealt > best.damage) {
      best.weapon = weapon;
      best.percent = accuracyPercent(weaponStats);
      best.damage = weaponStats.damageDealt;
    }
  }
  if (best.damage == 0) {
    best.percent = accuracyPercent(stats.weapons[weaponIndex(best.weapon)]);
  }
  return best;
}

std::string weaponAccuracyLabel(Weapon weapon, std::uint32_t percent) {
  std::string label(weaponShortName(weapon));
  for (char& character : label) {
    character = static_cast<char>(std::toupper(
      static_cast<unsigned char>(character)
    ));
  }
  return label + " " + std::to_string(percent) + "%";
}

bool scoreboardSlotVisible(
  const ServerSnapshot& snapshot,
  std::size_t index
) {
  return snapshot.participatingPlayers[index];
}

std::string scoreboardName(
  std::string_view playerName,
  bool localPlayer
) {
  std::string name = localPlayer ? "> " : "  ";
  name += std::string(
    playerName.substr(
      0,
      std::min(playerName.size(), kScoreboardNameTextWidth)
    )
  );
  return name;
}

std::string scoreboardColumns(
  std::string_view name,
  std::string_view score,
  std::string_view accuracy,
  std::string_view damage
) {
  std::string line(name);
  line.append(
    kScoreboardNameColumnWidth -
      std::min(name.size(), kScoreboardNameColumnWidth),
    ' '
  );
  line.append(kNameToScoreGap, ' ');
  line += score;
  line.append(
    kScoreColumnWidth - std::min(score.size(), kScoreColumnWidth),
    ' '
  );
  line += ' ';
  line += accuracy;
  line.append(
    kAccuracyColumnWidth - std::min(accuracy.size(), kAccuracyColumnWidth),
    ' '
  );
  line += ' ';
  line += damage;
  line.append(
    kDamageColumnWidth - std::min(damage.size(), kDamageColumnWidth),
    ' '
  );
  return line;
}

} // namespace

void populateScoreboard(
  HudRenderState& hud,
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  hud.scoreboardOpen = true;
  hud.scoreboardLines.push_back("SCOREBOARD");
  hud.scoreboardLineTeams.push_back(Team::None);
  hud.scoreboardLineAccuracyWeapons.push_back(Weapon::LightningGun);
  hud.scoreboardLineAccuracyWeaponColumns.push_back(std::string::npos);
  hud.scoreboardLines.push_back(
    scoreboardColumns(
      "  NAME",
      snapshot.gameMode == GameMode::ClanArena ? "KILLS" : "SCORE",
      "ACC",
      "DAMAGE"
    )
  );
  hud.scoreboardLineTeams.push_back(Team::None);
  hud.scoreboardLineAccuracyWeapons.push_back(Weapon::LightningGun);
  hud.scoreboardLineAccuracyWeaponColumns.push_back(std::string::npos);
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!scoreboardSlotVisible(snapshot, index)) {
      continue;
    }

    const std::string name =
      scoreboardName(snapshot.playerNames[index], index == localPlayerIndex);
    const RoundCombatStats& stats = snapshot.matchCombatStats[index];
    const AccuracyStat accuracy = bestDamageWeaponAccuracy(stats);
    const std::string weaponAccuracy =
      weaponAccuracyLabel(accuracy.weapon, accuracy.percent);
    const std::string row = scoreboardColumns(
      name,
      std::to_string(snapshot.scores[index]),
      weaponAccuracy,
      std::to_string(totalDamage(stats))
    );
    hud.scoreboardLines.push_back(
      row
    );
    hud.scoreboardLineTeams.push_back(
      snapshot.gameMode == GameMode::ClanArena
        ? snapshot.teams[index]
        : Team::None
    );
    hud.scoreboardLineAccuracyWeapons.push_back(accuracy.weapon);
    hud.scoreboardLineAccuracyWeaponColumns.push_back(
      row.find(weaponAccuracy, kScoreboardNameColumnWidth + kNameToScoreGap)
    );
  }
}

} // namespace lg
