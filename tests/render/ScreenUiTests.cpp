#include "app/TextInput.hpp"
#include "render/BitmapFont.hpp"
#include "render/ScreenUi.hpp"
#include "render/ChatLayout.hpp"
#include "render/ConsoleLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

const lg::Text2D* findText(
  const lg::DrawList2D& drawList,
  std::string_view value
) {
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* text = std::get_if<lg::Text2D>(&command)) {
      if (text->text == value) {
        return text;
      }
    }
  }
  return nullptr;
}

bool commandTouchesWeaponHud(const lg::DrawCommand2D& command) {
  if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
    for (const lg::ScreenPoint& point : quad->points) {
      if (point.x < 80.0F && point.y > 80.0F && point.y < 340.0F) {
        return true;
      }
    }
  }
  if (const auto* line = std::get_if<lg::Line2D>(&command)) {
    return (
      line->start.x < 80.0F &&
      line->start.y > 80.0F &&
      line->start.y < 340.0F
    ) || (
      line->end.x < 80.0F &&
      line->end.y > 80.0F &&
      line->end.y < 340.0F
    );
  }
  return false;
}

bool quadContainsPoint(
  const lg::FilledQuad2D& quad,
  float x,
  float y
) {
  float minX = quad.points[0].x;
  float maxX = quad.points[0].x;
  float minY = quad.points[0].y;
  float maxY = quad.points[0].y;
  for (const lg::ScreenPoint& point : quad.points) {
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minY = std::min(minY, point.y);
    maxY = std::max(maxY, point.y);
  }
  return x >= minX && x <= maxX && y >= minY && y <= maxY;
}

