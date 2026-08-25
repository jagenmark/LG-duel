#include "app/TextInput.hpp"
#include "render/BitmapFont.hpp"
#include "render/ChatLayout.hpp"
#include "render/ConsoleLayout.hpp"
#include "render/OptionMenuLayout.hpp"
#include "render/ScreenUi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

std::size_t countText(
  const lg::DrawList2D& drawList,
  std::string_view value
) {
  std::size_t count = 0;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* text = std::get_if<lg::Text2D>(&command)) {
      if (text->text == value) {
        ++count;
      }
    }
  }
  return count;
}

bool hasFilledQuadColor(
  const lg::DrawList2D& drawList,
  lg::RenderColor color
) {
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
      if (quad->color.red == color.red &&
          quad->color.green == color.green &&
          quad->color.blue == color.blue &&
          quad->color.alpha == color.alpha) {
        return true;
      }
    }
  }
  return false;
}

float minimumYForFilledQuadColor(
  const lg::DrawList2D& drawList,
  lg::RenderColor color
) {
  float minimumY = 100000.0F;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* quad = std::get_if<lg::FilledQuad2D>(&command);
    if (quad == nullptr || quad->color.red != color.red ||
        quad->color.green != color.green || quad->color.blue != color.blue ||
        quad->color.alpha != color.alpha) {
      continue;
    }
    for (const lg::ScreenPoint& point : quad->points) {
      minimumY = std::min(minimumY, point.y);
    }
  }
  return minimumY;
}

bool findFilledQuadBounds(
  const lg::DrawList2D& drawList,
  lg::RenderColor color,
  lg::ScreenRect& bounds
) {
  float minimumX = 100000.0F;
  float maximumX = -100000.0F;
  float minimumY = 100000.0F;
  float maximumY = -100000.0F;
  bool found = false;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* quad = std::get_if<lg::FilledQuad2D>(&command);
    if (quad == nullptr || quad->color.red != color.red ||
        quad->color.green != color.green || quad->color.blue != color.blue ||
        quad->color.alpha != color.alpha) {
      continue;
    }
    found = true;
    for (const lg::ScreenPoint& point : quad->points) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
      minimumY = std::min(minimumY, point.y);
      maximumY = std::max(maximumY, point.y);
    }
  }
  bounds = {
    minimumX,
    minimumY,
    maximumX - minimumX,
    maximumY - minimumY,
  };
  return found;
}

const lg::Text2D* findTextWithRedAtLeast(
  const lg::DrawList2D& drawList,
  std::string_view value,
  std::uint8_t minimumRed
) {
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* text = std::get_if<lg::Text2D>(&command)) {
      if (text->text == value && text->color.red >= minimumRed) {
        return text;
      }
    }
  }
  return nullptr;
}

bool commandTouchesWeaponHud(const lg::DrawCommand2D& command) {
  if (const auto* image = std::get_if<lg::Image2D>(&command)) {
    return image->destination.x < 80.0F &&
      image->destination.y > 30.0F && image->destination.y < 620.0F;
  }
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

bool hasLineNearEdge(
  const lg::DrawList2D& drawList,
  lg::RenderColor color,
  int edge,
  float width,
  float height
) {
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* line = std::get_if<lg::Line2D>(&command)) {
      if (line->color.red != color.red ||
          line->color.green != color.green ||
          line->color.blue != color.blue) {
        continue;
      }
      const float minimumX = std::min(line->start.x, line->end.x);
      const float maximumX = std::max(line->start.x, line->end.x);
      const float minimumY = std::min(line->start.y, line->end.y);
      const float maximumY = std::max(line->start.y, line->end.y);
      if ((edge == 0 && maximumY < 60.0F) ||
          (edge == 1 && minimumX > width - 60.0F) ||
          (edge == 2 && minimumY > height - 60.0F) ||
          (edge == 3 && maximumX < 60.0F)) {
        return true;
      }
    }
  }
  return false;
}

bool hasLineNearCenter(
  const lg::DrawList2D& drawList,
  lg::RenderColor color,
  float centerX,
  float centerY,
  float radius
) {
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* line = std::get_if<lg::Line2D>(&command);
    if (line == nullptr || line->color.red != color.red ||
        line->color.green != color.green || line->color.blue != color.blue) {
      continue;
    }
    if (std::fabs(line->start.x - centerX) < radius &&
        std::fabs(line->start.y - centerY) < radius &&
        std::fabs(line->end.x - centerX) < radius &&
        std::fabs(line->end.y - centerY) < radius) {
      return true;
    }
  }
  return false;
}

std::size_t countLinesWithColor(
  const lg::DrawList2D& drawList,
  lg::RenderColor color
) {
  std::size_t count = 0;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* line = std::get_if<lg::Line2D>(&command)) {
      if (
        line->color.red == color.red &&
        line->color.green == color.green &&
        line->color.blue == color.blue
      ) {
        ++count;
      }
    }
  }
  return count;
}

struct LineBounds {
  float minimumX = 100000.0F;
  float maximumX = -100000.0F;
  float minimumY = 100000.0F;
  float maximumY = -100000.0F;
  std::size_t lineCount = 0U;
};

bool findExactLineBounds(
  const lg::DrawList2D& drawList,
  lg::RenderColor color,
  LineBounds& bounds
) {
  bool found = false;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* line = std::get_if<lg::Line2D>(&command);
    if (line == nullptr || line->color.red != color.red ||
        line->color.green != color.green || line->color.blue != color.blue ||
        line->color.alpha != color.alpha) {
      continue;
    }
    found = true;
    ++bounds.lineCount;
    bounds.minimumX = std::min(
      bounds.minimumX,
      std::min(line->start.x, line->end.x)
    );
    bounds.maximumX = std::max(
      bounds.maximumX,
      std::max(line->start.x, line->end.x)
    );
    bounds.minimumY = std::min(
      bounds.minimumY,
      std::min(line->start.y, line->end.y)
    );
    bounds.maximumY = std::max(
      bounds.maximumY,
      std::max(line->start.y, line->end.y)
    );
  }
  return found;
}

bool directionalLinesStayInBounds(
  const lg::DrawList2D& drawList,
  lg::RenderColor color,
  float width,
  float height
) {
  bool found = false;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* line = std::get_if<lg::Line2D>(&command);
    if (line == nullptr || line->color.red != color.red ||
        line->color.green != color.green || line->color.blue != color.blue) {
      continue;
    }
    found = true;
    for (const lg::ScreenPoint point : {line->start, line->end}) {
      if (point.x < -0.01F || point.x > width + 0.01F ||
          point.y < -0.01F || point.y > height + 0.01F) {
        return false;
      }
    }
  }
  return found;
}

float minimumDirectionalEdgeDistance(
  const lg::DrawList2D& drawList,
  lg::RenderColor color,
  float width,
  float height
) {
  float minimumDistance = 100000.0F;
  bool found = false;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* line = std::get_if<lg::Line2D>(&command);
    if (line == nullptr || line->color.red != color.red ||
        line->color.green != color.green || line->color.blue != color.blue ||
        line->color.alpha != color.alpha) {
      continue;
    }
    found = true;
    for (const lg::ScreenPoint point : {line->start, line->end}) {
      minimumDistance = std::min(
        minimumDistance,
        std::min(
          std::min(point.x, width - point.x),
          std::min(point.y, height - point.y)
        )
      );
    }
  }
  return found ? minimumDistance : 100000.0F;
}

float directionalTangentSpan(const LineBounds& bounds, int edge) {
  return edge == 0 || edge == 2
    ? bounds.maximumX - bounds.minimumX
    : bounds.maximumY - bounds.minimumY;
}

float directionalNormalSpan(const LineBounds& bounds, int edge) {
  return edge == 0 || edge == 2
    ? bounds.maximumY - bounds.minimumY
    : bounds.maximumX - bounds.minimumX;
}

