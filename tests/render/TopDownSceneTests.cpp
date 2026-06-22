#include "render/TopDownScene.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <variant>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

lg::ScreenPoint quadCenter(const lg::FilledQuad2D& quad) {
  return {
    (quad.points[0].x + quad.points[2].x) * 0.5F,
    (quad.points[0].y + quad.points[2].y) * 0.5F,
  };
}

bool sameColor(lg::RenderColor lhs, lg::RenderColor rhs) {
  return lhs.red == rhs.red &&
    lhs.green == rhs.green &&
    lhs.blue == rhs.blue &&
    lhs.alpha == rhs.alpha;
}

} // namespace

int main() {
  int failures = 0;

  lg::Arena arena;
  arena.wallCount = 0;
  lg::PlayerState player;
  player.position = {0.0F, 0.0F, 0.9F};
  player.velocity = {6.0F, 0.0F, 0.0F};
  lg::PlayerState opponent;
  opponent.position = {2.0F, 0.0F, 0.9F};
  opponent.health = 50;
  lg::RenderSettings settings;
  lg::HudRenderState hud;
  lg::LightningGunResult inactiveBeam;
  const std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> weaponFires = {};
  const std::array<lg::RocketExplosionResult, lg::kDuelPlayerCount> rocketExplosions = {};
  const std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> rockets = {};

  {
    const lg::DrawList2D scene = lg::buildTopDownScene(
      800,
      720,
      arena,
      player,
      opponent,
      inactiveBeam,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings,
      hud
    );

    failures += expect(
      nearlyEqual(scene.clip.x, 80.0F) &&
        nearlyEqual(scene.clip.y, 40.0F) &&
        nearlyEqual(scene.clip.width, 640.0F) &&
        nearlyEqual(scene.clip.height, 640.0F),
      "top-down scene should expose the arena clip rectangle"
    );
    failures += expect(
      scene.commands.size() > 7,
      "empty arena should emit floor treatment, outline, player markers, and facing line"
    );
    failures += expect(
      scene.overlayCommands.size() == 2,
      "diagnostic bars should be emitted outside the world clip"
    );

    const lg::FilledQuad2D* opponentQuad = nullptr;
    const lg::FilledQuad2D* playerQuad = nullptr;
    for (const lg::DrawCommand2D& command : scene.commands) {
      const auto* quad = std::get_if<lg::FilledQuad2D>(&command);
      if (quad == nullptr) {
        continue;
      }
      if (sameColor(quad->color, {224, 82, 92, 255})) {
        opponentQuad = quad;
      }
      if (sameColor(quad->color, {66, 211, 146, 255})) {
        playerQuad = quad;
      }
    }
    failures += expect(
      opponentQuad != nullptr && playerQuad != nullptr,
      "player markers should be backend-neutral filled quads"
    );
    if (opponentQuad != nullptr && playerQuad != nullptr) {
      const lg::ScreenPoint opponentCenter = quadCenter(*opponentQuad);
      const lg::ScreenPoint playerCenter = quadCenter(*playerQuad);
      failures += expect(
        nearlyEqual(playerCenter.x, 400.0F) &&
          nearlyEqual(playerCenter.y, 360.0F),
        "local player should project to the viewport center"
      );
      failures += expect(
        opponentCenter.x > playerCenter.x,
        "positive world X should project right in the default view"
      );
    }

    const auto* speedBar =
      std::get_if<lg::FilledQuad2D>(&scene.overlayCommands[0]);
    failures += expect(
      speedBar != nullptr &&
        nearlyEqual(speedBar->points[1].x - speedBar->points[0].x, 110.0F),
      "speed diagnostic should preserve its existing normalized width"
    );

    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
    remotePlayers[1] = lg::RemotePlayerView{opponent, inactiveBeam, true};
    lg::PlayerState secondOpponent = opponent;
    secondOpponent.position.y += 2.0F;
    remotePlayers[2] = lg::RemotePlayerView{secondOpponent, inactiveBeam, true};
    const lg::DrawList2D multiScene = lg::buildTopDownScene(
      800,
      720,
      arena,
      player,
      remotePlayers,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings,
      hud
    );
    std::size_t opponentQuadCount = 0;
    for (const lg::DrawCommand2D& command : multiScene.commands) {
      const auto* quad = std::get_if<lg::FilledQuad2D>(&command);
      if (quad != nullptr && sameColor(quad->color, {224, 82, 92, 255})) {
        ++opponentQuadCount;
      }
    }
    failures += expect(
      opponentQuadCount >= 2,
      "top-down scene should draw multiple remote player markers"
    );
  }

  {
    lg::LightningGunResult localBeam;
    localBeam.active = true;
    localBeam.hit = true;
    localBeam.start = player.position;
    localBeam.end = opponent.position;
    settings.beamWidth = 6.0F;
    settings.beamRed = 20;
    settings.beamGreen = 40;
    settings.beamBlue = 60;
    settings.beamHitRed = 120;
    settings.beamHitGreen = 140;
    settings.beamHitBlue = 160;
    settings.beamHitAmount = 0.5F;
    settings.beamPulse = 1.0F;

    const lg::DrawList2D scene = lg::buildTopDownScene(
      800,
      720,
      arena,
      player,
      opponent,
      localBeam,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings,
      hud
    );

    const lg::Line2D* beam = nullptr;
    for (const lg::DrawCommand2D& command : scene.commands) {
      const auto* line = std::get_if<lg::Line2D>(&command);
      if (
        line != nullptr &&
        nearlyEqual(line->width, 6.24F) &&
        line->color.red == 73 &&
        line->color.green == 94 &&
        line->color.blue == 115
      ) {
        beam = line;
      }
    }
    failures += expect(
      beam != nullptr,
      "beam command should carry subtle animated width and brightness"
    );
    failures += expect(
      scene.commands.size() > 12,
      "a hitting beam should add one beam and four hit-marker lines"
    );
  }

  return failures == 0 ? 0 : 1;
}
