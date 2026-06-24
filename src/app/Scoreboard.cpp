#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"

#include <cstdint>
#include <string>

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

} // namespace

void populateScoreboard(
  HudRenderState& hud,
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  hud.scoreboardOpen = true;
  hud.scoreboardLines.push_back("SCOREBOARD");
  hud.scoreboardLines.push_back("NAME                 SCORE   ACC   DAMAGE");
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!snapshot.connectedPlayers[index] && !snapshot.botDodgeEnabled) {
      continue;
    }

    std::string name = snapshot.playerNames[index];
    if (index == localPlayerIndex) {
      name = "> " + name;
    } else {
      name = "  " + name;
    }
    name.resize(22U, ' ');
    const RoundCombatStats& stats = snapshot.matchCombatStats[index];
    hud.scoreboardLines.push_back(
      name +
      std::to_string(snapshot.scores[index]) + "       " +
      std::to_string(accuracyPercent(stats)) + "%    " +
      std::to_string(stats.damageDealt)
    );
  }
}

} // namespace lg