float maximumLineWidth(
  const lg::DrawList2D& drawList,
  lg::RenderColor color
) {
  float maximumWidth = 0.0F;
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    const auto* line = std::get_if<lg::Line2D>(&command);
    if (line == nullptr || line->color.red != color.red ||
        line->color.green != color.green || line->color.blue != color.blue ||
        line->color.alpha != color.alpha) {
      continue;
    }
    maximumWidth = std::max(maximumWidth, line->width);
  }
  return maximumWidth;
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
  hud.weaponValues = {{
    "11", "22", "33", "44", "55", "66", "77", "88", "99",
  }};
  hud.scoreboardOpen = true;
  {
    lg::HudRenderState settingsHud;
    settingsHud.settingsOpen = true;
    settingsHud.settingsItems = {
      {"Display mode", "Borderless Fullscreen", true, false, false},
      {"Display / Monitor", "Display 1", false, false, false},
      {"FPS limit", "Unlimited", false, false, false},
      {"Apply changes", "Enter", false, false, true},
    };
    settingsHud.topLeftLines = {"HUD MUST STAY BELOW SETTINGS"};
    settingsHud.topRightLines = {"SCORE MUST STAY BELOW SETTINGS"};
    settingsHud.netGraph.mode = 1;
    settingsHud.netGraph.telemetry.valid = true;
    lg::ConsoleRenderState settingsConsole;
    settingsConsole.open = true;
    settingsConsole.lines = {"CONSOLE MUST STAY BELOW SETTINGS"};
    const lg::DrawList2D settingsUi = lg::buildScreenUi(
      1280, 720, {}, settings, settingsHud, settingsConsole
    );
    const lg::Text2D* firstLabel = findText(settingsUi, "Display mode");
    const lg::Text2D* secondLabel = findText(settingsUi, "Display / Monitor");
    const lg::Text2D* longValue = findText(settingsUi, "Borderless Fullscreen");
    const lg::Text2D* shortValue = findText(settingsUi, "Unlimited");
    const lg::Text2D* commandValue = findText(settingsUi, "Enter");
    const lg::Text2D* arrows = findText(settingsUi, "<  >");
    failures += expect(
      firstLabel != nullptr && secondLabel != nullptr &&
        firstLabel->position.x == secondLabel->position.x &&
        firstLabel->horizontalAlignment == lg::TextHorizontalAlignment::Left &&
        longValue != nullptr && shortValue != nullptr && commandValue != nullptr &&
        longValue->position.x == shortValue->position.x &&
        shortValue->position.x == commandValue->position.x &&
        longValue->horizontalAlignment == lg::TextHorizontalAlignment::Right &&
        shortValue->horizontalAlignment == lg::TextHorizontalAlignment::Right &&
        arrows != nullptr && arrows->position.x > shortValue->position.x,
      "settings labels should share a left edge; values should share a right edge before fixed arrows"
    );
    failures += expect(
      findText(settingsUi, "HUD MUST STAY BELOW SETTINGS") == nullptr &&
        findText(settingsUi, "SCORE MUST STAY BELOW SETTINGS") == nullptr &&
        findText(settingsUi, "CONSOLE MUST STAY BELOW SETTINGS") == nullptr &&
        findText(settingsUi, "SETTINGS / VIDEO") != nullptr,
      "settings should be the exclusive top UI layer over HUD, network, and console overlays"
    );

    lg::HudRenderState scrolledSettings = settingsHud;
    scrolledSettings.settingsItems.clear();
    for (int row = 0; row < 24; ++row) {
      scrolledSettings.settingsItems.push_back(
        {"Scroll label " + std::to_string(row), "Value", row == 6, false, false}
      );
    }
    scrolledSettings.settingsScrollRows = 4U;
    scrolledSettings.settingsHoveredRow = 6;
    scrolledSettings.settingsPressedRow = 6;
    const lg::DrawList2D scrolledUi = lg::buildScreenUi(
      1280, 720, {}, settings, scrolledSettings, {}
    );
    failures += expect(
      findText(scrolledUi, "Scroll label 0") == nullptr &&
        findText(scrolledUi, "Scroll label 4") != nullptr &&
        findText(scrolledUi, "Scroll label 6") != nullptr,
      "settings rows should clip to the viewport after scrolling"
    );

    lg::HudRenderState miscHud;
    miscHud.miscMenuOpen = true;
    miscHud.miscMenuItems = {
        {"Weapon position", "Center", true, false, false},
        {"Netgraph", "Off", false, false, false},
        {"Close", "Esc", false, false, true},
    };
    miscHud.miscMenuFooter =
        "Chooses windowed, borderless fullscreen, or exclusive fullscreen.";
    miscHud.topLeftLines = {"HUD MUST STAY BELOW TOOLS"};
    const lg::DrawList2D miscUi =
        lg::buildScreenUi(1280, 720, {}, settings, miscHud, settingsConsole);
    const lg::OptionMenuLayout miscLayout =
        lg::buildOptionMenuLayout(1280, 720, miscHud.miscMenuItems.size(), 0U);
    std::size_t miscFooterLines = 0U;
    bool miscFooterFits = true;
    for (const lg::DrawCommand2D &command : miscUi.overlayCommands) {
      const auto *text = std::get_if<lg::Text2D>(&command);
      if (text == nullptr || text->color.red != 174U ||
          text->color.green != 190U || text->color.blue != 204U) {
        continue;
      }
      ++miscFooterLines;
      miscFooterFits =
          miscFooterFits && text->scale >= 2.0F &&
          text->position.x +
                  static_cast<float>(lg::utf8GlyphCount(text->text)) * 8.0F *
                      text->scale <=
              miscLayout.panelX + miscLayout.panelWidth - 22.0F;
    }
    failures += expect(
        findText(miscUi, "TOOLS / DEBUG") != nullptr &&
            findText(miscUi, "Weapon position") != nullptr &&
            miscFooterLines == 2U && miscFooterFits &&
            findText(miscUi, "HUD MUST STAY BELOW TOOLS") == nullptr &&
            findText(miscUi, "CONSOLE MUST STAY BELOW SETTINGS") == nullptr,
        "tools menu should use the option-menu layout as an exclusive modal "
        "with wrapped, readable help text");
  }
  hud.scoreboardLines = {"SCOREBOARD", "PLAYER  SCORE"};
  lg::ConsoleRenderState console;

  {
    lg::HudRenderState deathHud;
    deathHud.deathDesaturation = 1.0F;
    deathHud.topCenterLines = {"SPECTATING TEAMMATE"};
    const lg::DrawList2D deathUi = lg::buildScreenUi(
      1280, 720, {}, settings, deathHud, {}
    );
    const auto* wash = deathUi.commands.empty()
      ? nullptr
      : std::get_if<lg::FilledQuad2D>(&deathUi.commands.front());
    failures += expect(
      wash != nullptr && wash->color.red == wash->color.green &&
        wash->color.green == wash->color.blue && wash->color.alpha > 0,
      "death presentation should place a neutral desaturation wash below the HUD"
    );
    const lg::Text2D* spectatorName = findText(deathUi, "SPECTATING TEAMMATE");
    failures += expect(
      spectatorName != nullptr && spectatorName->position.y < 180.0F,
      "the spectated player name should remain visible in the top-center HUD"
    );
  }

  {
    lg::PlayerState dashPlayer;
    const lg::DrawList2D readyUi = lg::buildScreenUi(
      1280,
      720,
      dashPlayer,
      settings,
      {},
      {}
    );
    const lg::Text2D* readyDash = findText(readyUi, "DASH");
    failures += expect(
      readyDash != nullptr &&
        readyDash->color.green > readyDash->color.red,
      "dash indicator should show a bright ready state when cooldown is clear"
    );

    dashPlayer.dashCooldownTicksRemaining = 20;
    const lg::DrawList2D cooldownUi = lg::buildScreenUi(
      1280,
      720,
      dashPlayer,
      settings,
      {},
      {}
    );
    const lg::Text2D* cooldownDash = findText(cooldownUi, "DASH");
    failures += expect(
      cooldownDash != nullptr &&
        cooldownDash->color.green == cooldownDash->color.red + 8,
      "dash indicator should dim while dash is cooling down"
    );
  }

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
      overlay.overlayCommands.size() == 1U,
      "perspective overlay should keep only the lightning effect beside the 3D viewmodel"
    );
  }

  {
    lg::LightningGunResult beam;
    beam.active = true;
    constexpr lg::ScreenPoint visibleMuzzle = {508.0F, 626.0F};
    const lg::DrawList2D overlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      beam,
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      1.0F,
      settings,
      visibleMuzzle
    );
    const auto* line = overlay.overlayCommands.empty()
      ? nullptr
      : std::get_if<lg::Line2D>(&overlay.overlayCommands.front());
    failures += expect(
      line != nullptr &&
        line->start.x == visibleMuzzle.x &&
        line->start.y == visibleMuzzle.y,
      "first-person lightning beam should begin at its supplied visible muzzle"
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
      idleOverlay.overlayCommands.empty(),
      "lightning gun should use its 3D viewmodel without a legacy 2D body overlay"
    );
  }

  {
    const lg::DrawList2D freezeOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::FreezeGun,
      lg::Weapon::FreezeGun,
      1.0F,
      settings
    );
    failures += expect(
      freezeOverlay.overlayCommands.empty(),
      "freeze gun should use its 3D viewmodel without a legacy 2D body overlay"
    );
  }

  {
    lg::LightningGunResult freezeBeam;
    freezeBeam.active = true;
    lg::RenderSettings freezeSettings = settings;
    freezeSettings.freezeGunFiringAmount = 1.0F;
    freezeSettings.freezeGunActivationFlashAmount = 1.0F;
    freezeSettings.beamPhaseRadians = 1.25F;
    constexpr lg::ScreenPoint muzzle = {420.0F, 610.0F};
    const lg::DrawList2D freezeOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      freezeBeam,
      lg::Weapon::FreezeGun,
      lg::Weapon::FreezeGun,
      1.0F,
      freezeSettings,
      muzzle
    );
    bool foundStableCore = false;
    bool foundSheath = false;
    bool foundVapor = false;
    bool allBeamLinesOnAxis = true;
    constexpr float endX = 640.0F;
    constexpr float endY = 360.0F;
    const float axisX = endX - muzzle.x;
    const float axisY = endY - muzzle.y;
    const auto pointOnAxis = [&](lg::ScreenPoint point) {
      return std::fabs(
        axisX * (point.y - muzzle.y) - axisY * (point.x - muzzle.x)
      ) < 0.01F;
    };
    for (const lg::DrawCommand2D& command : freezeOverlay.overlayCommands) {
      if (const auto* line = std::get_if<lg::Line2D>(&command)) {
        allBeamLinesOnAxis = allBeamLinesOnAxis &&
          pointOnAxis(line->start) && pointOnAxis(line->end);
        foundStableCore = foundStableCore ||
          (
            line->start.x == muzzle.x && line->start.y == muzzle.y &&
            line->end.x == endX && line->end.y == endY &&
            line->color.red == 238 && line->color.alpha == 245
          );
        foundSheath = foundSheath || line->width > freezeSettings.beamWidth * 4.0F;
      } else if (std::get_if<lg::FilledQuad2D>(&command) != nullptr) {
        foundVapor = true;
      }
    }
    failures += expect(
      foundStableCore && foundSheath && foundVapor && allBeamLinesOnAxis,
      "every local Freeze beam line should stay on the current muzzle-to-center axis"
    );
  }

  {
    lg::LightningGunResult freezeBeam;
    freezeBeam.active = true;
    freezeBeam.end = {8.0F, 0.0F, 0.65F};
    lg::RenderSettings freezeSettings = settings;
    freezeSettings.freezeGunFiringAmount = 1.0F;
    constexpr lg::ScreenPoint visibleMuzzle = {472.0F, 602.0F};
    const lg::DrawList2D freezeOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      freezeBeam,
      lg::Weapon::FreezeGun,
      lg::Weapon::FreezeGun,
      1.0F,
      freezeSettings,
      visibleMuzzle
    );
    bool foundCurrentCore = false;
    for (const lg::DrawCommand2D& command : freezeOverlay.overlayCommands) {
      if (const auto* line = std::get_if<lg::Line2D>(&command)) {
        foundCurrentCore = foundCurrentCore ||
          (
            line->start.x == visibleMuzzle.x &&
            line->start.y == visibleMuzzle.y &&
            line->end.x == 640.0F &&
            line->end.y == 360.0F &&
            line->color.red == 238 &&
            line->color.green == 253 &&
            line->color.blue == 255 &&
            line->color.alpha == 245
          );
      }
    }
    failures += expect(
      foundCurrentCore,
      "local Freeze should run from its current muzzle to the screen center"
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
    failures += expect(
      railOverlay.overlayCommands.empty(),
      "authored sniper viewmodel should not be covered by the old railgun overlay"
    );
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
    failures += expect(
      rocketOverlay.overlayCommands.empty(),
      "rocket launcher should use its 3D viewmodel without a legacy 2D body overlay"
    );
  }

  {
    constexpr std::array<lg::Weapon, 3> authoredWeapons = {{
      lg::Weapon::MachineGun,
      lg::Weapon::Shotgun,
      lg::Weapon::GrenadeLauncher,
    }};

    for (const lg::Weapon weapon : authoredWeapons) {
      const lg::DrawList2D overlay = lg::buildPerspectiveWeaponOverlay(
        1280,
        720,
        {},
        weapon,
        lg::Weapon::LightningGun,
        1.0F,
        settings
      );
      failures += expect(
        overlay.overlayCommands.empty(),
        "authored 3D viewmodels should not receive legacy 2D body geometry"
      );
    }
  }

  {
    const lg::DrawList2D plasmaOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::PlasmaGun,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    failures += expect(
      plasmaOverlay.overlayCommands.empty(),
      "authored plasma gun viewmodel should not be covered by legacy overlay geometry"
    );
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
    failures += expect(
      switchingOverlay.overlayCommands.empty(),
      "3D weapon switching should not draw a second 2D outgoing weapon"
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
    constexpr float kPi = 3.14159265359F;
    constexpr float kHalfPi = kPi * 0.5F;
    lg::RenderSettings directionalSettings;
    directionalSettings.crosshairEnabled = false;
    constexpr lg::RenderColor directionalColor = {255, 76, 70, 255};
    struct DirectionExpectation {
      float relativeYaw;
      int edge;
      const char* name;
    };
    constexpr std::array<DirectionExpectation, 4> directions = {{
      {0.0F, 0, "front"},
      {-kHalfPi, 1, "right"},
      {kPi, 2, "back"},
      {kHalfPi, 3, "left"},
    }};
    for (const DirectionExpectation& direction : directions) {
      lg::HudRenderState directionalHud;
      directionalHud.directionalDamage.distancePixels = 24.0F;
      directionalHud.directionalDamage.indicators = {{
        {true, 1, direction.relativeYaw, 1.0F, 1.0F, true, false},
      }};
      const lg::DrawList2D directionalUi = lg::buildScreenUi(
        1280, 720, opponent, directionalSettings, directionalHud, {}
      );
      LineBounds bounds;
      failures += expect(
        hasLineNearEdge(
          directionalUi, directionalColor, direction.edge, 1280.0F, 720.0F
        ) && findExactLineBounds(directionalUi, directionalColor, bounds) &&
          bounds.lineCount == 10U &&
          directionalLinesStayInBounds(
            directionalUi, directionalColor, 1280.0F, 720.0F
          ) && directionalTangentSpan(bounds, direction.edge) > 100.0F &&
          directionalNormalSpan(bounds, direction.edge) > 1.0F &&
          maximumLineWidth(directionalUi, directionalColor) >= 6.0F &&
          !hasLineNearCenter(
            directionalUi, directionalColor, 640.0F, 360.0F, 180.0F
          ),
        (std::string("directional ") + direction.name +
         " damage should draw one curved, bounded edge crescent").c_str()
      );
      for (int otherEdge = 0; otherEdge < 4; ++otherEdge) {
        if (otherEdge == direction.edge) {
          continue;
        }
        failures += expect(
          !hasLineNearEdge(
            directionalUi, directionalColor, otherEdge, 1280.0F, 720.0F
          ),
          (std::string("directional ") + direction.name +
           " damage should not move to another edge").c_str()
        );
      }
    }

    lg::HudRenderState wideFrontHud;
    wideFrontHud.directionalDamage.distancePixels = 24.0F;
    wideFrontHud.directionalDamage.indicators = {{
      {true, 8, 0.0F, 1.0F, 1.0F, true, false},
    }};
    const lg::DrawList2D wideFrontUi = lg::buildScreenUi(
      1280, 720, opponent, directionalSettings, wideFrontHud, {}
    );
    LineBounds wideFrontBounds;
    const bool foundWideFront = findExactLineBounds(
      wideFrontUi, directionalColor, wideFrontBounds
    );
    lg::HudRenderState tallFrontHud = wideFrontHud;
    const lg::DrawList2D tallFrontUi = lg::buildScreenUi(
      720, 1280, opponent, directionalSettings, tallFrontHud, {}
    );
    LineBounds tallFrontBounds;
    const bool foundTallFront = findExactLineBounds(
      tallFrontUi, directionalColor, tallFrontBounds
    );
    const float wideFrontSpan = directionalTangentSpan(wideFrontBounds, 0);
    const float tallFrontSpan = directionalTangentSpan(tallFrontBounds, 0);
    failures += expect(
      foundWideFront && foundTallFront && wideFrontBounds.lineCount == 10U &&
        tallFrontBounds.lineCount == 10U && wideFrontSpan > 100.0F &&
        tallFrontSpan > 100.0F &&
        std::max(wideFrontSpan, tallFrontSpan) /
            std::min(wideFrontSpan, tallFrontSpan) < 1.25F,
      "directional crescent length should stay stable when the aspect ratio changes"
    );

    constexpr float diagonalYaw = -kHalfPi * 0.5F;
    lg::HudRenderState wideDiagonalHud;
    wideDiagonalHud.directionalDamage.distancePixels = 24.0F;
    wideDiagonalHud.directionalDamage.indicators = {{
      {true, 9, diagonalYaw, 1.0F, 1.0F, true, false},
    }};
    const lg::DrawList2D wideDiagonalUi = lg::buildScreenUi(
      1280, 720, opponent, directionalSettings, wideDiagonalHud, {}
    );
    LineBounds wideDiagonalBounds;
    const bool foundWideDiagonal = findExactLineBounds(
      wideDiagonalUi, directionalColor, wideDiagonalBounds
    );

    lg::HudRenderState tallDiagonalHud = wideDiagonalHud;
    const lg::DrawList2D tallDiagonalUi = lg::buildScreenUi(
      720, 1280, opponent, directionalSettings, tallDiagonalHud, {}
    );
    LineBounds tallDiagonalBounds;
    const bool foundTallDiagonal = findExactLineBounds(
      tallDiagonalUi, directionalColor, tallDiagonalBounds
    );
    const float wideDiagonalSpan = directionalTangentSpan(
      wideDiagonalBounds, 0
    );
    const float tallDiagonalSpan = directionalTangentSpan(
      tallDiagonalBounds, 1
    );
    failures += expect(
      foundWideDiagonal && foundTallDiagonal &&
        wideDiagonalBounds.lineCount == 10U &&
        tallDiagonalBounds.lineCount == 10U &&
        hasLineNearEdge(wideDiagonalUi, directionalColor, 0, 1280.0F, 720.0F) &&
        hasLineNearEdge(tallDiagonalUi, directionalColor, 1, 720.0F, 1280.0F) &&
        minimumDirectionalEdgeDistance(
          wideDiagonalUi, directionalColor, 1280.0F, 720.0F
        ) <= 30.0F &&
        minimumDirectionalEdgeDistance(
          tallDiagonalUi, directionalColor, 720.0F, 1280.0F
        ) <= 30.0F &&
        directionalLinesStayInBounds(
          wideDiagonalUi, directionalColor, 1280.0F, 720.0F
        ) &&
        directionalLinesStayInBounds(
          tallDiagonalUi, directionalColor, 720.0F, 1280.0F
        ) && wideDiagonalSpan > 100.0F && tallDiagonalSpan > 100.0F &&
        std::max(wideDiagonalSpan, tallDiagonalSpan) /
            std::min(wideDiagonalSpan, tallDiagonalSpan) < 1.25F,
      "diagonal damage crescents should anchor to the inset perimeter and keep their size across wide and tall screens"
    );

    lg::HudRenderState directionalHud;
    directionalHud.directionalDamage.distancePixels = 24.0F;
    directionalHud.directionalDamage.indicators = {{
      {true, 6, 0.0F, 1.0F, 1.0F, true, true},
    }};
    const lg::DrawList2D selfDamageUi = lg::buildScreenUi(
      1280, 720, opponent, directionalSettings, directionalHud, {}
    );
    constexpr lg::RenderColor selfDamageColor = {255, 186, 66, 255};
    failures += expect(
      countLinesWithColor(selfDamageUi, selfDamageColor) > 10U &&
        hasLineNearEdge(selfDamageUi, selfDamageColor, 0, 1280.0F, 720.0F) &&
        countLinesWithColor(selfDamageUi, directionalColor) == 0U,
      "self damage should use a distinct screen-edge arc color"
    );

    directionalHud.directionalDamage.indicators = {{
      {true, 7, 0.0F, 1.0F, 1.0F, false, false},
    }};
    const lg::DrawList2D neutralDamageUi = lg::buildScreenUi(
      1280, 720, opponent, directionalSettings, directionalHud, {}
    );
    failures += expect(
      hasLineNearEdge(neutralDamageUi, directionalColor, 0, 1280.0F, 720.0F) &&
        !hasLineNearCenter(
          neutralDamageUi, directionalColor, 640.0F, 360.0F, 180.0F
        ),
      "directionless damage should use a neutral top-edge arc"
    );

    directionalHud.directionalDamage.enabled = false;
    const lg::DrawList2D disabledDirectionalUi = lg::buildScreenUi(
      1280, 720, opponent, directionalSettings, directionalHud, {}
    );
    failures += expect(
      countLinesWithColor(disabledDirectionalUi, selfDamageColor) == 0U,
      "directional damage should render nothing without a local player body"
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
      {11, true, 1, 0.0F, 0, true, {10.0F, 0.0F, 0.0F}},
      {22, false, 1, 0.0F, 1, true, {10.0F, 0.0F, 0.0F}},
      {33, false, 1, 0.0F, 2, true, {10.0F, 0.0F, 0.0F}},
    };
    damageHud.damageNumbers.tallies[1] = {
      true,
      66,
      false,
      1,
      0.0F,
      true,
      {10.0F, 0.0F, 0.0F},
    };
    lg::RenderSettings damageSettings;
    damageSettings.crosshairEnabled = false;
    damageSettings.damageNumbersSize = 1.0F;
    damageSettings.damageNumbersOffsetX = 0.0F;
    damageSettings.damageNumbersOffsetY = -40.0F;
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
      damageHud,
      {}
    );
    const lg::DrawList2D damageUi = lg::buildFloatingDamageNumbers(
      1280,
      720,
      camera,
      damageSettings,
      damageHud
    );
    const lg::Text2D* first = findText(damageUi, "11");
    const lg::Text2D* second = findText(damageUi, "22");
    const lg::Text2D* third = findText(damageUi, "33");
    const lg::Text2D* tally = findText(damageUi, "66");
    failures += expect(
      findText(screenUi, "11") == nullptr &&
        findText(damageUi, "HEADSHOT 11") == nullptr &&
        countText(damageUi, "11") == 2U &&
        first != nullptr &&
        second != nullptr &&
        third != nullptr &&
        tally != nullptr &&
        std::abs(second->position.x - third->position.x) <= 5.0F &&
        first->position.y < second->position.y &&
        second->position.y < third->position.y &&
        third->position.y < tally->position.y &&
        first->color.red != 32 &&
        first->color.green != 96 &&
        first->color.blue != 224 &&
        second->color.red == 32 &&
        second->color.green == 96 &&
        second->color.blue == 224 &&
        third->scale > second->scale,
      "individual damage numbers should stack in world space, style headshots, and grow by damage"
    );
  }

  {
    lg::HudRenderState damageHud;
    damageHud.damageNumbers.entries = {
      {1, false, 1, 0.0F, 0, true, {10.0F, 0.0F, 0.0F}},
      {2, false, 1, 0.0F, 1, true, {10.0F, 0.0F, 0.0F}},
      {3, false, 1, 0.0F, 2, true, {10.0F, 0.0F, 0.0F}},
      {4, false, 1, 0.0F, 3, true, {10.0F, 0.0F, 0.0F}},
      {5, false, 1, 0.0F, 4, true, {10.0F, 0.0F, 0.0F}},
      {6, false, 1, 0.0F, 5, true, {10.0F, 0.0F, 0.0F}},
    };
    lg::RenderSettings damageSettings;
    damageSettings.crosshairEnabled = false;
    damageSettings.damageNumbersSize = 1.0F;
    damageSettings.damageNumbersOffsetX = 0.0F;
    damageSettings.damageNumbersOffsetY = -40.0F;
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 16.0F / 9.0F);
    const lg::DrawList2D damageUi = lg::buildFloatingDamageNumbers(
      1280,
      720,
      camera,
      damageSettings,
      damageHud
    );
    const lg::Text2D* second = findText(damageUi, "2");
    const lg::Text2D* sixth = findText(damageUi, "6");
    failures += expect(
      findText(damageUi, "1") == nullptr &&
        second != nullptr &&
        findText(damageUi, "3") != nullptr &&
        findText(damageUi, "4") != nullptr &&
        findText(damageUi, "5") != nullptr &&
        sixth != nullptr &&
        second->position.y < sixth->position.y,
      "per-instance damage numbers should render only the latest five hits above the anchor"
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
    damageSettings.damageNumbersDamageColor = true;
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
    const lg::Text2D* backingText = findText(floatingUi, "45");
    const lg::Text2D* floatingText =
      findTextWithRedAtLeast(floatingUi, "45", 100);
    failures += expect(
      screenText == nullptr &&
        findText(floatingUi, "HEADSHOT 45") == nullptr &&
        countText(floatingUi, "45") == 2U &&
        backingText != nullptr &&
        floatingText != nullptr &&
        backingText != floatingText &&
        backingText->color.red < floatingText->color.red &&
        backingText->position.x > floatingText->position.x &&
        backingText->position.y > floatingText->position.y &&
        floatingText->position.x > 560.0F &&
        floatingText->position.x < 700.0F &&
        floatingText->position.y > 260.0F &&
        floatingText->position.y < 360.0F &&
        floatingText->color.red > floatingText->color.green &&
        floatingText->color.green < 96 &&
        floatingText->scale > 1.35F,
      "world damage tallies should project stored positions and support damage-scaled color, size, and headshot backing"
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
    std::array<bool, 9> foundWeaponValues = {};
    std::array<bool, 9> foundWeaponImages = {};
    std::array<bool, 9> foundWeaponImageTextPairs = {};
    std::size_t weaponHudShapeCount = 0;
    const lg::Text2D* fpsText = nullptr;
    constexpr std::array<std::string_view, 9> weaponValues = {
      "11",
      "22",
      "33",
      "44",
      "55",
      "66",
      "77",
      "88",
      "99",
    };
    for (std::size_t commandIndex = 0;
         commandIndex < ui.overlayCommands.size();
         ++commandIndex) {
      const lg::DrawCommand2D& command = ui.overlayCommands[commandIndex];
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
        if (const auto* image = std::get_if<lg::Image2D>(&command)) {
          const std::size_t imageIndex =
            static_cast<std::size_t>(image->image);
          if (imageIndex < foundWeaponImages.size()) {
            foundWeaponImages[imageIndex] = true;
            if (commandIndex + 1U < ui.overlayCommands.size()) {
              const auto* ammoText = std::get_if<lg::Text2D>(
                &ui.overlayCommands[commandIndex + 1U]
              );
              foundWeaponImageTextPairs[imageIndex] =
                ammoText != nullptr &&
                ammoText->text == weaponValues[imageIndex] &&
                ammoText->position.x < 80.0F;
            }
          }
        }
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
      weaponHudShapeCount >= 9,
      "weapon HUD should draw all nine compact left-edge icon images"
    );
    failures += expect(
      std::all_of(
        foundWeaponImages.begin(),
        foundWeaponImages.end(),
        [](bool found) { return found; }
      ),
      "weapon HUD should map every weapon to its supplied image"
    );
    failures += expect(
      std::all_of(
        foundWeaponImageTextPairs.begin(),
        foundWeaponImageTextPairs.end(),
        [](bool found) { return found; }
      ),
      "every weapon image should be followed by its ammo text for a separate texture batch"
    );
    bool foundAllWeaponValues = true;
    for (bool foundWeaponValue : foundWeaponValues) {
      foundAllWeaponValues = foundAllWeaponValues && foundWeaponValue;
    }
    failures += expect(
      foundAllWeaponValues,
      "selected weapon indicator should show all nine weapon values"
    );

    constexpr std::array<lg::Weapon, 9> weapons = {{
      lg::Weapon::MachineGun,
      lg::Weapon::Shotgun,
      lg::Weapon::GrenadeLauncher,
      lg::Weapon::RocketLauncher,
      lg::Weapon::LightningGun,
      lg::Weapon::Railgun,
      lg::Weapon::PlasmaGun,
      lg::Weapon::FreezeGun,
      lg::Weapon::Revolver,
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
        healthNumber->position.x == 584.0F &&
        healthNumber->horizontalAlignment ==
          lg::TextHorizontalAlignment::Right &&
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
        largeHealthNumber->position.x == healthNumber->position.x &&
        largeHealthNumber->horizontalAlignment ==
          lg::TextHorizontalAlignment::Right &&
        largeAmmo->position.x == ammo->position.x,
      "health style 2 should anchor HP and ammo near the crosshair without scaling their spacing apart"
    );

    crosshairHealthHud.weaponValues[3] = "\xE2\x88\x9E";
    const lg::DrawList2D infiniteAmmoUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      crosshairHealthSettings,
      crosshairHealthHud,
      console
    );
    const lg::Text2D* infiniteAmmo = nullptr;
    for (const lg::DrawCommand2D& command : infiniteAmmoUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        if (
          text->text == "\xE2\x88\x9E" &&
          text->position.x > 600.0F &&
          text->position.y > 360.0F &&
          text->position.y < 380.0F
        ) {
          infiniteAmmo = text;
        }
      }
    }
    failures += expect(
      infiniteAmmo != nullptr &&
        infiniteAmmo->position.x == 696.0F &&
        infiniteAmmo->position.y == 368.5F &&
        std::abs(infiniteAmmo->scale - 8.4F) < 0.001F,
      "health style 2 should optically scale the compact infinity ammo mark"
    );
  }

  {
    constexpr std::array<lg::HudImage, 3> healthImages = {{
      lg::HudImage::HealthSegmented,
      lg::HudImage::HealthFilled,
      lg::HudImage::HealthOutlined,
    }};
    for (int style = 3; style <= 5; ++style) {
      lg::RenderSettings artSettings = settings;
      artSettings.healthStyle = style;
      const lg::DrawList2D artUi = lg::buildScreenUi(
        1280,
        720,
        opponent,
        artSettings,
        hud,
        console
      );
      const lg::Image2D* healthImage = nullptr;
      for (const lg::DrawCommand2D& command : artUi.overlayCommands) {
        const auto* image = std::get_if<lg::Image2D>(&command);
        if (image != nullptr &&
            image->image == healthImages[static_cast<std::size_t>(style - 3)]) {
          healthImage = image;
          break;
        }
      }
      failures += expect(
        healthImage != nullptr && healthImage->destination.width > 0.0F &&
          healthImage->destination.width < 374.0F &&
          findText(artUi, "50") != nullptr,
        "art health styles should draw their mapped image with live half-health width and value"
      );
      const lg::Text2D* healthPlus = findText(artUi, "+");
      const lg::Text2D* healthValue = findText(artUi, "50");
      failures += expect(
        healthPlus != nullptr && healthValue != nullptr &&
          std::abs(
            healthValue->position.x - healthPlus->position.x - 34.0F
          ) < 0.001F &&
          std::abs(healthValue->position.y - healthPlus->position.y) < 0.001F &&
          std::abs(healthValue->position.x - 114.0F) < 0.001F &&
          std::abs(healthValue->scale - 2.6F) < 0.001F &&
          std::abs(healthPlus->scale - 2.6F) < 0.001F &&
          healthValue->position.y > 670.0F &&
          healthValue->position.y < 690.0F,
        "art health styles should align the nearby plus and health number with the bar"
      );
      if (style == 3 && healthImage != nullptr) {
        failures += expect(
          std::abs(healthImage->source.width - 0.5F) < 0.001F &&
            std::abs(healthImage->destination.width - 187.0F) < 0.001F,
          "segmented art health should keep its default size and crop to the live health ratio"
        );
      } else if (healthImage != nullptr) {
        failures += expect(
          healthImage->source.x > 0.0F &&
            healthImage->source.y > 0.0F &&
            healthImage->source.width < 0.5F &&
            healthImage->source.height < 1.0F,
          "filled art should sample only its texture and not redraw the PNG frame"
        );
      }
    }
  }

  {
    constexpr std::array<lg::HudImage, 3> healthImages = {{
      lg::HudImage::HealthSegmented,
      lg::HudImage::HealthFilled,
      lg::HudImage::HealthOutlined,
    }};
    lg::PlayerState fullHealthPlayer = opponent;
    fullHealthPlayer.health = 100;
    for (int style = 3; style <= 5; ++style) {
      lg::RenderSettings maximumHealthSizeSettings = settings;
      maximumHealthSizeSettings.healthStyle = style;
      maximumHealthSizeSettings.healthTextScale = 20.0F;
      const lg::DrawList2D maximumHealthSizeUi = lg::buildScreenUi(
        1280,
        720,
        fullHealthPlayer,
        maximumHealthSizeSettings,
        hud,
        console
      );
      const lg::Image2D* healthImage = nullptr;
      for (const lg::DrawCommand2D& command :
           maximumHealthSizeUi.overlayCommands) {
        const auto* image = std::get_if<lg::Image2D>(&command);
        if (image != nullptr &&
            image->image == healthImages[static_cast<std::size_t>(style - 3)]) {
          healthImage = image;
          break;
        }
      }
      const lg::Text2D* healthValue = findText(maximumHealthSizeUi, "100");
      failures += expect(
        healthImage != nullptr &&
          healthImage->destination.x >= 0.0F &&
          healthImage->destination.y >= 0.0F &&
          healthImage->destination.x + healthImage->destination.width <=
            1280.0F &&
          healthImage->destination.y + healthImage->destination.height <=
            720.0F &&
          healthValue != nullptr &&
          healthValue->position.x >= 0.0F &&
          healthValue->position.x <= 1280.0F &&
          healthValue->position.y >= 0.0F &&
          healthValue->position.y <= 720.0F,
        "art health styles should fit their full layout at cl_health_size 20"
      );
    }
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

    layoutHud.scoreboardLines = {"SCOREBOARD", "  NAME              SCORE ACC     DAMAGE"};
    for (std::size_t player = 0; player < lg::kDuelPlayerCount; ++player) {
      layoutHud.scoreboardLines.push_back(
        "  PLAYER " + std::to_string(player + 1U)
      );
    }
    const lg::DrawList2D fullRosterUi = lg::buildScreenUi(
      640,
      480,
      opponent,
      settings,
      layoutHud,
      console
    );
    const lg::Text2D* fullRosterTitle = findText(fullRosterUi, "SCOREBOARD");
    const lg::Text2D* lastRosterPlayer = findText(
      fullRosterUi,
      "PLAYER " + std::to_string(lg::kDuelPlayerCount)
    );
    failures += expect(
      fullRosterTitle != nullptr &&
        lastRosterPlayer != nullptr &&
        fullRosterTitle->position.y >= 0.0F &&
        lastRosterPlayer->position.y + 16.0F * lastRosterPlayer->scale <= 480.0F &&
        lastRosterPlayer->scale < 2.0F,
      "full player-capacity scoreboard should compact to a 640x480 viewport"
    );

    lg::HudRenderState ffaHud;
    ffaHud.scoreboardOpen = true;
    ffaHud.freeForAllScoreboard = true;
    for (std::size_t player = 0; player < lg::kDuelPlayerCount; ++player) {
      ffaHud.freeForAllScoreboardRows.push_back({
        player + 1U,
        static_cast<std::uint8_t>(player),
        player == 0U ? "LEADER" : "PLAYER " + std::to_string(player + 1U),
        static_cast<lg::PlayerScore>(12 - static_cast<int>(player)),
        lg::Weapon::Railgun,
        75,
        84,
        player == 3U,
      });
    }
    ffaHud.freeForAllStandingRows = {
      {1, 0, "LEADER", 12, false},
      {4, 3, "PLAYER 4", -3, true},
    };
    const lg::DrawList2D ffaSmallUi = lg::buildScreenUi(
      640,
      480,
      opponent,
      settings,
      ffaHud,
      console
    );
    const lg::Text2D* ffaTitle = findText(ffaSmallUi, "FREE FOR ALL");
    const lg::Text2D* ffaRank = findText(ffaSmallUi, "RANK");
    const lg::Text2D* ffaLocal = findText(ffaSmallUi, "> PLAYER 4");
    const lg::Text2D* ffaNegative = findText(ffaSmallUi, "-3");
    const lg::Text2D* ffaWeapon = findText(ffaSmallUi, "SR");
    const lg::Text2D* ffaDamage = findText(ffaSmallUi, "84");
    lg::ScreenRect standingBounds;
    failures += expect(
      ffaTitle != nullptr && ffaRank != nullptr && ffaLocal != nullptr &&
        ffaNegative != nullptr && ffaWeapon != nullptr && ffaDamage != nullptr &&
        hasFilledQuadColor(ffaSmallUi, {34, 91, 126, 150}),
      "FFA scoreboard should draw rank, local mark, signed score, weapon, and damage"
    );
    failures += expect(
      findFilledQuadBounds(ffaSmallUi, {7, 11, 17, 220}, standingBounds) &&
        ffaTitle != nullptr && ffaTitle->position.y >=
          standingBounds.y + standingBounds.height &&
        ffaLocal != nullptr && ffaLocal->position.y < 480.0F,
      "FFA standing and full Tab rows should fit together at 640x480"
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

    chatHud.chatLines.clear();
    for (int index = 1; index <= 12; ++index) {
      chatHud.chatLines.push_back({0, "message " + std::to_string(index), "yg"});
    }
    chatHud.chatScrollRows = 3U;
    chatHud.chatHistoryExpanded = true;
    const lg::ChatTextLayout scrolledLayout =
      lg::buildChatTextLayout(800, 720, chatHud);
    failures += expect(
      scrolledLayout.rows.size() == 8U &&
        scrolledLayout.rows.back().text == "yg: message 9" &&
        scrolledLayout.totalHistoryRows == 12U &&
        scrolledLayout.firstVisibleHistoryRow == 1U &&
        scrolledLayout.visibleHistoryRows == 8U &&
        scrolledLayout.maxScrollRows == 4U,
      "chat scroll offset should move the visible row window away from newest"
    );

    const lg::DrawList2D scrolledUi = lg::buildScreenUi(
      800,
      720,
      lg::PlayerState{},
      lg::RenderSettings{},
      chatHud,
      {}
    );
    const lg::Text2D* chatPosition = findText(scrolledUi, "ROWS 2-9 / 12");
    bool foundChatTrack = false;
    bool foundChatThumb = false;
    for (const lg::DrawCommand2D& command : scrolledUi.overlayCommands) {
      const auto* quad = std::get_if<lg::FilledQuad2D>(&command);
      if (quad == nullptr) {
        continue;
      }
      const float quadWidth = quad->points[1].x - quad->points[0].x;
      const float quadHeight = quad->points[2].y - quad->points[1].y;
      if (std::fabs(
            quad->points[0].x - (scrolledLayout.historyRight + 12.0F)
          ) < 0.01F && std::fabs(quadWidth - 8.0F) < 0.01F) {
        foundChatTrack = foundChatTrack || std::fabs(quadHeight - 144.0F) < 0.01F;
        foundChatThumb = foundChatThumb || std::fabs(quadHeight - 96.0F) < 0.01F;
      }
    }
    failures += expect(
      chatPosition != nullptr &&
        chatPosition->horizontalAlignment == lg::TextHorizontalAlignment::Left &&
        foundChatTrack && foundChatThumb,
      "scrollable chat should show its visible rows, total rows, track, and proportional thumb"
    );

    chatHud.chatLines.resize(2U);
    chatHud.chatScrollRows = 0U;
    const lg::DrawList2D shortChatUi = lg::buildScreenUi(
      800,
      720,
      lg::PlayerState{},
      lg::RenderSettings{},
      chatHud,
      {}
    );
    failures += expect(
      findText(shortChatUi, "ROWS 1-2 / 2") != nullptr,
      "held expanded chat should show its position indicator even when all history fits"
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

    chatHud.chatInputOpen = true;
    const lg::ChatTextLayout selectableChatLayout =
        lg::buildChatTextLayout(800, 720, chatHud);
    const std::size_t historySelectionStart = lg::chatHistoryTextOffsetAt(
        selectableChatLayout, selectableChatLayout.rows.front().x,
        selectableChatLayout.rows.front().y + 2.0F);
    const std::size_t historySelectionEnd = lg::chatHistoryTextOffsetAt(
        selectableChatLayout,
        selectableChatLayout.rows.front().x +
            6.0F * selectableChatLayout.characterWidth,
        selectableChatLayout.rows.front().y + 2.0F);
    failures += expect(
        !lg::chatHistorySelectedText(selectableChatLayout,
                                     historySelectionStart, historySelectionEnd)
             .empty(),
        "chat history mouse selection should return visible selected text");
    chatHud.chatHistoryHasSelection = true;
    chatHud.chatHistorySelectionAnchor = historySelectionStart;
    chatHud.chatHistorySelectionFocus = historySelectionEnd;
    const lg::DrawList2D selectedHistoryUi =
        lg::buildScreenUi(800, 720, {}, settings, chatHud, {});
    bool foundHistorySelection = false;
    for (const lg::DrawCommand2D &command : selectedHistoryUi.overlayCommands) {
      if (const auto *quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundHistorySelection =
            foundHistorySelection ||
            (quad->color.red == 58 && quad->color.green == 118 &&
             quad->color.blue == 188 && quad->color.alpha == 170);
      }
    }
    failures +=
        expect(foundHistorySelection,
               "chat history selection should draw a translucent highlight");
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

    selectableConsole.hasSelection = false;
    selectableConsole.inputHasSelection = true;
    selectableConsole.inputSelectionAnchor = 0U;
    selectableConsole.inputSelectionFocus = selectableConsole.input.size();
    const lg::DrawList2D selectedConsoleInputUi =
        lg::buildScreenUi(1280, 720, opponent, settings, {}, selectableConsole);
    bool foundInputSelectionHighlight = false;
    for (const lg::DrawCommand2D &command :
         selectedConsoleInputUi.overlayCommands) {
      if (const auto *quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundInputSelectionHighlight =
            foundInputSelectionHighlight ||
            (quad->color.red == 58 && quad->color.green == 118 &&
             quad->color.blue == 188 && quad->color.alpha == 170);
      }
    }
    failures +=
        expect(foundInputSelectionHighlight,
               "console Ctrl+A selection should highlight the editable input");
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

  {
    lg::ConsoleRenderState scrollConsole;
    scrollConsole.open = true;
    for (int line = 0; line < 30; ++line) {
      scrollConsole.lines.push_back("line " + std::to_string(line));
    }
    const lg::ConsoleTextLayout latest =
      lg::buildConsoleTextLayout(640, 360, scrollConsole);
    scrollConsole.scrollRows = 5U;
    const lg::ConsoleTextLayout scrolled =
      lg::buildConsoleTextLayout(640, 360, scrollConsole);
    failures += expect(
      !latest.lines.empty() && !scrolled.lines.empty() &&
        latest.lines.front().text != scrolled.lines.front().text,
      "console scrollback should move away from the newest output rows"
    );
    failures += expect(
      scrolled.lines.front().text == "line 17",
      "console scrollback should offset by the requested wrapped rows"
    );
    failures += expect(
      latest.maxScrollRows == 22U,
      "console scrollback should report its wrapped-row limit"
    );
    failures += expect(
      scrolled.lines.back().prompt,
      "console prompt should remain visible while output is scrolled"
    );
  }

  {
    lg::ConsoleCatController cat;
    cat.reset(1280.0F, 720.0F);
    bool sawCrouch = false;
    bool sawLeap = false;
    float highestY = cat.pose().position.y;
    for (int step = 0; step < 70; ++step) {
      cat.update(0.05F, 500.0F, 80.0F, 1280.0F, 720.0F);
      sawCrouch = sawCrouch ||
        cat.pose().action == lg::ConsoleCatAction::Crouch;
      sawLeap = sawLeap || cat.pose().action == lg::ConsoleCatAction::Leap;
      highestY = std::min(highestY, cat.pose().position.y);
    }
    failures += expect(sawCrouch, "console cat should crouch before pouncing");
    failures += expect(sawLeap, "console cat should pounce toward the pointer");
    failures += expect(
      highestY < 330.0F,
      "console cat pounce should visibly leave the console floor"
    );

    lg::ConsoleCatController lateralCat;
    lateralCat.reset(1280.0F, 720.0F);
    lateralCat.update(0.05F, 900.0F, 350.0F, 1280.0F, 720.0F);
    failures += expect(
      lateralCat.pose().profile,
      "console cat should show its profile during lateral movement"
    );
    bool keptProfileWhileCrouching = false;
    for (int step = 0; step < 28; ++step) {
      lateralCat.update(0.05F, 500.0F, 80.0F, 1280.0F, 720.0F);
      if (lateralCat.pose().action == lg::ConsoleCatAction::Crouch) {
        keptProfileWhileCrouching = lateralCat.pose().profile;
        break;
      }
    }
    failures += expect(
      keptProfileWhileCrouching,
      "console cat should remain side-on while preparing a lateral jump"
    );
    for (int step = 0; step < 80; ++step) {
      lateralCat.update(
        0.05F,
        lateralCat.pose().position.x,
        650.0F,
        1280.0F,
        720.0F
      );
    }
    failures += expect(
      !lateralCat.pose().profile,
      "console cat should face forward at the pointer or when it is below"
    );

    lg::ConsoleRenderState catConsole;
    catConsole.open = true;
    catConsole.cat = cat.pose();
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      catConsole
    );
    bool foundCalicoPatch = false;
    bool foundLaser = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundCalicoPatch = foundCalicoPatch ||
          (quad->color.red == 190 && quad->color.green == 132 && quad->color.blue == 73);
        foundLaser = foundLaser ||
          (quad->color.red == 255 && quad->color.green == 112 && quad->color.blue == 118);
      }
    }
    failures += expect(
      foundCalicoPatch,
      "console should render the cat's caramel calico markings"
    );
    failures += expect(foundLaser, "console should render the red laser-pointer target");

    catConsole.showCat = false;
    const lg::DrawList2D hiddenCatUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      catConsole
    );
    bool hiddenCatArt = false;
    for (const lg::DrawCommand2D& command : hiddenCatUi.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        hiddenCatArt = hiddenCatArt ||
          (quad->color.red == 190 && quad->color.green == 132 && quad->color.blue == 73) ||
          (quad->color.red == 255 && quad->color.green == 112 && quad->color.blue == 118);
      }
    }
    bool hiddenSleepMarker = false;
    for (const lg::DrawCommand2D& command : hiddenCatUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        hiddenSleepMarker = hiddenSleepMarker || text->text == "Z";
      }
    }
    failures += expect(
      !hiddenCatArt && !hiddenSleepMarker,
      "console cat toggle should emit no cat, laser, or sleep-marker commands"
    );

    cat.update(0.05F, 600.0F, 650.0F, 1280.0F, 720.0F);
    failures += expect(
      cat.pose().laser.y == 650.0F,
      "console laser target should follow the mouse below the console panel"
    );

    lg::ConsoleCatController overheadCat;
    overheadCat.reset(1280.0F, 720.0F);
    overheadCat.update(
      0.05F,
      overheadCat.pose().position.x,
      80.0F,
      1280.0F,
      720.0F
    );
    failures += expect(
      overheadCat.pose().action == lg::ConsoleCatAction::Crouch,
      "console cat should prepare a vertical pounce when the pointer is overhead"
    );

    lg::ConsoleCatController sleepyCat;
    sleepyCat.reset(1280.0F, 720.0F);
    const lg::ScreenPoint restingPointer = sleepyCat.pose().position;
    for (int step = 0; step < 60; ++step) {
      sleepyCat.update(
        0.05F,
        restingPointer.x,
        restingPointer.y,
        1280.0F,
        720.0F
      );
    }
    failures += expect(
      sleepyCat.pose().action == lg::ConsoleCatAction::Sleep,
      "console cat should fall asleep after two seconds without pointer movement"
    );
    lg::ConsoleRenderState sleepConsole;
    sleepConsole.open = true;
    sleepConsole.cat = sleepyCat.pose();
    const lg::DrawList2D sleepUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      sleepConsole
    );
    bool foundSleepZ = false;
    for (const lg::DrawCommand2D& command : sleepUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundSleepZ = foundSleepZ || text->text == "Z";
      }
    }
    failures += expect(foundSleepZ, "sleeping console cat should emit animated Zs");
    sleepConsole.showCat = false;
    const lg::DrawList2D hiddenSleepUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      sleepConsole
    );
    bool hiddenSleepCatCommand = false;
    for (const lg::DrawCommand2D& command : hiddenSleepUi.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        hiddenSleepCatCommand = hiddenSleepCatCommand || text->text == "Z";
      }
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        hiddenSleepCatCommand = hiddenSleepCatCommand ||
          (quad->color.red == 190 && quad->color.green == 132 && quad->color.blue == 73) ||
          (quad->color.red == 255 && quad->color.green == 112 && quad->color.blue == 118);
      }
    }
    failures += expect(
      !hiddenSleepCatCommand,
      "disabled console cat should emit no sleeping-cat, shadow, or laser commands"
    );
    sleepyCat.update(
      0.05F,
      restingPointer.x + 20.0F,
      restingPointer.y,
      1280.0F,
      720.0F
    );
    failures += expect(
      sleepyCat.pose().action != lg::ConsoleCatAction::Sleep,
      "pointer movement should wake the sleeping console cat"
    );
  }

  {
    lg::HudRenderState netHud;
    netHud.netGraph.mode = 2;
    netHud.netGraph.interpolationEffectiveDelayMilliseconds = 24.0F;
    netHud.netGraph.interpolationBufferLeadTicks = 2.75;
    netHud.netGraph.interpolationDesiredBufferLeadTicks = 3.0;
    netHud.netGraph.interpolationTimelineErrorTicks = 0.25;
    netHud.netGraph.interpolationPlaybackRate = 1.015F;
    netHud.netGraph.interpolationBufferedSnapshotCount = 4;
    netHud.netGraph.interpolationPlaybackStarted = true;
    netHud.netGraph.interpolationUnderrun = true;
    netHud.netGraph.interpolationUnderrunCount = 5;
    netHud.netGraph.interpolationHardCorrectionCount = 2;
    netHud.netGraph.interpolationPresentationTick = 102.25;
    netHud.netGraph.interpolationNewestSnapshotTick = 105.0;
    netHud.netGraph.interpolationSampleTick = 102;
    netHud.netGraph.interpolationSampleEligible = true;
    netHud.netGraph.pendingCommands = 3;
    netHud.netGraph.correctionCount = 7;
    netHud.netGraph.lastCorrectionDistance = 0.125F;
    netHud.netGraph.requestedRewindTicks = 5;
    netHud.netGraph.appliedRewindTicks = 4;
    netHud.netGraph.telemetry.valid = true;
    netHud.netGraph.telemetry.pingMilliseconds = 34.0F;
    netHud.netGraph.telemetry.snapshotJitterMilliseconds = 3.0F;
    netHud.netGraph.telemetry.incomingLossPercent = 0.4F;
    netHud.netGraph.telemetry.outgoingLossPercent = 0.2F;
    netHud.netGraph.telemetry.snapshotRate = 124.0F;
    netHud.netGraph.telemetry.lastSnapshotBytes = 1014;
    netHud.netGraph.telemetry.lastCommandBytes = 640;
    netHud.netGraph.telemetry.historyCount = 3;
    netHud.netGraph.telemetry.history[0].serial = 1;
    netHud.netGraph.telemetry.history[0].snapshotJitterMilliseconds = 2.0F;
    netHud.netGraph.telemetry.history[0].interpolationUnderrun = true;
    netHud.netGraph.telemetry.history[0].predictionCorrectionDistance = 0.002F;
    netHud.netGraph.telemetry.history[1].serial = 2;
    netHud.netGraph.telemetry.history[1].snapshotGaps = 1;
    netHud.netGraph.telemetry.history[2].serial = 3;
    netHud.netGraph.telemetry.history[2].predictionCorrectionDistance = 0.125F;
    netHud.netGraph.telemetry.history[2].interpolationHardCorrection = true;
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      netHud,
      {}
    );
    bool foundLossBar = false;
    bool foundCorrectionBar = false;
    bool foundUnderrunEvent = false;
    bool foundHardCorrectionEvent = false;
    float shortestCorrectionBar = 10000.0F;
    float tallestCorrectionBar = 0.0F;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundLossBar = foundLossBar ||
          (quad->color.red == 244 && quad->color.green == 72);
        foundUnderrunEvent = foundUnderrunEvent ||
          (quad->color.red == 255 && quad->color.green == 80 &&
           quad->color.blue == 190);
        foundHardCorrectionEvent = foundHardCorrectionEvent ||
          (quad->color.red == 64 && quad->color.green == 220 &&
           quad->color.blue == 255);
        if (quad->color.blue == 255 && quad->color.red >= 70 &&
            quad->color.red <= 90 && quad->color.green >= 120 &&
            quad->color.green <= 150) {
          foundCorrectionBar = true;
          const auto [minimumY, maximumY] = std::minmax_element(
            quad->points.begin(),
            quad->points.end(),
            [](lg::ScreenPoint lhs, lg::ScreenPoint rhs) {
              return lhs.y < rhs.y;
            }
          );
          const float correctionBarHeight = maximumY->y - minimumY->y;
          shortestCorrectionBar = std::min(
            shortestCorrectionBar,
            correctionBarHeight
          );
          tallestCorrectionBar = std::max(
            tallestCorrectionBar,
            correctionBarHeight
          );
        }
      }
    }
    failures += expect(
        findText(ui, "NETWORK") != nullptr &&
        findText(ui, "PING") != nullptr &&
        findText(ui, "LOSS IN") != nullptr &&
        findText(ui, "LEAD/TARGET") != nullptr &&
        findText(ui, "2.75 / 3.00 tk") != nullptr &&
        findText(ui, "ERROR +0.25 tk  RATE 1.015x") != nullptr &&
        findText(ui, "DELAY 24.0 ms  SNAPS 4") != nullptr &&
        findText(ui, "PLAY ON  UNDERRUN ACTIVE") != nullptr &&
        findText(ui, "EVENTS UNDER 5  HARD 2") != nullptr &&
        findText(ui, "TICK P/N 102.25 / 105") != nullptr &&
        findText(ui, "COLLISION TICK 102  VALID") != nullptr &&
        findText(ui, "CORR 0.125  AVG 0.064  MAX 0.125") != nullptr,
      "expanded netgraph should render interpolation controller diagnostics"
    );
    failures += expect(
      findText(ui, "NETWORK")->scale >= 1.7F,
      "netgraph should use a legible scaled default"
    );
    failures += expect(
      foundLossBar && foundCorrectionBar &&
        foundUnderrunEvent && foundHardCorrectionEvent &&
        tallestCorrectionBar > shortestCorrectionBar * 2.0F &&
        tallestCorrectionBar < 36.0F,
      "expanded netgraph should separate event bars from compact correction magnitudes"
    );
    const lg::Text2D* lossLegend = findText(ui, "LOSS");
    const lg::Text2D* lateLegend = findText(ui, "LATE");
    const lg::Text2D* underrunLegend = findText(ui, "UNDER");
    const lg::Text2D* hardCorrectionLegend = findText(ui, "HARD");
    const lg::Text2D* correctionLegend = findText(ui, "PRED");
    failures += expect(
      lossLegend != nullptr && lossLegend->color.red == 244 &&
        lateLegend != nullptr && lateLegend->color.green == 195 &&
        underrunLegend != nullptr && underrunLegend->color.green == 80 &&
        hardCorrectionLegend != nullptr &&
        hardCorrectionLegend->color.green == 220 &&
        correctionLegend != nullptr && correctionLegend->color.blue == 255,
      "netgraph legend labels should match their graph indicator colors"
    );

    netHud.netGraph.mode = 1;
    const lg::DrawList2D compactUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      netHud,
      {}
    );
    failures += expect(
      findText(compactUi, "LEAD/TARGET") != nullptr &&
        findText(compactUi, "2.75 / 3.00 tk") != nullptr &&
        findText(compactUi, "ERROR +0.25 tk  RATE 1.015x") == nullptr,
      "compact netgraph should show controller lead without expanded detail"
    );
  }

  {
    lg::RenderSettings scopeSettings;
    lg::HudRenderState scopeHud;
    scopeHud.sniperScopeActive = true;
    scopeHud.sniperScopeAmount = 1.0F;
    scopeHud.sniperChargePercent = 73;
    const lg::DrawList2D scopeUi = lg::buildScreenUi(
      1920, 1200, {}, scopeSettings, scopeHud, {}
    );
    const lg::DrawList2D unscopedUi = lg::buildScreenUi(
      1920, 1200, {}, scopeSettings, {}, {}
    );
    const lg::SniperScopeOverlay2D* scopeOverlay = nullptr;
    std::size_t scopeOverlayCount = 0;
    for (const lg::DrawCommand2D& command : scopeUi.overlayCommands) {
      if (
        const auto* candidate =
          std::get_if<lg::SniperScopeOverlay2D>(&command)
      ) {
        scopeOverlay = candidate;
        ++scopeOverlayCount;
      }
    }
    failures += expect(
      scopeOverlayCount == 1U &&
        scopeOverlay != nullptr &&
        findText(scopeUi, "73%") != nullptr,
      "Sniper Rifle ADS should draw one cached scope overlay and charge readout"
    );
    failures += expect(
      scopeOverlay != nullptr &&
        std::fabs(scopeOverlay->center.x - 960.0F) < 0.01F &&
        std::fabs(scopeOverlay->center.y - 600.0F) < 0.01F &&
        std::fabs(scopeOverlay->radius - 552.0F) < 0.01F &&
        std::fabs(scopeOverlay->openingScale - 1.0F) < 0.01F &&
        std::fabs(scopeOverlay->opacity - 1.0F) < 0.01F,
      "Sniper scope overlay should keep a circular, fully open wide-screen lens"
    );
    failures += expect(
      scopeUi.overlayCommands.size() <=
        unscopedUi.overlayCommands.size() + 4U,
      "Sniper scope should add only a bounded number of UI commands"
    );

    std::array<lg::SniperScopeOverlay2D, 3> fovOverlays = {};
    bool foundAllFovOverlays = true;
    constexpr std::array<float, 3> kSniperFovs = {20.0F, 45.0F, 140.0F};
    for (std::size_t index = 0; index < kSniperFovs.size(); ++index) {
      scopeSettings.fieldOfView = kSniperFovs[index];
      const lg::DrawList2D fovUi = lg::buildScreenUi(
        1920, 1200, {}, scopeSettings, scopeHud, {}
      );
      bool found = false;
      for (const lg::DrawCommand2D& command : fovUi.overlayCommands) {
        if (
          const auto* overlay =
            std::get_if<lg::SniperScopeOverlay2D>(&command)
        ) {
          fovOverlays[index] = *overlay;
          found = true;
          break;
        }
      }
      foundAllFovOverlays = foundAllFovOverlays && found;
    }
    failures += expect(
      foundAllFovOverlays &&
        fovOverlays[0].center.x == fovOverlays[1].center.x &&
        fovOverlays[1].center.x == fovOverlays[2].center.x &&
        fovOverlays[0].center.y == fovOverlays[1].center.y &&
        fovOverlays[1].center.y == fovOverlays[2].center.y &&
        fovOverlays[0].radius == fovOverlays[1].radius &&
        fovOverlays[1].radius == fovOverlays[2].radius &&
        fovOverlays[0].openingScale == fovOverlays[1].openingScale &&
        fovOverlays[1].openingScale == fovOverlays[2].openingScale,
      "scope overlay geometry should not change at low, default, or high FOV"
    );
  }

  {
    constexpr float kPi = 3.14159265359F;
    const lg::PerspectiveCamera camera = lg::makePerspectiveCamera(
      {},
      0.0F,
      0.0F,
      90.0F,
      16.0F / 9.0F
    );
    lg::McGuffinNavigationTarget target;
    target.active = true;
    target.kind = lg::McGuffinNavigationKind::Objective;
    target.worldPosition = {10.0F, 0.0F, 0.0F};
    const lg::McGuffinNavigationProjection centered =
      lg::projectMcGuffinNavigationTarget(target, camera, 1280, 720);
    failures += expect(
      centered.valid && centered.onScreen && !centered.behind &&
        std::fabs(centered.screenPosition.x - 640.0F) < 0.01F &&
        std::fabs(centered.screenPosition.y - 360.0F) < 0.01F &&
        std::fabs(centered.distance - 10.0F) < 0.01F,
      "an in-view objective should project to its screen position and distance"
    );

    target.worldPosition = {10.0F, -30.0F, 0.0F};
    const lg::McGuffinNavigationProjection rightEdge =
      lg::projectMcGuffinNavigationTarget(target, camera, 1280, 720);
    failures += expect(
      rightEdge.valid && !rightEdge.onScreen && !rightEdge.behind &&
        rightEdge.edgePosition.x >= 76.0F &&
        rightEdge.edgePosition.x <= 1204.0F &&
        rightEdge.edgePosition.y >= 100.0F &&
        rightEdge.edgePosition.y <= 620.0F,
      "an off-screen objective should clamp to the safe screen edge"
    );

    target.worldPosition = {-10.0F, 0.0F, 0.0F};
    const lg::McGuffinNavigationProjection behind =
      lg::projectMcGuffinNavigationTarget(target, camera, 1280, 720);
    failures += expect(
      behind.valid && behind.behind && !behind.onScreen &&
        std::isfinite(behind.edgePosition.x) &&
        std::isfinite(behind.edgePosition.y) &&
        behind.edgePosition.y >= 100.0F && behind.edgePosition.y <= 620.0F,
      "a target behind the camera should produce a finite turn cue"
    );

    target.worldPosition = {0.00001F, 0.0F, 0.0F};
    const lg::McGuffinNavigationProjection nearZero =
      lg::projectMcGuffinNavigationTarget(target, camera, 1280, 720);
    failures += expect(
      nearZero.valid && nearZero.onScreen &&
        std::isfinite(nearZero.screenPosition.x) &&
        std::isfinite(nearZero.screenPosition.y),
      "a reached objective should not create a zero-vector edge arrow"
    );

    const lg::PerspectiveCamera positivePiCamera = lg::makePerspectiveCamera(
      {}, kPi, 0.0F, 90.0F, 16.0F / 9.0F
    );
    const lg::PerspectiveCamera negativePiCamera = lg::makePerspectiveCamera(
      {}, -kPi, 0.0F, 90.0F, 16.0F / 9.0F
    );
    target.worldPosition = {-10.0F, 0.0F, 0.0F};
    const lg::McGuffinNavigationProjection positivePi =
      lg::projectMcGuffinNavigationTarget(
        target,
        positivePiCamera,
        1280,
        720
      );
    const lg::McGuffinNavigationProjection negativePi =
      lg::projectMcGuffinNavigationTarget(
        target,
        negativePiCamera,
        1280,
        720
      );
    failures += expect(
      positivePi.onScreen && negativePi.onScreen &&
        std::fabs(positivePi.screenPosition.x - 640.0F) < 0.01F &&
        std::fabs(negativePi.screenPosition.x - 640.0F) < 0.01F,
      "navigation projection should remain stable across yaw angle wrap"
    );

    target.worldPosition = {10.0F, -30.0F, 0.0F};
    const lg::PerspectiveCamera wideCamera = lg::makePerspectiveCamera(
      {}, 0.0F, 0.0F, 90.0F, 2.0F
    );
    const lg::McGuffinNavigationProjection resized =
      lg::projectMcGuffinNavigationTarget(target, wideCamera, 1600, 900);
    failures += expect(
      resized.valid && !resized.onScreen &&
        resized.edgePosition.x >= 95.0F && resized.edgePosition.x <= 1505.0F &&
        resized.edgePosition.y >= 125.0F && resized.edgePosition.y <= 775.0F,
      "navigation projection should recompute safe bounds after aspect changes"
    );

    target.active = false;
    failures += expect(
      !lg::projectMcGuffinNavigationTarget(target, camera, 1280, 720).valid,
      "inactive navigation state should emit no projection"
    );

    lg::PlayerState navigationPlayer;
    lg::RenderSettings navigationSettings;
    lg::HudRenderState navigationHud;
    navigationHud.mcguffinNavigation = {
      true,
      lg::McGuffinNavigationKind::Objective,
      {10.0F, -30.0F, 0.0F},
    };
    const lg::DrawList2D baselineUi = lg::buildScreenUi(
      1280,
      720,
      navigationPlayer,
      navigationSettings,
      {},
      {},
      &camera
    );
    const lg::DrawList2D edgeUi = lg::buildScreenUi(
      1280,
      720,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &camera
    );
    failures += expect(
      findText(edgeUi, "OBJECTIVE 32u") != nullptr &&
        edgeUi.overlayCommands.size() == baselineUi.overlayCommands.size() + 7U &&
        hasFilledQuadColor(edgeUi, {255, 224, 96, 245}),
      "off-screen navigation should draw one labeled card and one edge arrow"
    );

    navigationHud.mcguffinNavigation.worldPosition = {10.0F, 0.0F, 0.0F};
    const lg::DrawList2D onScreenUi = lg::buildScreenUi(
      1280,
      720,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &camera
    );
    failures += expect(
      findText(onScreenUi, "OBJECTIVE 10u") != nullptr &&
        onScreenUi.overlayCommands.size() == baselineUi.overlayCommands.size() + 6U &&
        !hasFilledQuadColor(onScreenUi, {255, 224, 96, 245}),
      "an in-view objective should show a quiet card without an edge arrow"
    );

    navigationHud.topCenterLines = {
      "SCORE 50 / 100",
      "MCGUFFIN CARRIED",
      "YOU HAVE THE MCGUFFIN",
      "OBJECTIVE THROWN",
      "EVENT",
    };
    navigationHud.mcguffinNavigation.worldPosition = {10.0F, 0.0F, 30.0F};
    const lg::DrawList2D topEdgeUi = lg::buildScreenUi(
      1280,
      720,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &camera
    );
    failures += expect(
      minimumYForFilledQuadColor(topEdgeUi, {255, 224, 96, 245}) > 108.0F,
      "a top-edge arrow should stay below a multi-line score and objective HUD"
    );

    navigationHud.settingsOpen = true;
    const lg::DrawList2D modalUi = lg::buildScreenUi(
      1280,
      720,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &camera
    );
    failures += expect(
      findText(modalUi, "OBJECTIVE 10u") == nullptr,
      "modal HUD layers should suppress objective navigation"
    );

    navigationHud.settingsOpen = false;
    navigationHud.topCenterLines.clear();
    const auto expectedSafeBounds = [](int width, int height) {
      const float outputWidth = static_cast<float>(width);
      const float outputHeight = static_cast<float>(height);
      const float horizontalMargin = std::min(
        outputWidth * 0.25F,
        std::max(24.0F, outputWidth * 0.06F)
      );
      const float verticalMargin = std::min(
        outputHeight * 0.30F,
        std::max(56.0F, outputHeight * 0.14F)
      );
      return lg::ScreenRect{
        horizontalMargin,
        verticalMargin,
        outputWidth - horizontalMargin * 2.0F,
        outputHeight - verticalMargin * 2.0F,
      };
    };
    navigationHud.mcguffinNavigation.worldPosition = {10.0F, -15.0F, -9.0F};
    const lg::PerspectiveCamera wideNavigationCamera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 16.0F / 9.0F);
    const lg::DrawList2D wideNearEdgeUi = lg::buildScreenUi(
      1920,
      1080,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &wideNavigationCamera
    );
    lg::ScreenRect wideCard;
    const lg::ScreenRect wideSafe = expectedSafeBounds(1920, 1080);
    failures += expect(
      findFilledQuadBounds(wideNearEdgeUi, {7, 12, 17, 212}, wideCard) &&
        !hasFilledQuadColor(wideNearEdgeUi, {255, 224, 96, 245}) &&
        wideCard.x >= wideSafe.x - 0.1F &&
        wideCard.y >= wideSafe.y - 0.1F &&
        wideCard.x + wideCard.width <= wideSafe.x + wideSafe.width + 0.1F &&
        wideCard.y + wideCard.height <= wideSafe.y + wideSafe.height + 0.1F,
      "wide near-edge objective cards should stay inside the safe area"
    );

    navigationHud.mcguffinNavigation.worldPosition = {10.0F, -3.0F, -9.0F};
    const lg::PerspectiveCamera tallNavigationCamera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 600.0F / 1600.0F);
    const lg::DrawList2D tallNearEdgeUi = lg::buildScreenUi(
      600,
      1600,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &tallNavigationCamera
    );
    lg::ScreenRect tallCard;
    const lg::ScreenRect tallSafe = expectedSafeBounds(600, 1600);
    failures += expect(
      findFilledQuadBounds(tallNearEdgeUi, {7, 12, 17, 212}, tallCard) &&
        !hasFilledQuadColor(tallNearEdgeUi, {255, 224, 96, 245}) &&
        tallCard.x >= tallSafe.x - 0.1F &&
        tallCard.y >= tallSafe.y - 0.1F &&
        tallCard.x + tallCard.width <= tallSafe.x + tallSafe.width + 0.1F &&
        tallCard.y + tallCard.height <= tallSafe.y + tallSafe.height + 0.1F,
      "tall near-edge objective cards should stay inside the safe area"
    );

    navigationHud.mcguffinNavigation.worldPosition = {10.0F, -4.0F, -9.0F};
    const lg::PerspectiveCamera narrowNavigationCamera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 120.0F / 240.0F);
    const lg::DrawList2D narrowNearEdgeUi = lg::buildScreenUi(
      120,
      240,
      navigationPlayer,
      navigationSettings,
      navigationHud,
      {},
      &narrowNavigationCamera
    );
    lg::ScreenRect narrowCard;
    const lg::ScreenRect narrowSafe = expectedSafeBounds(120, 240);
    failures += expect(
      findText(narrowNearEdgeUi, "OBJ 14u") != nullptr &&
        findFilledQuadBounds(narrowNearEdgeUi, {7, 12, 17, 212}, narrowCard) &&
        !hasFilledQuadColor(narrowNearEdgeUi, {255, 224, 96, 245}) &&
        narrowCard.x >= narrowSafe.x - 0.1F &&
        narrowCard.y >= narrowSafe.y - 0.1F &&
        narrowCard.x + narrowCard.width <= narrowSafe.x + narrowSafe.width + 0.1F &&
        narrowCard.y + narrowCard.height <= narrowSafe.y + narrowSafe.height + 0.1F,
      "a label wider than a narrow safe area should use a compact card without invalid clamping"
    );
  }

  {
    lg::HudRenderState killcamHud;
    killcamHud.killcam.active = true;
    killcamHud.killcam.killer = "RANGER";
    killcamHud.killcam.weapon = "RAILGUN";
    killcamHud.killcam.cause = "DIRECT";
    killcamHud.killcam.progress = 0.5F;
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      killcamHud,
      {}
    );
    failures += expect(
      findText(ui, "KILLCAM") != nullptr &&
        findText(ui, "KILLED BY RANGER - RAILGUN") != nullptr &&
        findText(ui, "DIRECT") != nullptr &&
        findText(ui, "SPACE/ESC: SKIP") != nullptr,
      "killcam HUD should show label, killer, weapon, cause, and skip prompt"
    );
    failures += expect(
      hasFilledQuadColor(ui, {235, 90, 70, 255}),
      "killcam HUD should show replay progress"
    );
  }

  return failures == 0 ? 0 : 1;
}
