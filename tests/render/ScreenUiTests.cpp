#include "render/ScreenUi.hpp"

#include <array>
#include <cmath>
#include <string>
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

} // namespace

int main() {
  int failures = 0;

  lg::PlayerState opponent;
  opponent.health = 50;
  lg::RenderSettings settings;
  lg::HudRenderState hud;
  hud.showOpponentHealthBar = true;
  hud.topLeftLines = {"FPS 240"};
  hud.bottomCenterLines = {"SPEED 320 UPS", "HEALTH 100"};
  hud.scoreboardOpen = true;
  hud.scoreboardLines = {"SCOREBOARD", "PLAYER  SCORE"};
  lg::ConsoleRenderState console;

  {
    lg::LightningGunResult beam;
    beam.active = true;
    settings.beamAlpha = 0.5F;
    settings.beamHitAmount = 1.0F;
    settings.beamPulse = 1.0F;
    const lg::DrawList2D overlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      beam,
      settings
    );
    const auto* line = overlay.overlayCommands.empty()
      ? nullptr
      : std::get_if<lg::Line2D>(&overlay.overlayCommands.front());
    failures += expect(
      line != nullptr &&
        line->start.y > 720.0F &&
        line->end.x == 640.0F &&
        line->end.y == 360.0F &&
        line->color.alpha == 127 &&
        line->width > settings.beamWidth,
      "perspective local beam should pulse without moving its endpoints"
    );
    failures += expect(
      overlay.overlayCommands.size() >= 8,
      "perspective overlay should include a simple lightning gun viewmodel"
    );
  }

  {
    const lg::DrawList2D idleOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      settings
    );
    failures += expect(
      !idleOverlay.overlayCommands.empty() &&
        std::get_if<lg::FilledQuad2D>(
          &idleOverlay.overlayCommands.front()
        ) != nullptr,
      "lightning gun viewmodel should remain visible while idle"
    );
  }

  {
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      hud,
      console
    );
    failures += expect(
      ui.commands.empty(),
      "screen UI should contain only unclipped overlay commands"
    );
    failures += expect(
      ui.overlayCommands.size() >= 16,
      "health bar, crosshair, scoreboard, and HUD should emit commands"
    );

    const auto* outline =
      std::get_if<lg::FilledQuad2D>(&ui.overlayCommands[0]);
    failures += expect(
      outline != nullptr &&
        outline->color.red == 220 &&
        outline->color.alpha == 255,
      "opponent health outline should be the first UI primitive"
    );

    bool foundHealthLabel = false;
    bool foundScoreboardTitle = false;
    bool foundSpeed = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundHealthLabel =
          foundHealthLabel || text->text == "ENEMY HP 50";
        foundScoreboardTitle =
          foundScoreboardTitle || text->text == "SCOREBOARD";
        foundSpeed = foundSpeed || text->text == "SPEED 320 UPS";
      }
    }
    failures += expect(
      foundHealthLabel && foundScoreboardTitle && foundSpeed,
      "health, scoreboard, and speed should be backend-neutral UI text"
    );
  }

  {
    constexpr std::array<int, 5> visibleRightColumns = {5, 5, 5, 6, 5};
    for (std::size_t index = 0; index < visibleRightColumns.size(); ++index) {
      lg::HudRenderState countdownHud;
      countdownHud.countdownText = std::to_string(index + 1);
      const lg::DrawList2D ui = lg::buildScreenUi(
        1280,
        720,
        opponent,
        settings,
        countdownHud,
        {}
      );
      const auto* text = ui.overlayCommands.empty()
        ? nullptr
        : std::get_if<lg::Text2D>(&ui.overlayCommands.back());
      const float visibleCenterX = text == nullptr
        ? 0.0F
        : text->position.x +
          static_cast<float>(visibleRightColumns[index] + 1) *
            text->scale * 0.5F;
      const float visibleCenterY = text == nullptr
        ? 0.0F
        : text->position.y + 3.5F * text->scale;
      failures += expect(
        text != nullptr &&
          std::abs(visibleCenterX - 640.0F) < 0.01F &&
          std::abs(visibleCenterY - 360.0F) < 0.01F,
        "countdown digit pixels should be centered in the screen"
      );
    }
  }

  {
    console.open = true;
    console.lines = {"first", "second"};
    console.input = "r_vsync 0";
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      hud,
      console
    );
    const auto* prompt =
      std::get_if<lg::Text2D>(&ui.overlayCommands.back());
    failures += expect(
      prompt != nullptr &&
        prompt->text == "] r_vsync 0_" &&
        prompt->color.red == 255,
      "console prompt should render last and above the rest of the UI"
    );
  }

  return failures == 0 ? 0 : 1;
}
