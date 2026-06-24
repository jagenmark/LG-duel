#include "render/ScreenUi.hpp"
#include "render/ConsoleLayout.hpp"

#include <array>
#include <cmath>
#include <cstddef>
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
  hud.selectedWeapon = lg::Weapon::LightningGun;
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
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      1.0F,
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
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      1.0F,
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
    const lg::DrawList2D railOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::Railgun,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    bool foundRailCore = false;
    for (const lg::DrawCommand2D& command : railOverlay.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundRailCore =
          foundRailCore ||
          (
            quad->color.red == 90 &&
            quad->color.green == 220 &&
            quad->color.blue == 255
          );
      }
    }
    failures += expect(foundRailCore, "railgun viewmodel should use its own rail core");
  }

  {
    const lg::DrawList2D rocketOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::RocketLauncher,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    bool foundRocketAccent = false;
    for (const lg::DrawCommand2D& command : rocketOverlay.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundRocketAccent =
          foundRocketAccent ||
          (quad->color.red == 185 && quad->color.green == 120);
      }
    }
    failures += expect(
      foundRocketAccent,
      "rocket launcher viewmodel should use its own accent"
    );
  }

  {
    struct WeaponAccent {
      lg::Weapon weapon;
      lg::RenderColor color;
      std::string_view message;
    };
    constexpr std::array<WeaponAccent, 4> accents = {{
      {
        lg::Weapon::MachineGun,
        {218, 196, 116, 255},
        "machine gun viewmodel should use its ammo-feed accent",
      },
      {
        lg::Weapon::Shotgun,
        {188, 120, 84, 255},
        "shotgun viewmodel should use its wide pump accent",
      },
      {
        lg::Weapon::GrenadeLauncher,
        {112, 188, 90, 255},
        "grenade launcher viewmodel should use its drum accent",
      },
      {
        lg::Weapon::PlasmaGun,
        {95, 235, 210, 255},
        "plasma gun viewmodel should use its core accent",
      },
    }};

    for (const WeaponAccent& accent : accents) {
      const lg::DrawList2D overlay = lg::buildPerspectiveWeaponOverlay(
        1280,
        720,
        {},
        accent.weapon,
        lg::Weapon::LightningGun,
        1.0F,
        settings
      );
      bool foundAccent = false;
      for (const lg::DrawCommand2D& command : overlay.overlayCommands) {
        if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
          foundAccent =
            foundAccent ||
            (
              quad->color.red == accent.color.red &&
              quad->color.green == accent.color.green &&
              quad->color.blue == accent.color.blue &&
              quad->color.alpha == accent.color.alpha
            );
        }
      }
      failures += expect(foundAccent, accent.message);
    }
  }

  {
    const lg::DrawList2D switchingOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::Railgun,
      lg::Weapon::LightningGun,
      0.2F,
      settings
    );
    const auto* quad = switchingOverlay.overlayCommands.empty()
      ? nullptr
      : std::get_if<lg::FilledQuad2D>(&switchingOverlay.overlayCommands.front());
    failures += expect(
      quad != nullptr && quad->points[0].y > 640.0F,
      "weapon switch should drop the outgoing viewmodel below the screen"
    );
  }

  {
    lg::RenderSettings crosshairSettings;
    crosshairSettings.crosshairRed = 20;
    crosshairSettings.crosshairGreen = 40;
    crosshairSettings.crosshairBlue = 60;
    crosshairSettings.crosshairHitRed = 120;
    crosshairSettings.crosshairHitGreen = 140;
    crosshairSettings.crosshairHitBlue = 160;
    crosshairSettings.crosshairHitAmount = 0.5F;

    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      crosshairSettings,
      {},
      {}
    );
    const auto* crosshairArm =
      ui.overlayCommands.empty()
        ? nullptr
        : std::get_if<lg::FilledQuad2D>(&ui.overlayCommands.front());
    failures += expect(
      crosshairArm != nullptr &&
        crosshairArm->color.red == 70 &&
        crosshairArm->color.green == 90 &&
        crosshairArm->color.blue == 110,
      "crosshair should blend to its hit-feedback color"
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
      ui.overlayCommands.size() >= 13,
      "crosshair, scoreboard, and HUD should emit commands"
    );

    bool foundHealthLabel = false;
    bool foundScoreboardTitle = false;
    bool foundSpeed = false;
    bool foundSelectedWeapon = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundHealthLabel =
          foundHealthLabel || text->text == "ENEMY HP 50";
        foundScoreboardTitle =
          foundScoreboardTitle || text->text == "SCOREBOARD";
        foundSpeed = foundSpeed || text->text == "SPEED 320 UPS";
        foundSelectedWeapon =
          foundSelectedWeapon ||
          (
            text->text == "LG" &&
            text->position.x > 1180.0F &&
            text->color.red == 255
          );
      }
    }
    failures += expect(
      !foundHealthLabel && foundScoreboardTitle && foundSpeed,
      "enemy health should move out of the static HUD"
    );
    failures += expect(
      foundSelectedWeapon,
      "selected weapon indicator should mark LG on the right side"
    );

    hud.selectedWeapon = lg::Weapon::PlasmaGun;
    const lg::DrawList2D plasmaUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      hud,
      console
    );
    bool foundPlasmaWeapon = false;
    for (const lg::DrawCommand2D& command : plasmaUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundPlasmaWeapon =
          foundPlasmaWeapon ||
          (
            text->text == "PG" &&
            text->position.x > 1180.0F &&
            text->color.red == 255
          );
      }
    }
    failures += expect(
      foundPlasmaWeapon,
      "selected weapon indicator should mark expanded weapon slots"
    );
  }

  {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
    opponent.position = {10.0F, 0.0F, 0.0F};
    opponent.bounds.halfHeight = 0.9F;
    remotePlayers[1] = lg::RemotePlayerView{
      opponent,
      {},
      0.0F,
      0.5F,
      true,
      false,
      "RANGER",
    };
    settings.enemyHealthBarRed = 20;
    settings.enemyHealthBarGreen = 220;
    settings.enemyHealthBarBlue = 90;
    settings.enemyHealthBarWidth = 80.0F;
    settings.enemyHealthBarHeight = 8.0F;
    settings.enemyNameTagRed = 12;
    settings.enemyNameTagGreen = 34;
    settings.enemyNameTagBlue = 56;
    settings.enemyNameTagAlpha = 0.5F;
    settings.enemyNameTagScale = 2.0F;
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 16.0F / 9.0F);
    const lg::DrawList2D bars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      remotePlayers,
      settings,
      hud
    );
    failures += expect(
      bars.overlayCommands.size() == 4,
      "visible enemy should emit a floating name tag and health bar"
    );
    const auto* nameTag =
      std::get_if<lg::Text2D>(&bars.overlayCommands.front());
    failures += expect(
      nameTag != nullptr &&
        nameTag->text == "RANGER" &&
        nameTag->color.red == 12 &&
        nameTag->color.green == 34 &&
        nameTag->color.blue == 56 &&
        nameTag->color.alpha == 127 &&
        nameTag->scale == 2.0F,
      "floating enemy name tag should use configured text style"
    );
    const auto* fill =
      std::get_if<lg::FilledQuad2D>(&bars.overlayCommands.back());
    failures += expect(
      fill != nullptr &&
        fill->color.red == 20 &&
        fill->color.green == 220 &&
        fill->color.blue == 90 &&
        fill->color.alpha == 127 &&
        fill->points[1].x - fill->points[0].x == 40.0F,
      "floating health bar should use configured color, alpha, and health ratio"
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
      const lg::Text2D* text = nullptr;
      for (const lg::DrawCommand2D& command : ui.overlayCommands) {
        if (const auto* candidate = std::get_if<lg::Text2D>(&command)) {
          if (
            candidate->text == countdownHud.countdownText &&
            candidate->scale >= 10.0F
          ) {
            text = candidate;
          }
        }
      }
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
    console.cursorIndex = console.input.size();
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

  {
    lg::ConsoleRenderState narrowConsole;
    narrowConsole.open = true;
    narrowConsole.lines = {"alpha beta gamma"};
    narrowConsole.input = "wrap input";
    narrowConsole.cursorIndex = narrowConsole.input.size();
    const lg::DrawList2D ui = lg::buildScreenUi(
      180,
      240,
      opponent,
      settings,
      {},
      narrowConsole
    );

    bool foundFirstOutputWrap = false;
    bool foundSecondOutputWrap = false;
    bool foundFirstPromptWrap = false;
    bool foundSecondPromptWrap = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundFirstOutputWrap =
          foundFirstOutputWrap || text->text == "alpha beta";
        foundSecondOutputWrap =
          foundSecondOutputWrap || text->text == "gamma";
        foundFirstPromptWrap =
          foundFirstPromptWrap || text->text == "] wrap";
        foundSecondPromptWrap =
          foundSecondPromptWrap || text->text == "input_";
        if (
          text->color.red == 215 ||
          (
            text->color.red == 255 &&
            text->color.green == 255 &&
            text->color.blue == 255
          )
        ) {
          failures += expect(
            text->text.size() <= 10U,
            "wrapped console text should fit the available character columns"
          );
        }
      }
    }
    failures += expect(
      foundFirstOutputWrap && foundSecondOutputWrap,
      "console output should wrap to the available width"
    );
    failures += expect(
      foundFirstPromptWrap && foundSecondPromptWrap,
      "console prompt should wrap to the available width"
    );
  }

  {
    lg::ConsoleRenderState selectableConsole;
    selectableConsole.open = true;
    selectableConsole.lines = {"alpha beta"};
    selectableConsole.input = "copy me";
    selectableConsole.cursorIndex = selectableConsole.input.size();

    const lg::ConsoleTextLayout layout =
      lg::buildConsoleTextLayout(1280, 720, selectableConsole);
    const std::size_t selectionStart =
      lg::consoleTextOffsetAt(layout, 10.0F, 10.0F);
    const std::size_t selectionEnd =
      lg::consoleTextOffsetAt(layout, 10.0F + 5.0F * 16.0F, 10.0F);
    failures += expect(
      lg::consoleSelectedText(layout, selectionStart, selectionEnd) == "alpha",
      "console mouse selection should copy selected visible text"
    );

    selectableConsole.hasSelection = true;
    selectableConsole.selectionAnchor = selectionStart;
    selectableConsole.selectionFocus = selectionEnd;
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      selectableConsole
    );
    bool foundSelectionHighlight = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundSelectionHighlight =
          foundSelectionHighlight ||
          (
            quad->color.red == 58 &&
            quad->color.green == 118 &&
            quad->color.blue == 188 &&
            quad->color.alpha == 170
          );
      }
    }
    failures += expect(
      foundSelectionHighlight,
      "console selection should render a highlight behind selected text"
    );
  }

  {
    lg::ConsoleRenderState cursorConsole;
    cursorConsole.open = true;
    cursorConsole.input = "r_vsync 0";
    cursorConsole.cursorIndex = 2U;
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      cursorConsole
    );
    const auto* prompt =
      std::get_if<lg::Text2D>(&ui.overlayCommands.back());
    failures += expect(
      prompt != nullptr && prompt->text == "] r__vsync 0",
      "console prompt cursor should render at the tracked input position"
    );
  }

  return failures == 0 ? 0 : 1;
}
