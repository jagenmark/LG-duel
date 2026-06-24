#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace lg {
namespace {

std::uint32_t accuracyPercent(const RoundCombatStats& stats) {
  return stats.lightningActiveTicks == 0
    ? 0
    : (
        stats.lightningHitTicks * 100U +
        (stats.lightningActiveTicks / 2U)
      ) / stats.lightningActiveTicks;
}

bool scoreboardSlotVisible(
  const ServerSnapshot& snapshot,
  std::size_t index
) {
  return snapshot.participatingPlayers[index];
}

std::size_t scoreboardNameWidth(const ServerSnapshot& snapshot) {
  std::size_t width = std::string_view("  NAME").size();
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (scoreboardSlotVisible(snapshot, index)) {
      width = std::max(width, snapshot.playerNames[index].size() + 2U);
    }
  }
  return width;
}

std::string centered(std::string_view value, std::size_t width) {
  if (value.size() >= width) {
    return std::string(value);
  }

  const std::size_t padding = width - value.size();
  const std::size_t leftPadding = padding / 2U;
  return std::string(leftPadding, ' ') + std::string(value) +
    std::string(padding - leftPadding, ' ');
}

std::string scoreboardColumns(
  std::string_view name,
  std::string_view score,
  std::string_view accuracy,
  std::string_view damage,
  std::size_t nameWidth
) {
  std::string line(name);
  line.append(nameWidth - name.size(), ' ');
  line += ' ';
  line += centered(score, 6U);
  line += ' ';
  line += centered(accuracy, 6U);
  line += ' ';
  line += centered(damage, 6U);
  return line;
}

} // namespace

void populateScoreboard(
  HudRenderState& hud,
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  const std::size_t nameWidth = scoreboardNameWidth(snapshot);
  hud.scoreboardOpen = true;
  hud.scoreboardLines.push_back("SCOREBOARD");
  hud.scoreboardLineTeams.push_back(Team::None);
  hud.scoreboardLines.push_back(
    scoreboardColumns(
      "  NAME",
      snapshot.gameMode == GameMode::ClanArena ? "KILLS" : "SCORE",
      "ACC",
      "DAMAGE",
      nameWidth
    )
  );
  hud.scoreboardLineTeams.push_back(Team::None);
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!scoreboardSlotVisible(snapshot, index)) {
      continue;
    }

    std::string name = snapshot.playerNames[index];
    if (index == localPlayerIndex) {
      name = "> " + name;
    } else {
      name = "  " + name;
    }
    const RoundCombatStats& stats = snapshot.matchCombatStats[index];
    hud.scoreboardLines.push_back(
      scoreboardColumns(
        name,
        std::to_string(snapshot.scores[index]),
        std::to_string(accuracyPercent(stats)) + "%",
        std::to_string(stats.damageDealt),
        nameWidth
      )
    );
    hud.scoreboardLineTeams.push_back(
      snapshot.gameMode == GameMode::ClanArena
        ? snapshot.teams[index]
        : Team::None
    );
  }
}

} // namespace lg