std::size_t centerLineCount(const lg::DrawList2D& drawList) {
  std::size_t count = 0;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* line = std::get_if<lg::Line2D>(&command)) {
      const bool nearCenter =
        std::fabs(line->start.x - 640.0F) < 64.0F &&
        std::fabs(line->start.y - 360.0F) < 64.0F &&
        std::fabs(line->end.x - 640.0F) < 64.0F &&
        std::fabs(line->end.y - 360.0F) < 64.0F;
      if (nearCenter) {
        ++count;
      }
    }
  }
  return count;
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
  hud.topCenterLines = {"SCORE 0 / 10"};
  hud.bottomCenterLines = {"HEALTH 100"};
  hud.fpsText = "111fps";
  hud.speedText = "320 ups";
  hud.weaponValues = {{"11", "22", "33", "44", "55", "66", "77", "88"}};
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
    lg::RenderSettings hiddenWeaponSettings = settings;
    hiddenWeaponSettings.showOwnWeapons = false;
    lg::LightningGunResult beam;
    beam.active = true;
    const lg::DrawList2D hiddenWeaponOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      beam,
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      1.0F,
      hiddenWeaponSettings
    );
    failures += expect(
      hiddenWeaponOverlay.overlayCommands.size() == 1U &&
        std::get_if<lg::Line2D>(&hiddenWeaponOverlay.overlayCommands.front()) != nullptr,
      "hidden first-person weapons should keep the local lightning beam without drawing the viewmodel"
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
        {162, 168, 176, 255},
        "shotgun viewmodel should use its sawed-off steel muzzle accent",
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
    lg::RenderSettings noGapCrosshairSettings;
    noGapCrosshairSettings.crosshairStyle = 1;
    noGapCrosshairSettings.crosshairSize = 10.0F;
    noGapCrosshairSettings.crosshairThickness = 4.0F;
    const lg::DrawList2D noGapUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      noGapCrosshairSettings,
      {},
      {}
    );
    bool foundHorizontalCenter = false;
    bool foundVerticalCenter = false;
    for (const lg::DrawCommand2D& command : noGapUi.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        const bool crosshairColor =
          quad->color.red == 255 && quad->color.green == 255 &&
          quad->color.blue == 255;
        const float quadWidth = quad->points[1].x - quad->points[0].x;
        const float quadHeight = quad->points[2].y - quad->points[1].y;
        foundHorizontalCenter =
          foundHorizontalCenter ||
          (
            crosshairColor &&
            quadContainsPoint(*quad, 640.0F, 360.0F) &&
            quadWidth > quadHeight
          );
        foundVerticalCenter =
          foundVerticalCenter ||
          (
            crosshairColor &&
            quadContainsPoint(*quad, 640.0F, 360.0F) &&
            quadHeight > quadWidth
          );
      }
    }
    failures += expect(
      foundHorizontalCenter && foundVerticalCenter,
      "crosshair_style 1 should draw a no-gap cross through the center"
    );
  }

  {
    lg::RenderSettings ringCrosshairSettings;
    ringCrosshairSettings.crosshairStyle = 3;
    ringCrosshairSettings.crosshairSize = 9.0F;
    ringCrosshairSettings.crosshairThickness = 2.0F;
    const lg::DrawList2D ringUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      ringCrosshairSettings,
      {},
      {}
    );
    failures += expect(
      centerLineCount(ringUi) >= 30U,
      "crosshair_style 3 should draw a ring using center line segments"
    );
  }

  {
    lg::RenderSettings dotOutlineSettings;
    dotOutlineSettings.crosshairStyle = 2;
    dotOutlineSettings.crosshairDotEnabled = true;
    dotOutlineSettings.crosshairDotThickness = 4.0F;
    dotOutlineSettings.crosshairOutlineEnabled = true;
    dotOutlineSettings.crosshairOutlineWidth = 2.0F;
    const lg::DrawList2D dotOutlineUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      dotOutlineSettings,
      {},
      {}
    );
    const lg::FilledQuad2D* outlineDot = nullptr;
    const lg::FilledQuad2D* colorDot = nullptr;
    for (const lg::DrawCommand2D& command : dotOutlineUi.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        if (!quadContainsPoint(*quad, 640.0F, 360.0F)) {
          continue;
        }
        if (
          quad->color.red == 0 && quad->color.green == 0 &&
          quad->color.blue == 0
        ) {
          outlineDot = quad;
        } else if (
          quad->color.red == 255 && quad->color.green == 255 &&
          quad->color.blue == 255
        ) {
          colorDot = quad;
        }
      }
    }
    failures += expect(
      outlineDot != nullptr &&
        colorDot != nullptr &&
        (outlineDot->points[1].x - outlineDot->points[0].x) >
          (colorDot->points[1].x - colorDot->points[0].x),
      "crosshair dot and outline should render independently of the main style"
    );
  }

  {
    lg::HudRenderState damageHud;
    damageHud.damageNumbers.entries = {
      {11, true, 1, 0.0F, 0},
      {22, false, 1, 0.0F, 1},
      {33, false, 1, 0.0F, 2},
    };
    lg::RenderSettings damageSettings;
    damageSettings.crosshairEnabled = false;
    damageSettings.damageNumbersSize = 1.0F;
    damageSettings.damageNumbersOffsetX = 0.0F;
    damageSettings.damageNumbersOffsetY = -40.0F;
    damageSettings.damageNumbersRed = 32;
    damageSettings.damageNumbersGreen = 96;
    damageSettings.damageNumbersBlue = 224;
    const lg::DrawList2D damageUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      damageSettings,
      damageHud,
      {}
    );
    const lg::Text2D* first = findText(damageUi, "HEADSHOT 11");
    const lg::Text2D* second = findText(damageUi, "22");
    const lg::Text2D* third = findText(damageUi, "33");
    failures += expect(
      first != nullptr &&
        second != nullptr &&
        third != nullptr &&
        std::abs(second->position.x - third->position.x) <= 4.0F &&
        first->position.y < second->position.y &&
        second->position.y < third->position.y &&
        first->color.red == 32 &&
        first->color.green == 96 &&
        first->color.blue == 224,
      "individual damage numbers should stack vertically near the aim point and use configured color"
    );
  }

  {
    lg::HudRenderState tallyHud;
    tallyHud.damageNumbers.tallies[1] = {
      true,
      45,
      false,
      1,
      0.0F,
    };
    lg::RenderSettings damageSettings;
    damageSettings.crosshairEnabled = false;
    damageSettings.damageNumbersRed = 32;
    damageSettings.damageNumbersGreen = 96;
    damageSettings.damageNumbersBlue = 224;
    const lg::DrawList2D tallyUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      damageSettings,
      tallyHud,
      {}
    );
    const lg::Text2D* tallyText = findText(tallyUi, "45");
    failures += expect(
      tallyText != nullptr &&
        tallyText->color.red == 32 &&
        tallyText->color.green == 96 &&
        tallyText->color.blue == 224,
      "screen-space damage tallies should use configured damage-number color"
    );
  }

  {
    lg::HudRenderState worldDamageHud;
    worldDamageHud.damageNumbers.tallies[1] = {
      true,
      45,
      true,
      1,
      0.0F,
      true,
      {10.0F, 0.0F, 0.0F},
    };
    lg::RenderSettings damageSettings;
    damageSettings.crosshairEnabled = false;
    damageSettings.damageNumbersSize = 1.0F;
    damageSettings.damageNumbersOffsetX = 0.0F;
    damageSettings.damageNumbersOffsetY = 0.0F;
    damageSettings.damageNumbersRed = 32;
    damageSettings.damageNumbersGreen = 96;
    damageSettings.damageNumbersBlue = 224;
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 16.0F / 9.0F);
    const lg::DrawList2D screenUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      damageSettings,
      worldDamageHud,
      {}
    );
    const lg::DrawList2D floatingUi = lg::buildFloatingDamageNumbers(
      1280,
      720,
      camera,
      damageSettings,
      worldDamageHud
    );
    const lg::Text2D* screenText = findText(screenUi, "45");
    const lg::Text2D* floatingText = findText(floatingUi, "HEADSHOT 45");
    failures += expect(
      screenText == nullptr &&
        floatingText != nullptr &&
        std::abs(floatingText->position.x - 580.6F) < 0.1F &&
        std::abs(floatingText->position.y - 326.0F) < 0.1F &&
        floatingText->color.red == 32 &&
        floatingText->color.green == 96 &&
        floatingText->color.blue == 224,
      "world damage tallies should be projected from their stored world position and use configured color"
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
    bool foundLocalHpLabel = false;
    bool foundLocalHpValue = false;
    bool foundBottomLeftHealthFill = false;
    bool foundLegacyHealthText = false;
    bool foundScoreboardTitle = false;
    bool foundSpeed = false;
    bool foundLegacySpeedText = false;
    bool foundYellowHealthFill = false;
    const lg::Text2D* topCenterScore = nullptr;
    std::array<bool, 8> foundWeaponValues = {};
    std::size_t weaponHudShapeCount = 0;
    const lg::Text2D* fpsText = nullptr;
    constexpr std::array<std::string_view, 8> weaponValues = {
      "11",
      "22",
      "33",
      "44",
      "55",
      "66",
      "77",
      "88",
    };
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundHealthLabel =
          foundHealthLabel || text->text == "ENEMY HP 50";
        foundLocalHpLabel = foundLocalHpLabel || text->text == "HP";
        foundLocalHpValue = foundLocalHpValue || text->text == "50 / 100";
        foundLegacyHealthText =
          foundLegacyHealthText || text->text == "HEALTH 100";
        foundScoreboardTitle =
          foundScoreboardTitle || text->text == "SCOREBOARD";
        topCenterScore =
          text->text == "SCORE 0 / 10" ? text : topCenterScore;
        foundSpeed =
          foundSpeed ||
          (text->text == "320 ups" && text->position.x == 640.0F &&
           text->horizontalAlignment ==
             lg::TextHorizontalAlignment::Center &&
           text->position.y > 380.0F && text->position.y < 400.0F);
        foundLegacySpeedText =
          foundLegacySpeedText || text->text == "SPEED 320 UPS";
        fpsText = text->text == "111fps" ? text : fpsText;
        for (std::size_t index = 0; index < weaponValues.size(); ++index) {
          foundWeaponValues[index] =
            foundWeaponValues[index] ||
            (text->text == weaponValues[index] && text->position.x < 80.0F);
        }
      } else {
        if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
          foundYellowHealthFill =
            foundYellowHealthFill ||
            (quad->color.red == 228 && quad->color.green == 206 &&
             quad->color.blue == 42);
          foundBottomLeftHealthFill =
            foundBottomLeftHealthFill ||
            (quad->color.red == 228 && quad->color.green == 206 &&
             quad->color.blue == 42 && quad->points[0].x < 80.0F &&
             quad->points[0].y > 650.0F);
        }
        if (commandTouchesWeaponHud(command)) {
          ++weaponHudShapeCount;
        }
      }
    }
    failures += expect(
      !foundHealthLabel && foundScoreboardTitle && foundSpeed &&
        !foundLegacySpeedText,
      "enemy health should move out of the static HUD and speed should sit under the crosshair"
    );
    failures += expect(
      foundLocalHpLabel && foundLocalHpValue && !foundLegacyHealthText &&
        foundYellowHealthFill && foundBottomLeftHealthFill,
      "health style 0 should render as a bottom-left scaled HP bar with ratio-driven fill color"
    );
    failures += expect(
      fpsText != nullptr &&
        fpsText->position.x == 1272.0F &&
        fpsText->position.y == 4.0F &&
        fpsText->horizontalAlignment == lg::TextHorizontalAlignment::Right,
      "cl_showfps HUD text should stay anchored to a fixed top-right offset"
    );
    failures += expect(
      topCenterScore != nullptr &&
        topCenterScore->position.x == 640.0F &&
        topCenterScore->position.y == 12.0F &&
        topCenterScore->horizontalAlignment == lg::TextHorizontalAlignment::Center,
      "score should render centered at the top of the HUD"
    );
    failures += expect(
      weaponHudShapeCount >= 20,
      "weapon HUD should draw compact left-edge icon silhouettes"
    );
    bool foundAllWeaponValues = true;
    for (bool foundWeaponValue : foundWeaponValues) {
      foundAllWeaponValues = foundAllWeaponValues && foundWeaponValue;
    }
    failures += expect(
      foundAllWeaponValues,
      "selected weapon indicator should show all eight weapon values"
    );

    constexpr std::array<lg::Weapon, 8> weapons = {{
      lg::Weapon::MachineGun,
      lg::Weapon::Shotgun,
      lg::Weapon::GrenadeLauncher,
      lg::Weapon::RocketLauncher,
      lg::Weapon::LightningGun,
      lg::Weapon::Railgun,
      lg::Weapon::PlasmaGun,
      lg::Weapon::FreezeGun,
    }};

    for (std::size_t index = 0; index < weapons.size(); ++index) {
      hud.selectedWeapon = weapons[index];
      const lg::DrawList2D selectedUi = lg::buildScreenUi(
        1280,
        720,
        opponent,
        settings,
        hud,
        console
      );
      bool foundSelectedWeapon = false;
      for (const lg::DrawCommand2D& command : selectedUi.overlayCommands) {
        if (const auto* text = std::get_if<lg::Text2D>(&command)) {
          foundSelectedWeapon =
            foundSelectedWeapon ||
            (
              text->text == weaponValues[index] &&
              text->position.x < 80.0F &&
              text->color.red == 245
            );
        }
      }
      failures += expect(
        foundSelectedWeapon,
        "selected weapon indicator should mark every weapon slot"
      );
    }
  }

  {
    lg::RenderSettings smallFpsSettings = settings;
    smallFpsSettings.fpsTextScale = 0.75F;
    lg::RenderSettings largeFpsSettings = settings;
    largeFpsSettings.fpsTextScale = 4.0F;
    const lg::DrawList2D smallFpsUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      smallFpsSettings,
      hud,
      console
    );
    const lg::DrawList2D largeFpsUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      largeFpsSettings,
      hud,
      console
    );
    const lg::Text2D* smallFpsText = findText(smallFpsUi, "111fps");
    const lg::Text2D* largeFpsText = findText(largeFpsUi, "111fps");
    failures += expect(
      smallFpsText != nullptr &&
        largeFpsText != nullptr &&
        smallFpsText->position.x == largeFpsText->position.x &&
        smallFpsText->position.y == largeFpsText->position.y &&
        smallFpsText->horizontalAlignment == lg::TextHorizontalAlignment::Right &&
        largeFpsText->horizontalAlignment == lg::TextHorizontalAlignment::Right,
      "cl_showfps size changes should not move the top-right FPS anchor"
    );
  }

  {
    lg::RenderSettings numberSettings = settings;
    numberSettings.healthStyle = 1;
    const lg::DrawList2D numberUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      numberSettings,
      hud,
      console
    );
    const lg::Text2D* healthNumber = findText(numberUi, "50");
    failures += expect(
      healthNumber != nullptr &&
        healthNumber->position.x == 640.0F &&
        healthNumber->horizontalAlignment ==
          lg::TextHorizontalAlignment::Center &&
        healthNumber->position.y > 670.0F &&
        healthNumber->color.red == 228 &&
        healthNumber->color.green == 206 &&
        healthNumber->color.blue == 42 &&
        findText(numberUi, "50 / 100") == nullptr,
      "health style 1 should render only the centered HP number with dynamic color"
    );
  }

  {
    lg::RenderSettings crosshairHealthSettings = settings;
    crosshairHealthSettings.healthStyle = 2;
    lg::HudRenderState crosshairHealthHud = hud;
    crosshairHealthHud.selectedWeapon = lg::Weapon::RocketLauncher;
    const lg::DrawList2D crosshairHealthUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      crosshairHealthSettings,
      crosshairHealthHud,
      console
    );
    const lg::Text2D* healthNumber = findText(crosshairHealthUi, "50");
    const lg::Text2D* ammo = nullptr;
    for (const lg::DrawCommand2D& command : crosshairHealthUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        if (text->text == "44" && text->position.x > 600.0F) {
          ammo = text;
        }
      }
    }
    crosshairHealthSettings.healthTextScale = 6.0F;
    const lg::DrawList2D largeCrosshairHealthUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      crosshairHealthSettings,
      crosshairHealthHud,
      console
    );
    const lg::Text2D* largeHealthNumber =
      findText(largeCrosshairHealthUi, "50");
    const lg::Text2D* largeAmmo = nullptr;
    for (const lg::DrawCommand2D& command :
         largeCrosshairHealthUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        if (text->text == "44" && text->position.x > 600.0F) {
          largeAmmo = text;
        }
      }
    }
    failures += expect(
      healthNumber != nullptr &&
        healthNumber->position.x > 500.0F &&
        healthNumber->position.x < 620.0F &&
        healthNumber->position.y > 380.0F &&
        healthNumber->position.y < 410.0F &&
        healthNumber->color.red == 228 &&
        healthNumber->color.green == 206 &&
        healthNumber->color.blue == 42 &&
        ammo != nullptr &&
        ammo->position.x > 680.0F &&
        ammo->position.x < 760.0F &&
        ammo->position.y > 380.0F &&
        ammo->position.y < 410.0F &&
        ammo->color.red == 255 &&
        ammo->color.green == 72 &&
        ammo->color.blue == 54 &&
        largeHealthNumber != nullptr &&
        largeAmmo != nullptr &&
        largeHealthNumber->position.x +
            2.0F * 8.0F * 6.0F ==
          healthNumber->position.x + 2.0F * 8.0F * 2.0F &&
        largeAmmo->position.x == ammo->position.x,
      "health style 2 should anchor HP and ammo near the crosshair without scaling their spacing apart"
    );
  }

  {
    lg::HudRenderState layoutHud;
    layoutHud.scoreboardOpen = true;
    layoutHud.scoreboardLines = {"SCOREBOARD", "NAME SCORE ACC DAMAGE", "> PLAYER 1"};
    layoutHud.centerLines = {"WAITING FOR PLAYERS", "1/6 PLAYERS CONNECTED"};
    layoutHud.centerOffsetY = -150.0F;
    const lg::DrawList2D scoreboardUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    layoutHud.scoreboardOpen = false;
    const lg::DrawList2D regularUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    const lg::Text2D* scoreboardTitle = findText(scoreboardUi, "SCOREBOARD");
    const lg::Text2D* scoreboardStatus =
      findText(scoreboardUi, "1/6 PLAYERS CONNECTED");
    const lg::Text2D* regularStatus =
      findText(regularUi, "1/6 PLAYERS CONNECTED");
    failures += expect(
      scoreboardTitle != nullptr &&
        scoreboardStatus != nullptr &&
        regularStatus != nullptr &&
        regularStatus->position.x == 640.0F &&
        regularStatus->horizontalAlignment ==
          lg::TextHorizontalAlignment::Center &&
        scoreboardStatus->position.y == regularStatus->position.y,
      "waiting status should stay centered and not move when the scoreboard opens"
    );

    const std::string coloredScoreboardHeader =
      "  NAME" + std::string(14U, ' ') +
      "SCORE ACC     DAMAGE";
    const std::string coloredScoreboardRow =
      "> P" + std::string(17U, ' ') +
      "0" + std::string(5U, ' ') +
      "RG 75%" + std::string(2U, ' ') +
      "84" + std::string(4U, ' ');
    layoutHud.scoreboardOpen = true;
    layoutHud.scoreboardLines = {
      "SCOREBOARD",
      coloredScoreboardHeader,
      coloredScoreboardRow,
    };
    layoutHud.scoreboardLineAccuracyWeapons = {
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      lg::Weapon::Railgun,
    };
    layoutHud.scoreboardLineAccuracyWeaponColumns = {
      std::string::npos,
      std::string::npos,
      coloredScoreboardRow.find("RG"),
    };
    const lg::DrawList2D coloredScoreboardUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    const lg::Text2D* coloredWeapon = findText(coloredScoreboardUi, "RG");
    const lg::Text2D* coloredHeaderDamage =
      findText(coloredScoreboardUi, "DAMAGE");
    const lg::Text2D* coloredDamage = findText(coloredScoreboardUi, "84");
    const lg::Text2D* coloredHeaderAccuracy =
      findText(coloredScoreboardUi, "ACC");
    failures += expect(
      coloredWeapon != nullptr &&
        coloredWeapon->color.red == 72 &&
        coloredWeapon->color.green == 232 &&
        coloredWeapon->color.blue == 112,
      "scoreboard weapon abbreviation should use the weapon color"
    );
    failures += expect(
      coloredHeaderDamage != nullptr &&
        coloredDamage != nullptr &&
        coloredHeaderDamage->position.x == coloredDamage->position.x &&
        coloredHeaderAccuracy != nullptr &&
        coloredWeapon != nullptr &&
        coloredHeaderAccuracy->position.x == coloredWeapon->position.x,
      "scoreboard header columns should share fixed row column origins"
    );

    layoutHud.scoreboardOpen = true;
    layoutHud.centerOffsetY = -220.0F;
    const lg::DrawList2D raisedUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    const lg::Text2D* raisedStatus =
      findText(raisedUi, "1/6 PLAYERS CONNECTED");
    failures += expect(
      scoreboardTitle != nullptr &&
        raisedStatus != nullptr &&
        raisedStatus->position.y + 16.0F <=
          scoreboardTitle->position.y - 8.0F,
      "fixed waiting status position should stay above the scoreboard"
    );
  }

  {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
    opponent.position = {10.0F, 0.0F, 0.0F};
    opponent.bounds.halfHeight = 0.9F;
    remotePlayers[1] = lg::RemotePlayerView{
      opponent,
      {},
      lg::Weapon::LightningGun,
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
    lg::Arena arena;
    std::array<bool, lg::kDuelPlayerCount> remoteRenderVisible = {};
    remoteRenderVisible.fill(true);
    const lg::DrawList2D bars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      remoteRenderVisible,
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
        nameTag->scale == 2.0F &&
        nameTag->horizontalAlignment == lg::TextHorizontalAlignment::Center,
      "floating enemy name tag should use configured centered text style"
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

    arena.wallCount = 1;
    arena.walls[0] = {{4.0F, -1.0F, -1.0F}, {6.0F, 1.0F, 2.0F}};
    const lg::DrawList2D occludedEnemyBars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      remoteRenderVisible,
      settings,
      hud
    );
    failures += expect(
      occludedEnemyBars.overlayCommands.empty(),
      "occluded enemy should not emit a floating name tag or health bar"
    );

    arena.wallCount = 0;
    arena.brushCount = 1;
    lg::ArenaBrush& angledBrush = arena.brushes[0];
    angledBrush.faceCount = 6;
    angledBrush.faces[0].normal = {-1.0F, 0.25F, 0.0F};
    angledBrush.faces[0].distance = -4.0F;
    angledBrush.faces[1].normal = {1.0F, -0.25F, 0.0F};
    angledBrush.faces[1].distance = 6.0F;
    angledBrush.faces[2].normal = {0.0F, -1.0F, 0.0F};
    angledBrush.faces[2].distance = 4.0F;
    angledBrush.faces[3].normal = {0.0F, 1.0F, 0.0F};
    angledBrush.faces[3].distance = 4.0F;
    angledBrush.faces[4].normal = {0.0F, 0.0F, -1.0F};
    angledBrush.faces[4].distance = 2.0F;
    angledBrush.faces[5].normal = {0.0F, 0.0F, 1.0F};
    angledBrush.faces[5].distance = 4.0F;
    const lg::DrawList2D brushOccludedEnemyBars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      remoteRenderVisible,
      settings,
      hud
    );
    failures += expect(
      brushOccludedEnemyBars.overlayCommands.empty(),
      "convex angled brush should occlude enemy floating name tag and health bar"
    );

    remotePlayers[1].teammate = true;
    const lg::DrawList2D occludedTeammateBars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      remoteRenderVisible,
      settings,
      hud
    );
    failures += expect(
      occludedTeammateBars.overlayCommands.size() == 4,
      "occluded teammate should keep existing floating name tag and health bar behavior"
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
    const std::string swedish = "ÅÄÖåäö";
    constexpr std::array<std::uint32_t, 6> expectedCodepoints = {{
      0x00C5U,
      0x00C4U,
      0x00D6U,
      0x00E5U,
      0x00E4U,
      0x00F6U,
    }};
    std::size_t offset = 0;
    for (const std::uint32_t expected : expectedCodepoints) {
      const lg::BitmapGlyphLookup glyph = lg::bitmapGlyphAt(swedish, offset);
      const auto rows = lg::supplementalBitmapGlyph(expected);
      bool hasLitPixel = false;
      if (rows.has_value()) {
        for (const std::uint8_t row : *rows) {
          hasLitPixel = hasLitPixel || row != 0U;
        }
      }
      failures += expect(
        glyph.atlasCodepoint == expected &&
          glyph.byteLength == 2U &&
          glyph.drawable &&
          !glyph.fallback &&
          rows.has_value() &&
          hasLitPixel,
        "Swedish glyph lookup should resolve to drawable supplemental bitmap glyphs"
      );
      offset += glyph.byteLength;
    }
    failures += expect(
      offset == swedish.size(),
      "Swedish glyph lookup should consume each UTF-8 codepoint exactly"
    );

    const std::string ascii = "A Z?";
    const std::array<lg::BitmapGlyphLookup, 4> asciiGlyphs = {{
      lg::bitmapGlyphAt(ascii, 0U),
      lg::bitmapGlyphAt(ascii, 1U),
      lg::bitmapGlyphAt(ascii, 2U),
      lg::bitmapGlyphAt(ascii, 3U),
    }};
    failures += expect(
      asciiGlyphs[0].atlasCodepoint == 'A' &&
        asciiGlyphs[0].byteLength == 1U &&
        asciiGlyphs[0].drawable &&
        !asciiGlyphs[0].fallback &&
        asciiGlyphs[1].atlasCodepoint == ' ' &&
        asciiGlyphs[1].byteLength == 1U &&
        !asciiGlyphs[1].drawable &&
        !asciiGlyphs[1].fallback &&
        asciiGlyphs[2].atlasCodepoint == 'Z' &&
        asciiGlyphs[2].drawable &&
        !asciiGlyphs[2].fallback &&
        asciiGlyphs[3].atlasCodepoint == '?' &&
        asciiGlyphs[3].drawable &&
        !asciiGlyphs[3].fallback,
      "ASCII bitmap glyph lookup should remain unchanged"
    );

    const lg::BitmapGlyphLookup infinityGlyph =
      lg::bitmapGlyphAt("\xE2\x88\x9E", 0U);
    const auto infinityRows = lg::supplementalBitmapGlyph(0x221EU);
    failures += expect(
      infinityGlyph.atlasCodepoint == 0x221EU &&
        infinityGlyph.byteLength == 3U &&
        infinityGlyph.drawable &&
        !infinityGlyph.fallback &&
        infinityRows.has_value(),
      "Infinity glyph lookup should resolve to U+221E instead of a missing-glyph box"
    );
  }

  {
    lg::HudRenderState chatHud;
    chatHud.chatLines.push_back({
      0,
      "This is a long message that wraps onto continuation rows",
      "Zap Witch"
    });
    const lg::ChatTextLayout layout =
      lg::buildChatTextLayout(420, 720, chatHud);
    bool foundPrefixRow = false;
    bool foundContinuationRow = false;
    for (const lg::ChatLayoutRow& row : layout.rows) {
      failures += expect(
        row.text.size() <= 24U,
        "wrapped chat rows should fit available columns"
      );
      foundPrefixRow = foundPrefixRow || row.text.rfind("Zap Witch:", 0U) == 0U;
      foundContinuationRow =
        foundContinuationRow ||
        (row.continuation && row.text.rfind("           ", 0U) == 0U);
      failures += expect(
        !row.continuation || row.text.find("Zap Witch:") == std::string::npos,
        "chat continuation rows should not repeat the speaker prefix"
      );
    }
    failures += expect(
      foundPrefixRow && foundContinuationRow,
      "chat layout should wrap with continuation indentation"
    );

    chatHud.chatLines = {{
      {1, "supercalifragilisticexpialidocious", ""}
    }};
    const lg::ChatTextLayout longWordLayout =
      lg::buildChatTextLayout(300, 720, chatHud);
    for (const lg::ChatLayoutRow& row : longWordLayout.rows) {
      failures += expect(
        row.text.size() <= 17U,
        "long chat words should split before overflowing"
      );
    }

    chatHud.chatLines = {{
      {0, "rävsmörgås ÅÄÖ", ""}
    }};
    const lg::ChatTextLayout swedishChatLayout =
      lg::buildChatTextLayout(800, 720, chatHud);
    failures += expect(
      swedishChatLayout.rows.size() == 1U &&
        swedishChatLayout.rows.front().text == "PLAYER 1: rävsmörgås ÅÄÖ" &&
        lg::utf8GlyphCount(swedishChatLayout.rows.front().text) == 24U &&
        swedishChatLayout.rows.front().text.size() >
          lg::utf8GlyphCount(swedishChatLayout.rows.front().text),
      "Swedish chat text should contribute normal glyph width and remain visible in layout"
    );
  }

  {
    lg::HudRenderState inputHud;
    inputHud.chatInputOpen = true;
    inputHud.chatInput = "one two three four five six";
    inputHud.chatCursorIndex = inputHud.chatInput.size();
    const lg::ChatTextLayout inputLayout =
      lg::buildChatTextLayout(420, 720, inputHud);
    failures += expect(
      inputLayout.inputRows.size() >= 2U &&
        inputHud.chatInput.find('\n') == std::string::npos,
      "long active chat input should wrap visually without changing logical input"
    );
    failures += expect(
      inputLayout.inputRows.front().text.rfind("CHAT: ", 0U) == 0U,
      "wrapped chat input should use CHAT prefix on the first row"
    );
    for (std::size_t index = 1; index < inputLayout.inputRows.size(); ++index) {
      failures += expect(
        inputLayout.inputRows[index].text.rfind("      ", 0U) == 0U &&
          inputLayout.inputRows[index].text.find("CHAT:") == std::string::npos,
        "chat input continuation rows should align after the prefix"
      );
    }

    const std::size_t continuationCursor = inputLayout.inputRows[1].inputBegin;
    const lg::ScreenPoint continuationCaret =
      lg::chatInputCursorPosition(
        inputLayout,
        inputHud.chatInput,
        continuationCursor
      );
    failures += expect(
      std::abs(continuationCaret.y - inputLayout.inputRows[1].y) < 0.01F,
      "chat input caret after a wrap should render on the continuation row"
    );

    const std::size_t clickedOffset = lg::chatInputOffsetAt(
      inputLayout,
      inputHud.chatInput,
      inputLayout.inputRows[1].x +
        static_cast<float>(inputLayout.inputRows[1].contentColumn + 2U) *
          inputLayout.characterWidth,
      inputLayout.inputRows[1].y + 2.0F
    );
    failures += expect(
      clickedOffset == inputLayout.inputRows[1].inputBegin + 2U,
      "clicking a chat input continuation row should map to its UTF-8-safe cursor offset"
    );

    inputHud.chatHasSelection = true;
    inputHud.chatSelectionAnchor = 1U;
    inputHud.chatSelectionFocus = inputLayout.inputRows[1].inputBegin + 2U;
    const lg::DrawList2D selectedInputUi = lg::buildScreenUi(
      420,
      720,
      opponent,
      settings,
      inputHud,
      {}
    );
    int selectionHighlights = 0;
    for (const lg::DrawCommand2D& command : selectedInputUi.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        if (
          quad->color.red == 58 &&
          quad->color.green == 118 &&
          quad->color.blue == 188
        ) {
          ++selectionHighlights;
        }
      }
    }
    failures += expect(
      selectionHighlights >= 2,
      "chat input selection spanning a wrap should highlight both visual rows"
    );

    lg::HudRenderState swedishInput;
    swedishInput.chatInputOpen = true;
    swedishInput.chatInput = "åäöÅÄÖ åäöÅÄÖ åäöÅÄÖ";
    swedishInput.chatCursorIndex = swedishInput.chatInput.size();
    const lg::ChatTextLayout swedishLayout =
      lg::buildChatTextLayout(260, 720, swedishInput);
    failures += expect(
      swedishLayout.inputRows.size() >= 2U,
      "Swedish active chat input should wrap by UTF-8 codepoint"
    );
    const std::size_t swedishOffset = lg::chatInputOffsetAt(
      swedishLayout,
      swedishInput.chatInput,
      swedishLayout.inputRows.back().x +
        static_cast<float>(swedishLayout.inputRows.back().contentColumn + 1U) *
          swedishLayout.characterWidth,
      swedishLayout.inputRows.back().y + 2.0F
    );
    failures += expect(
      lg::isUtf8Boundary(swedishInput.chatInput, swedishOffset),
      "wrapped Swedish chat input hit testing should stay on UTF-8 boundaries"
    );
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
