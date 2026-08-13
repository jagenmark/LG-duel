#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

struct RankedFreeForAllSlot {
  std::size_t playerIndex = 0;
  std::size_t rank = 0;
};

std::string clipUtf8(std::string_view text, std::size_t maximumBytes) {
  std::size_t end = 0;
  while (end < text.size() && end < maximumBytes) {
    const unsigned char first = static_cast<unsigned char>(text[end]);
    std::size_t bytes = 1;
    if ((first & 0xE0U) == 0xC0U) {
      bytes = 2;
    } else if ((first & 0xF0U) == 0xE0U) {
      bytes = 3;
    } else if ((first & 0xF8U) == 0xF0U) {
      bytes = 4;
    }
    if (end + bytes > text.size() || end + bytes > maximumBytes) {
      break;
    }
    end += bytes;
  }
  return std::string(text.substr(0, end));
}

std::vector<RankedFreeForAllSlot> rankedFreeForAllSlots(
  const ServerSnapshot& snapshot
) {
  std::vector<std::size_t> slots;
  slots.reserve(kDuelPlayerCount);
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (snapshot.participatingPlayers[index]) {
      slots.push_back(index);
    }
  }
  std::sort(slots.begin(), slots.end(), [&snapshot](
    std::size_t left,
    std::size_t right
  ) {
    if (snapshot.scores[left] != snapshot.scores[right]) {
      return snapshot.scores[left] > snapshot.scores[right];
    }
    return left < right;
  });

  std::vector<RankedFreeForAllSlot> ranked;
  ranked.reserve(slots.size());
  for (std::size_t order = 0; order < slots.size(); ++order) {
    const std::size_t rank = order == 0U
      ? 1U
      : snapshot.scores[slots[order]] == snapshot.scores[slots[order - 1U]]
        ? ranked.back().rank
        : order + 1U;
    ranked.push_back({slots[order], rank});
  }
  return ranked;
}

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
  name += clipUtf8(playerName, kScoreboardNameTextWidth);
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
  if (snapshot.gameMode == GameMode::FreeForAll) {
    hud.freeForAllScoreboard = true;
    for (const RankedFreeForAllSlot& ranked : rankedFreeForAllSlots(snapshot)) {
      const std::size_t index = ranked.playerIndex;
      const RoundCombatStats& stats = snapshot.matchCombatStats[index];
      const AccuracyStat accuracy = bestDamageWeaponAccuracy(stats);
      hud.freeForAllScoreboardRows.push_back({
        ranked.rank,
        static_cast<std::uint8_t>(index),
        clipUtf8(snapshot.playerNames[index], kScoreboardNameTextWidth),
        snapshot.scores[index],
        accuracy.weapon,
        accuracy.percent,
        totalDamage(stats),
        index == localPlayerIndex,
      });
    }
    return;
  }
  hud.scoreboardLines.push_back("SCOREBOARD");
  hud.scoreboardLineTeams.push_back(Team::None);
  hud.scoreboardLineAccuracyWeapons.push_back(Weapon::LightningGun);
  hud.scoreboardLineAccuracyWeaponColumns.push_back(std::string::npos);
  hud.scoreboardLines.push_back(
    scoreboardColumns(
      "  NAME",
      snapshot.gameMode == GameMode::ClanArena ? "KILLS" :
        snapshot.gameMode == GameMode::McGuffin ? "TEAM" : "SCORE",
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
      snapshot.gameMode == GameMode::McGuffin
        ? std::to_string(snapshot.mcguffinScores[
            snapshot.teams[index] == Team::Blue ? 1U : 0U
          ])
        : std::to_string(snapshot.scores[index]),
      weaponAccuracy,
      std::to_string(totalDamage(stats))
    );
    hud.scoreboardLines.push_back(
      row
    );
    hud.scoreboardLineTeams.push_back(
      snapshot.gameMode != GameMode::Duel
        ? snapshot.teams[index]
        : Team::None
    );
    hud.scoreboardLineAccuracyWeapons.push_back(accuracy.weapon);
    hud.scoreboardLineAccuracyWeaponColumns.push_back(
      row.find(weaponAccuracy, kScoreboardNameColumnWidth + kNameToScoreGap)
    );
  }
}

void populateFreeForAllStanding(
  HudRenderState& hud,
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  if (snapshot.gameMode != GameMode::FreeForAll) {
    return;
  }
  const std::vector<RankedFreeForAllSlot> ranked =
    rankedFreeForAllSlots(snapshot);
  if (ranked.empty()) {
    return;
  }

  const auto append = [&](const RankedFreeForAllSlot& slot) {
    const std::size_t index = slot.playerIndex;
    hud.freeForAllStandingRows.push_back({
      slot.rank,
      static_cast<std::uint8_t>(index),
      clipUtf8(snapshot.playerNames[index], kScoreboardNameTextWidth),
      snapshot.scores[index],
      index == localPlayerIndex,
    });
  };
  append(ranked.front());
  if (
    localPlayerIndex >= kDuelPlayerCount ||
    localPlayerIndex == ranked.front().playerIndex ||
    !snapshot.participatingPlayers[localPlayerIndex]
  ) {
    return;
  }
  const auto local = std::find_if(
    ranked.begin(),
    ranked.end(),
    [localPlayerIndex](const RankedFreeForAllSlot& slot) {
      return slot.playerIndex == localPlayerIndex;
    }
  );
  if (local != ranked.end()) {
    append(*local);
  }
}

} // namespace lg
