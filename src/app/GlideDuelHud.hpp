#pragma once

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"
#include <optional>

namespace lg {

inline void populateGlideDuelHud(
  HudRenderState& hud, const ServerSnapshot& snapshot,
  std::optional<std::size_t> subject, bool spectator
) {
  auto& panel = hud.glideDuel;
  panel = {};
  if (snapshot.gameMode != GameMode::GlideDuel) return;
  panel.active = true;
  panel.showHealth = subject.has_value() && *subject < 2;
  const std::size_t left = subject.value_or(0) < 2 ? subject.value_or(0) : 0;
  const std::size_t right = 1 - left;
  panel.leftName = subject && !spectator ? "YOU" : snapshot.playerNames[left];
  panel.rightName = snapshot.playerNames[right];
  if (panel.leftName.empty()) panel.leftName = "PLAYER 1";
  if (panel.rightName.empty()) panel.rightName = "WAITING";
  panel.leftScore = snapshot.scores[left];
  panel.rightScore = snapshot.scores[right];
  panel.target = 7;
  panel.readiness = snapshot.matchPhase == MatchPhase::WaitingForReady;
  panel.leftReady = snapshot.readyPlayers[left];
  panel.rightReady = snapshot.readyPlayers[right];
  const bool leftPoint = panel.leftScore == panel.target - 1;
  const bool rightPoint = panel.rightScore == panel.target - 1;
  if (leftPoint || rightPoint) {
    panel.stakes = leftPoint && rightPoint ? "DECIDING ROUND" : "MATCH POINT";
  }
  const auto round = std::max(1U, snapshot.glide.roundNumber);
  const auto seconds = (snapshot.phaseTicksRemaining + 124U) / 125U;
  panel.status = "ROUND " + std::to_string(round);
  switch (snapshot.matchPhase) {
  case MatchPhase::WaitingForPlayers:
    panel.status = "WAITING FOR OPPONENT";
    panel.hint = "CROUCH TO SLIDE - JUMP TO CARRY SPEED";
    break;
  case MatchPhase::WaitingForReady:
    panel.status = "READY CHECK";
    panel.hint = !subject || spectator ? "WAITING FOR PLAYERS TO READY"
      : snapshot.readyPlayers[left] ? "WAITING FOR OPPONENT TO READY" : "PRESS F3 TO READY";
    break;
  case MatchPhase::Countdown:
    panel.status += "  -  STARTING IN " + std::to_string(seconds);
    panel.hint = std::string(glideConditionName(glideCondition(round))) + " - " +
      (glideCondition(round) == GlideCondition::Float ? "30% LOWER GRAVITY" :
       glideCondition(round) == GlideCondition::Blast ? "50% MORE KNOCKBACK" : "40% SHORTER DASH COOLDOWN");
    break;
  case MatchPhase::Live: {
    panel.roundFraction = std::clamp(static_cast<float>(snapshot.phaseTicksRemaining) / kGlideRoundTicks, 0.0F, 1.0F);
    panel.timer = std::to_string(seconds / 60) + ':' + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
    panel.overdrive = snapshot.phaseTicksRemaining <= kGlideOverdriveTicks;
    panel.status += panel.overdrive ? "  -  OVERDRIVE" : "  -  " + std::string(glideConditionName(glideCondition(round)));
    panel.outsideZone = snapshot.glide.arenaReady && panel.overdrive && panel.showHealth &&
      glideOutsideZone(snapshot.glide, snapshot.phaseTicksRemaining, snapshot.players[left].position);
    panel.hint = panel.outsideZone ? "OUTSIDE ZONE - RETURN TO THE RING" :
      panel.overdrive ? (snapshot.glide.arenaReady ? "2X DAMAGE - STAY INSIDE THE RING" : "2X DAMAGE - MOST HEALTH WINS") : "MOST HEALTH WINS AT TIME";
    if (const int wave = glidePickupWave(snapshot.glide, snapshot.phaseTicksRemaining, true); wave >= 0) {
      const auto powerup = glideWavePowerup(round, wave);
      const auto elapsed = glideElapsedTicks(snapshot.phaseTicksRemaining);
      const auto spawn = kGlidePickupSpawnTicks[wave];
      panel.pickup = std::string(glidePowerupName(powerup)) + (elapsed < spawn
        ? " IN " + std::to_string((spawn - elapsed + 124U) / 125U) + "s"
        : " LIVE - " + std::string(powerup == GlidePowerup::Surge ? "+35% DAMAGE" :
          powerup == GlidePowerup::Guard ? "35% DAMAGE RESIST" : "+25 HEALTH"));
      panel.pickupColor = powerup == GlidePowerup::Surge ? RenderColor{255, 181, 72, 255} :
        powerup == GlidePowerup::Guard ? RenderColor{106, 185, 255, 255} : RenderColor{104, 239, 155, 255};
    }
    if (!snapshot.glide.arenaReady) panel.pickup = "NO SHARED POWER-UP AREA";
    const auto buffLabel = [&](std::size_t index) {
      return std::string(glidePowerupName(snapshot.glide.buffs[index])) + " " +
        std::to_string((snapshot.glide.buffTicks[index] + 124U) / 125U) + "s";
    };
    if (panel.showHealth && snapshot.glide.buffTicks[left]) panel.buff = std::string(spectator ? "FOLLOWED: " : "YOU: ") + buffLabel(left);
    if (snapshot.glide.announcementTicks && snapshot.glide.lastCollector < 2) {
      panel.pickup = std::string(snapshot.glide.lastCollector == left && panel.showHealth && !spectator ? "YOU TOOK " : "TAKEN: ") +
        std::string(glidePowerupName(snapshot.glide.lastPickup));
      panel.pickupColor = snapshot.glide.lastPickup == GlidePowerup::Surge ? RenderColor{255, 181, 72, 255} :
        snapshot.glide.lastPickup == GlidePowerup::Guard ? RenderColor{106, 185, 255, 255} : RenderColor{104, 239, 155, 255};
    }
    break;
  }
  case MatchPhase::RoundEnd:
    panel.status += snapshot.roundWinner == 255 ? "  -  DRAW"
      : snapshot.roundWinner == left ? "  -  ROUND WON" : "  -  ROUND LOST";
    if (spectator && snapshot.roundWinner < 2) {
      panel.status = "ROUND " + std::to_string(round) + "  -  " +
        snapshot.playerNames[snapshot.roundWinner].substr(0, 12) + " WON";
    }
    panel.hint = snapshot.roundWinner == 255 ?
      std::string(snapshot.players[left].health <= 0 && snapshot.players[right].health <= 0
        ? "DOUBLE KO" : "TIED HEALTH") + " - REPLAY IN " + std::to_string(seconds)
      : std::string(snapshot.players[left].health > 0 && snapshot.players[right].health > 0
          ? "WON ON HEALTH" : "ELIMINATION") + " - NEXT IN " + std::to_string(seconds);
    break;
  case MatchPhase::MatchEnd:
    panel.stakes.clear();
    panel.status = panel.leftScore > panel.rightScore
      ? (subject && !spectator ? "YOU WIN" : panel.leftName.substr(0, 12) + " WINS")
      : panel.rightName.substr(0, 12) + " WINS";
    panel.hint = "REMATCH READY CHECK IN " + std::to_string(seconds);
    break;
  }
  hud.topLeftLines.clear();
  hud.topCenterLines.clear();
  hud.topRightLines.clear();
  hud.centerLines.clear();
  hud.countdownText.clear();
}
} // namespace lg
