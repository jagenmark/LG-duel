#include "render/ScreenUi.hpp"
#include "app/TextInput.hpp"
#include "render/ChatLayout.hpp"
#include "render/ConsoleLayout.hpp"
#include "sim/Combat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace lg {
namespace {

constexpr float kGlyphSize = 8.0F;
constexpr float kTwoPi = 6.28318530718F;
constexpr float kHalfPi = 1.57079632679F;

[[nodiscard]] float snappedTextScale(float scale) {
  constexpr std::array<float, 10> pixelHeights = {
    8.0F,
    12.0F,
    16.0F,
    24.0F,
    32.0F,
    48.0F,
    64.0F,
    96.0F,
    128.0F,
    160.0F,
  };
  const float targetHeight = kGlyphSize * std::max(0.1F, scale);
  float nearest = pixelHeights.front();
  float nearestDistance = std::abs(targetHeight - nearest);
  for (const float pixelHeight : pixelHeights) {
    const float distance = std::abs(targetHeight - pixelHeight);
    if (distance < nearestDistance) {
      nearest = pixelHeight;
      nearestDistance = distance;
    }
  }
  return nearest / kGlyphSize;
}

[[nodiscard]] float textWidth(std::string_view text, float scale) {
  return static_cast<float>(utf8GlyphCount(text)) *
    kGlyphSize *
    snappedTextScale(scale);
}

[[nodiscard]] std::string trimCell(std::string_view value) {
  while (!value.empty() && value.front() == ' ') {
    value.remove_prefix(1U);
  }
  while (!value.empty() && value.back() == ' ') {
    value.remove_suffix(1U);
  }
  return std::string(value);
}

[[nodiscard]] float countdownGlyphOffsetX(
  const std::string& text,
  float scale
) {
  const char finalDigit = text.back();
  return (finalDigit == '0' || finalDigit == '4' ? 0.5F : 1.0F) * scale;
}

void addRect(
  DrawList2D& drawList,
  float x,
  float y,
  float width,
  float height,
  RenderColor color
) {
  const std::array<ScreenPoint, 4> points = {{
    {x, y},
    {x + width, y},
    {x + width, y + height},
    {x, y + height},
  }};
  drawList.overlayCommands.emplace_back(FilledQuad2D{points, color});
}

void addLine(
  DrawList2D& drawList,
  ScreenPoint start,
  ScreenPoint end,
  RenderColor color,
  float width
) {
  drawList.overlayCommands.emplace_back(Line2D{
    start,
    end,
    color,
    width,
  });
}

void addText(
  DrawList2D& drawList,
  float x,
  float y,
  std::string text,
  RenderColor color,
  float scale,
  TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left
) {
  drawList.overlayCommands.emplace_back(Text2D{
    {x, y},
    std::move(text),
    color,
    scale,
    horizontalAlignment,
  });
}

[[nodiscard]] std::uint8_t blendChannel(
  std::uint8_t base,
  std::uint8_t highlight,
  float amount
) {
  return static_cast<std::uint8_t>(
    std::clamp(
      static_cast<float>(base) +
        (static_cast<float>(highlight) - static_cast<float>(base)) * amount,
      0.0F,
      255.0F
    )
  );
}

[[nodiscard]] RenderColor lerpColor(
  RenderColor a,
  RenderColor b,
  float amount
) {
  const float t = std::clamp(amount, 0.0F, 1.0F);
  return {
    blendChannel(a.red, b.red, t),
    blendChannel(a.green, b.green, t),
    blendChannel(a.blue, b.blue, t),
    blendChannel(a.alpha, b.alpha, t),
  };
}

[[nodiscard]] RenderColor localHealthFillColor(float healthRatio) {
  constexpr RenderColor red = {220, 38, 38, 255};
  constexpr RenderColor yellow = {228, 206, 42, 255};
  constexpr RenderColor green = {64, 214, 34, 255};
  const float ratio = std::clamp(healthRatio, 0.0F, 1.0F);
  if (ratio >= 0.75F) {
    return green;
  }
  if (ratio >= 0.5F) {
    return lerpColor(yellow, green, (ratio - 0.5F) / 0.25F);
  }
  return lerpColor(red, yellow, ratio / 0.5F);
}

void addOutline(
  DrawList2D& drawList,
  float x,
  float y,
  float width,
  float height,
  RenderColor color
) {
  addLine(drawList, {x, y}, {x + width, y}, color, 1.0F);
  addLine(
    drawList,
    {x + width, y},
    {x + width, y + height},
    color,
    1.0F
  );
  addLine(
    drawList,
    {x + width, y + height},
    {x, y + height},
    color,
    1.0F
  );
  addLine(drawList, {x, y + height}, {x, y}, color, 1.0F);
}

void addLocalHealthBar(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings,
  float bottomY
) {
  (void)height;
  const float scale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  const float labelScale = std::max(0.75F, scale * 0.72F);
  const std::string value =
    std::to_string(std::max(0, localPlayer.health)) + " / " +
    std::to_string(std::max(1, hud.healthAmount));
  const float valueWidth = textWidth(value, labelScale);
  const float barWidth = std::min(
    static_cast<float>(width) - 24.0F,
    std::max(60.0F * scale, valueWidth + 8.0F * scale)
  );
  const float barHeight = 13.0F * scale;
  const float border = std::max(1.0F, std::round(1.0F * scale));
  const float padding = std::max(2.0F, std::round(2.0F * scale));
  const float labelHeight = kGlyphSize * labelScale;
  const float labelGap = std::max(4.0F, 3.0F * scale);
  const float totalHeight = labelHeight + labelGap + barHeight + border * 2.0F;
  const float x = std::min(
    24.0F * scale,
    std::max(12.0F, static_cast<float>(width) - barWidth - 12.0F)
  );
  const float y = bottomY - totalHeight;
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(localPlayer.health) / maxHealth, 0.0F, 1.0F);
  addText(drawList, x, y, "HP", {240, 246, 252, 255}, labelScale);
  addText(
    drawList,
    x + barWidth - valueWidth,
    y,
    value,
    {240, 246, 252, 255},
    labelScale
  );
  const float barY = y + labelHeight + labelGap + border;
  addRect(
    drawList,
    x - border,
    barY - border,
    barWidth + border * 2.0F,
    barHeight + border * 2.0F,
    {5, 16, 9, 210}
  );
  addOutline(
    drawList,
    x - border,
    barY - border,
    barWidth + border * 2.0F,
    barHeight + border * 2.0F,
    {69, 226, 42, 255}
  );
  addRect(drawList, x, barY, barWidth, barHeight, {7, 10, 8, 210});
  addRect(
    drawList,
    x + padding,
    barY + padding,
    std::max(0.0F, (barWidth - padding * 2.0F) * healthRatio),
    std::max(0.0F, barHeight - padding * 2.0F),
    localHealthFillColor(healthRatio)
  );
}

void addLocalHealthNumber(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  const float scale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(localPlayer.health) / maxHealth, 0.0F, 1.0F);
  const std::string text = std::to_string(std::max(0, localPlayer.health));
  const float x = static_cast<float>(width) * 0.5F;
  const float y =
    static_cast<float>(height) - 24.0F - kGlyphSize * snappedTextScale(scale);
  addText(
    drawList,
    x,
    y,
    text,
    localHealthFillColor(healthRatio),
    scale,
    TextHorizontalAlignment::Center
  );
}

[[nodiscard]] RenderColor quakeLiveWeaponColor(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return {255, 232, 92, 255};
  case Weapon::Shotgun:
    return {255, 150, 64, 255};
  case Weapon::GrenadeLauncher:
    return {80, 224, 96, 255};
  case Weapon::RocketLauncher:
    return {255, 72, 54, 255};
  case Weapon::LightningGun:
    return {245, 244, 168, 255};
  case Weapon::Railgun:
    return {72, 232, 112, 255};
  case Weapon::PlasmaGun:
    return {190, 82, 255, 255};
  }
  return {230, 238, 246, 255};
}

[[nodiscard]] std::size_t weaponHudSlotIndex(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return 0;
  case Weapon::Shotgun:
    return 1;
  case Weapon::GrenadeLauncher:
    return 2;
  case Weapon::RocketLauncher:
    return 3;
  case Weapon::LightningGun:
    return 4;
  case Weapon::Railgun:
    return 5;
  case Weapon::PlasmaGun:
    return 6;
  }
  return 4;
}

void addCrosshairHealthAndAmmo(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  const float scale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(localPlayer.health) / maxHealth, 0.0F, 1.0F);
  const std::string healthText = std::to_string(std::max(0, localPlayer.health));
  const std::string ammoText = hud.weaponValues[weaponHudSlotIndex(hud.selectedWeapon)];
  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float y = centerY + std::max(22.0F, settings.crosshairGap + settings.crosshairSize + 10.0F);
  constexpr float sideOffset = 56.0F;
  const float healthWidth = textWidth(healthText, scale);

  addText(
    drawList,
    centerX - sideOffset - healthWidth,
    y,
    healthText,
    localHealthFillColor(healthRatio),
    scale
  );
  addText(
    drawList,
    centerX + sideOffset,
    y,
    ammoText,
    quakeLiveWeaponColor(hud.selectedWeapon),
    scale
  );
}

void addSpeedText(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  if (hud.speedText.empty()) {
    return;
  }

  const float scale = std::clamp(settings.speedTextScale, 0.5F, 6.0F);
  const float snappedScale = snappedTextScale(scale);
  const float crosshairReach = settings.crosshairEnabled
    ? settings.crosshairGap + settings.crosshairSize
    : 0.0F;
  const float y =
    static_cast<float>(height) * 0.5F +
    std::max(24.0F, crosshairReach + 14.0F) +
    snappedScale * 2.0F;
  addText(
    drawList,
    static_cast<float>(width) * 0.5F,
    y,
    hud.speedText,
    {230, 238, 246, 225},
    scale,
    TextHorizontalAlignment::Center
  );
}

void addWeaponIcon(
  DrawList2D& drawList,
  float centerX,
  float centerY,
  Weapon weapon,
  RenderColor color,
  float scale
) {
  const auto rect = [&](float x, float y, float width, float height) {
    addRect(
      drawList,
      centerX + x * scale,
      centerY + y * scale,
      width * scale,
      height * scale,
      color
    );
  };
  const auto quad = [&](std::array<ScreenPoint, 4> points) {
    for (ScreenPoint& point : points) {
      point.x = centerX + point.x * scale;
      point.y = centerY + point.y * scale;
    }
    drawList.overlayCommands.emplace_back(FilledQuad2D{points, color});
  };
  const auto line =
    [&](float x1, float y1, float x2, float y2, float width) {
      addLine(
        drawList,
        {centerX + x1 * scale, centerY + y1 * scale},
        {centerX + x2 * scale, centerY + y2 * scale},
        color,
        width * scale
      );
    };
  const auto lineColor = [&](
    float x1,
    float y1,
    float x2,
    float y2,
    float width,
    RenderColor lineRenderColor
  ) {
    addLine(
      drawList,
      {centerX + x1 * scale, centerY + y1 * scale},
      {centerX + x2 * scale, centerY + y2 * scale},
      lineRenderColor,
      width * scale
    );
  };
  const auto ringAt = [&](
    float x,
    float y,
    float radius,
    float width,
    int segments,
    RenderColor ringColor
  ) {
    for (int segment = 0; segment < segments; ++segment) {
      const float a0 =
        (static_cast<float>(segment) / static_cast<float>(segments)) *
        kTwoPi;
      const float a1 =
        (static_cast<float>(segment + 1) / static_cast<float>(segments)) *
        kTwoPi;
      lineColor(
        x + std::cos(a0) * radius,
        y + std::sin(a0) * radius,
        x + std::cos(a1) * radius,
        y + std::sin(a1) * radius,
        width,
        ringColor
      );
    }
  };
  const auto ring = [&](float radius, float width, int segments) {
    ringAt(0.0F, 0.0F, radius, width, segments, color);
  };

  if (weapon == Weapon::MachineGun) {
    rect(-17.0F, -15.0F, 8.0F, 30.0F);
    rect(-9.0F, -12.0F, 8.0F, 24.0F);
    rect(-2.0F, -11.0F, 19.0F, 5.0F);
    rect(-2.0F, -2.5F, 22.0F, 5.0F);
    rect(-2.0F, 6.0F, 19.0F, 5.0F);
    rect(17.0F, -10.0F, 4.0F, 3.0F);
    rect(20.0F, -1.5F, 4.0F, 3.0F);
    rect(17.0F, 7.0F, 4.0F, 3.0F);
    return;
  }

  if (weapon == Weapon::Shotgun) {
    for (float x : {-13.0F, -1.0F, 11.0F}) {
      line(x, -18.0F, x, 12.0F, 4.0F);
      line(x + 6.0F, -18.0F, x + 6.0F, 12.0F, 4.0F);
      line(x, -18.0F, x + 6.0F, -18.0F, 4.0F);
      line(x, 12.0F, x + 6.0F, 12.0F, 4.0F);
      rect(x - 1.0F, 14.0F, 8.0F, 4.0F);
    }
    return;
  }

  if (weapon == Weapon::GrenadeLauncher) {
    rect(-8.0F, -14.0F, 16.0F, 4.0F);
    rect(-13.0F, -10.0F, 26.0F, 8.0F);
    rect(-15.0F, -2.0F, 30.0F, 12.0F);
    rect(-11.0F, 10.0F, 22.0F, 6.0F);
    rect(-5.0F, 16.0F, 10.0F, 3.0F);
    rect(1.0F, -20.0F, 8.0F, 5.0F);
    line(8.0F, -19.0F, 16.0F, -17.0F, 4.0F);
    line(16.0F, -17.0F, 20.0F, -10.0F, 4.0F);
    return;
  }

  if (weapon == Weapon::RocketLauncher) {
    quad({{
      {-18.0F, -13.0F},
      {-10.0F, -21.0F},
      {17.0F, 6.0F},
      {9.0F, 14.0F},
    }});
    quad({{
      {17.0F, 6.0F},
      {9.0F, 14.0F},
      {24.0F, 20.0F},
      {26.0F, 18.0F},
    }});
    quad({{
      {-23.0F, -18.0F},
      {-18.0F, -24.0F},
      {-10.0F, -21.0F},
      {-18.0F, -13.0F},
    }});
    quad({{
      {-16.0F, -12.0F},
      {-29.0F, -8.0F},
      {-23.0F, -1.0F},
      {-9.0F, -5.0F},
    }});
    quad({{
      {-11.0F, -18.0F},
      {-13.0F, -31.0F},
      {-5.0F, -27.0F},
      {-3.0F, -14.0F},
    }});
    line(-19.0F, -20.0F, -28.0F, -27.0F, 4.0F);
    line(-22.0F, -15.0F, -33.0F, -16.0F, 4.0F);
    return;
  }

  if (weapon == Weapon::LightningGun) {
    for (int branch = 0; branch < 6; ++branch) {
      const float angle =
        (static_cast<float>(branch) / 6.0F) * kTwoPi - kHalfPi;
      const float dx = std::cos(angle);
      const float dy = std::sin(angle);
      const float px = -dy;
      const float py = dx;
      line(0.0F, 0.0F, dx * 21.0F, dy * 21.0F, 5.0F);
      line(
        dx * 12.0F,
        dy * 12.0F,
        dx * 18.0F + px * 7.0F,
        dy * 18.0F + py * 7.0F,
        4.0F
      );
      line(
        dx * 12.0F,
        dy * 12.0F,
        dx * 18.0F - px * 7.0F,
        dy * 18.0F - py * 7.0F,
        4.0F
      );
    }
    return;
  }

  if (weapon == Weapon::Railgun) {
    const RenderColor holeColor = {6, 8, 10, 230};
    rect(-8.0F, -17.0F, 16.0F, 34.0F);
    rect(-14.0F, -12.0F, 28.0F, 24.0F);
    rect(-17.0F, -7.0F, 34.0F, 14.0F);
    ring(16.0F, 3.0F, 24);
    ringAt(0.0F, 0.0F, 1.4F, 5.8F, 10, holeColor);
    for (int hole = 0; hole < 6; ++hole) {
      const float angle = static_cast<float>(hole) / 6.0F * kTwoPi;
      ringAt(
        std::cos(angle) * 8.5F,
        std::sin(angle) * 8.5F,
        1.4F,
        5.8F,
        10,
        holeColor
      );
    }
    return;
  }

  if (weapon == Weapon::PlasmaGun) {
    rect(-4.0F, -20.0F, 8.0F, 40.0F);
    rect(-20.0F, -4.0F, 40.0F, 8.0F);
    rect(-12.0F, -12.0F, 24.0F, 24.0F);
    line(-16.0F, -16.0F, 16.0F, 16.0F, 3.0F);
    line(-16.0F, 16.0F, 16.0F, -16.0F, 3.0F);
  }
}

void addSelectedWeaponIndicator(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  (void)width;
  constexpr std::array<Weapon, 7> weapons = {{
    Weapon::MachineGun,
    Weapon::Shotgun,
    Weapon::GrenadeLauncher,
    Weapon::RocketLauncher,
    Weapon::LightningGun,
    Weapon::Railgun,
    Weapon::PlasmaGun,
  }};
  const float viewportScale = std::clamp(
    static_cast<float>(height) / 720.0F,
    0.75F,
    1.25F
  );
  const float scale =
    viewportScale * std::clamp(settings.weaponBarScale, 0.5F, 4.0F);
  const float rowHeight = 24.0F * scale;
  const float gap = 4.0F * scale;
  const float iconScale = 0.38F * scale;
  const float textScale = 1.45F * scale;
  const float panelHeight =
    rowHeight * static_cast<float>(weapons.size()) +
    gap * static_cast<float>(weapons.size() - 1U);
  const float x = 4.0F * viewportScale;
  const float y = static_cast<float>(height) * 0.18F;

  float rowY = std::min(
    y,
    static_cast<float>(height) - panelHeight - 96.0F * viewportScale
  );
  rowY = std::max(48.0F * viewportScale, rowY);
  for (std::size_t index = 0; index < weapons.size(); ++index) {
    const Weapon weapon = weapons[index];
    const bool selected = weapon == hud.selectedWeapon;
    const RenderColor weaponColor = quakeLiveWeaponColor(weapon);
    const RenderColor textColor = selected
      ? RenderColor{245, 250, 255, 255}
      : RenderColor{220, 226, 232, 235};
    if (selected) {
      addRect(
        drawList,
        x - 3.0F * scale,
        rowY - 1.0F * scale,
        45.0F * scale,
        rowHeight + 2.0F * scale,
        {10, 13, 18, 155}
      );
    }

    addWeaponIcon(
      drawList,
      x + 9.0F * scale,
      rowY + rowHeight * 0.5F,
      weapon,
      weaponColor,
      iconScale
    );
    addText(
      drawList,
      x + 20.0F * scale,
      rowY + 4.0F * scale,
      hud.weaponValues[index],
      textColor,
      textScale
    );

    rowY += rowHeight + gap;
  }
}

[[nodiscard]] RenderColor withAlpha(RenderColor color, float alphaScale) {
  color.alpha = static_cast<std::uint8_t>(
    std::clamp(
      static_cast<float>(color.alpha) * std::clamp(alphaScale, 0.0F, 1.0F),
      0.0F,
      255.0F
    )
  );
  return color;
}

void addKillFeed(
  DrawList2D& drawList,
  int width,
  float startY,
  const HudRenderState& hud
) {
  if (hud.killFeedLines.empty()) {
    return;
  }

  constexpr float textScale = 2.25F;
  constexpr float rowHeight = 31.5F;
  constexpr float rightPadding = 10.0F;
  constexpr float textIconGap = 12.0F;
  constexpr float iconWidth = 30.0F;
  constexpr float iconScale = 0.51F;
  constexpr RenderColor baseText = {235, 242, 250, 245};

  float y = startY;
  for (const HudRenderState::KillFeedLine& line : hud.killFeedLines) {
    const float alpha = std::clamp(line.alpha, 0.0F, 1.0F);
    if (alpha <= 0.0F) {
      y += rowHeight;
      continue;
    }

    const float killerWidth = textWidth(line.killerName, textScale);
    const float killedWidth = textWidth(line.killedName, textScale);
    const bool selfKill = line.killedName.empty();
    const float rowWidth =
      killerWidth + iconWidth + textIconGap +
      (selfKill ? 0.0F : killedWidth + textIconGap);
    float x = static_cast<float>(width) - rightPadding - rowWidth;
    x = std::max(12.0F, x);
    const float textY = y + 3.0F;
    const RenderColor textColor = withAlpha(baseText, alpha);
    const RenderColor weaponColor =
      withAlpha(quakeLiveWeaponColor(line.weapon), alpha);

    addText(drawList, x, textY, line.killerName, textColor, textScale);
    x += killerWidth + textIconGap;
    addWeaponIcon(
      drawList,
      x + iconWidth * 0.5F,
      y + rowHeight * 0.5F,
      line.weapon,
      weaponColor,
      iconScale
    );
    if (!selfKill) {
      x += iconWidth + textIconGap;
      addText(drawList, x, textY, line.killedName, textColor, textScale);
    }

    y += rowHeight;
  }
}

[[nodiscard]] ScreenPoint screenPointFromProjection(
  ProjectedPoint projected,
  int width,
  int height
) {
  return {
    (projected.x + 1.0F) * 0.5F * static_cast<float>(width),
    (1.0F - projected.y) * 0.5F * static_cast<float>(height),
  };
}

[[nodiscard]] bool hasClearLineToPoint(
  const PerspectiveCamera& camera,
  const Arena& arena,
  Vec3 point
) {
  const Vec3 direction = point - camera.position;
  if (length(direction) <= 0.00001F) {
    return true;
  }
  return traceWorld(arena, camera.position, direction, 1.0F).distance >= 0.999F;
}

[[nodiscard]] bool enemyBodyVisibleFromCamera(
  const PerspectiveCamera& camera,
  const Arena& arena,
  const PlayerState& player
) {
  const float radius = player.bounds.radius;
  const float bottom = player.position.z - player.bounds.halfHeight;
  const float middle = player.position.z;
  const float top = player.position.z + player.bounds.halfHeight;
  constexpr std::array<std::array<float, 2>, 9> planarOffsets = {{
    {{0.0F, 0.0F}},
    {{-1.0F, 0.0F}},
    {{1.0F, 0.0F}},
    {{0.0F, -1.0F}},
    {{0.0F, 1.0F}},
    {{-1.0F, -1.0F}},
    {{-1.0F, 1.0F}},
    {{1.0F, -1.0F}},
    {{1.0F, 1.0F}},
  }};
  const std::array<float, 3> zLevels = {{bottom, middle, top}};
  const auto sampleVisible = [&](Vec3 sample) {
    ProjectedPoint projected;
    return
      projectPerspectivePoint(camera, sample, projected) &&
      projected.x >= -1.0F &&
      projected.x <= 1.0F &&
      projected.y >= -1.0F &&
      projected.y <= 1.0F &&
      hasClearLineToPoint(camera, arena, sample);
  };

  for (float z : zLevels) {
    for (const auto& offset : planarOffsets) {
      if (
        sampleVisible({
          player.position.x + offset[0] * radius,
          player.position.y + offset[1] * radius,
          z,
        })
      ) {
        return true;
      }
    }
  }
  return false;
}

void addFloatingHealthBar(
  DrawList2D& drawList,
  int width,
  int height,
  const PerspectiveCamera& camera,
  const PlayerState& player,
  float alpha,
  bool teammate,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  const bool enabled = teammate
    ? settings.teammateHealthBarEnabled
    : settings.enemyHealthBarEnabled;
  if (
    (!teammate && !hud.showOpponentHealthBar) ||
    !enabled ||
    player.health <= 0 ||
    alpha <= 0.0F
  ) {
    return;
  }

  const float maxDistance = teammate
    ? settings.teammateHealthBarMaxDistance
    : settings.enemyHealthBarMaxDistance;
  if (
    maxDistance > 0.0F &&
    length(player.position - camera.position) > maxDistance
  ) {
    return;
  }

  const float worldOffsetZ = teammate
    ? settings.teammateHealthBarWorldOffsetZ
    : settings.enemyHealthBarWorldOffsetZ;
  const Vec3 anchor =
    player.position +
    Vec3{0.0F, 0.0F, player.bounds.halfHeight + worldOffsetZ};
  ProjectedPoint projected;
  if (!projectPerspectivePoint(camera, anchor, projected)) {
    return;
  }
  const ScreenPoint anchorScreen =
    screenPointFromProjection(projected, width, height);
  const float barWidth = std::max(
    1.0F,
    teammate ? settings.teammateHealthBarWidth : settings.enemyHealthBarWidth
  );
  const float barHeight = std::max(
    1.0F,
    teammate ? settings.teammateHealthBarHeight : settings.enemyHealthBarHeight
  );
  const float border = std::max(1.0F, std::round(barHeight * 0.25F));
  const float offsetX = teammate
    ? settings.teammateHealthBarScreenOffsetX
    : settings.enemyHealthBarScreenOffsetX;
  const float offsetY = teammate
    ? settings.teammateHealthBarScreenOffsetY
    : settings.enemyHealthBarScreenOffsetY;
  const float x = anchorScreen.x + offsetX - barWidth * 0.5F;
  const float y = anchorScreen.y + offsetY - barHeight;
  const float maxHealth = std::max(1.0F, static_cast<float>(hud.healthAmount));
  const float healthRatio =
    std::clamp(static_cast<float>(player.health) / maxHealth, 0.0F, 1.0F);
  const float barAlpha = teammate
    ? settings.teammateHealthBarAlpha
    : settings.enemyHealthBarAlpha;
  const RenderColor outline =
    withAlpha({220, 226, 236, 255}, alpha * barAlpha);
  const RenderColor back =
    withAlpha({10, 13, 18, 215}, alpha * barAlpha);
  const RenderColor fill = withAlpha(
    {
      teammate ? settings.teammateHealthBarRed : settings.enemyHealthBarRed,
      teammate
        ? settings.teammateHealthBarGreen
        : settings.enemyHealthBarGreen,
      teammate ? settings.teammateHealthBarBlue : settings.enemyHealthBarBlue,
      255,
    },
    alpha * barAlpha
  );

  addRect(
    drawList,
    x - border,
    y - border,
    barWidth + border * 2.0F,
    barHeight + border * 2.0F,
    outline
  );
  addRect(drawList, x, y, barWidth, barHeight, back);
  addRect(drawList, x, y, barWidth * healthRatio, barHeight, fill);
}

void addFloatingNameTag(
  DrawList2D& drawList,
  int width,
  int height,
  const PerspectiveCamera& camera,
  const RemotePlayerView& remote,
  const RenderSettings& settings
) {
  if (remote.name.empty() || remote.player.health <= 0) {
    return;
  }

  const bool enabled = remote.teammate
    ? settings.teammateNameTagEnabled
    : settings.enemyNameTagEnabled;
  if (!enabled) {
    return;
  }

  const float maxDistance = remote.teammate
    ? settings.teammateNameTagMaxDistance
    : settings.enemyNameTagMaxDistance;
  if (
    maxDistance > 0.0F &&
    length(remote.player.position - camera.position) > maxDistance
  ) {
    return;
  }

  const float worldOffsetZ = remote.teammate
    ? settings.teammateNameTagWorldOffsetZ
    : settings.enemyNameTagWorldOffsetZ;
  const Vec3 anchor =
    remote.player.position +
    Vec3{0.0F, 0.0F, remote.player.bounds.halfHeight + worldOffsetZ};
  ProjectedPoint projected;
  if (!projectPerspectivePoint(camera, anchor, projected)) {
    return;
  }

  const ScreenPoint anchorScreen =
    screenPointFromProjection(projected, width, height);
  const float scale = std::max(
    0.1F,
    remote.teammate ? settings.teammateNameTagScale : settings.enemyNameTagScale
  );
  const float textWidth =
    static_cast<float>(remote.name.size()) * kGlyphSize * scale;
  const float offsetX = remote.teammate
    ? settings.teammateNameTagScreenOffsetX
    : settings.enemyNameTagScreenOffsetX;
  const float offsetY = remote.teammate
    ? settings.teammateNameTagScreenOffsetY
    : settings.enemyNameTagScreenOffsetY;
  const float alpha = std::clamp(
    remote.teammate ? settings.teammateNameTagAlpha : settings.enemyNameTagAlpha,
    0.0F,
    1.0F
  );
  const RenderColor color = {
    remote.teammate ? settings.teammateNameTagRed : settings.enemyNameTagRed,
    remote.teammate ? settings.teammateNameTagGreen : settings.enemyNameTagGreen,
    remote.teammate ? settings.teammateNameTagBlue : settings.enemyNameTagBlue,
    static_cast<std::uint8_t>(alpha * 255.0F),
  };

  addText(
    drawList,
    anchorScreen.x + offsetX - textWidth * 0.5F,
    anchorScreen.y + offsetY,
    remote.name,
    color,
    scale
  );
}

void addCrosshair(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings
) {
  if (!settings.crosshairEnabled) {
    return;
  }

  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float size = settings.crosshairSize;
  const float gap = settings.crosshairGap;
  const float thickness = settings.crosshairThickness;
  const float dotThickness = settings.crosshairDotThickness;
  const float outlineWidth = settings.crosshairOutlineEnabled
    ? std::max(0.0F, settings.crosshairOutlineWidth)
    : 0.0F;
  const float hitAmount = std::clamp(settings.crosshairHitAmount, 0.0F, 1.0F);
  const RenderColor color = {
    blendChannel(settings.crosshairRed, settings.crosshairHitRed, hitAmount),
    blendChannel(settings.crosshairGreen, settings.crosshairHitGreen, hitAmount),
    blendChannel(settings.crosshairBlue, settings.crosshairHitBlue, hitAmount),
    static_cast<std::uint8_t>(
      std::clamp(settings.crosshairAlpha, 0.0F, 1.0F) * 255.0F
    ),
  };
  const RenderColor outlineColor = {0, 0, 0, color.alpha};
  const auto rect = [&](float x, float y, float w, float h, RenderColor rectColor) {
    addRect(drawList, x, y, w, h, rectColor);
  };
  const auto cross = [&](float crossGap, float extra, RenderColor crossColor) {
    const float crossThickness = thickness + extra * 2.0F;
    const float halfThickness = crossThickness * 0.5F;
    const float crossSize = size + extra;
    if (crossGap <= 0.0F) {
      rect(
        centerX - crossSize,
        centerY - halfThickness,
        crossSize * 2.0F,
        crossThickness,
        crossColor
      );
      rect(
        centerX - halfThickness,
        centerY - crossSize,
        crossThickness,
        crossSize * 2.0F,
        crossColor
      );
      return;
    }
    const float armGap = std::max(0.0F, crossGap - extra);
    rect(
      centerX - armGap - crossSize,
      centerY - halfThickness,
      crossSize,
      crossThickness,
      crossColor
    );
    rect(
      centerX + armGap,
      centerY - halfThickness,
      crossSize,
      crossThickness,
      crossColor
    );
    rect(
      centerX - halfThickness,
      centerY - armGap - crossSize,
      crossThickness,
      crossSize,
      crossColor
    );
    rect(
      centerX - halfThickness,
      centerY + armGap,
      crossThickness,
      crossSize,
      crossColor
    );
  };
  const auto ring = [&](float extra, RenderColor ringColor) {
    const float ringRadius = std::max(1.0F, size + extra);
    const float ringThickness = thickness + extra * 2.0F;
    constexpr int segmentCount = 36;
    for (int segment = 0; segment < segmentCount; ++segment) {
      const float a0 =
        (static_cast<float>(segment) / static_cast<float>(segmentCount)) *
        kTwoPi;
      const float a1 =
        (static_cast<float>(segment + 1) / static_cast<float>(segmentCount)) *
        kTwoPi;
      addLine(
        drawList,
        {
          centerX + std::cos(a0) * ringRadius,
          centerY + std::sin(a0) * ringRadius,
        },
        {
          centerX + std::cos(a1) * ringRadius,
          centerY + std::sin(a1) * ringRadius,
        },
        ringColor,
        ringThickness
      );
    }
  };
  const auto dot = [&](float extra, RenderColor dotColor) {
    const float dotSize = dotThickness + extra * 2.0F;
    rect(
      centerX - dotSize * 0.5F,
      centerY - dotSize * 0.5F,
      dotSize,
      dotSize,
      dotColor
    );
  };

  const auto drawMainShape = [&](float extra, RenderColor shapeColor) {
    switch (settings.crosshairStyle) {
    case 1:
      cross(0.0F, extra, shapeColor);
      break;
    case 3:
      ring(extra, shapeColor);
      break;
    case 2:
      break;
    case 0:
    default:
      cross(gap, extra, shapeColor);
      break;
    }
  };

  if (outlineWidth > 0.0F) {
    drawMainShape(outlineWidth, outlineColor);
    if (settings.crosshairDotEnabled) {
      dot(outlineWidth, outlineColor);
    }
  }

  drawMainShape(0.0F, color);
  if (settings.crosshairDotEnabled) {
    dot(0.0F, color);
  }
}

void addHitMarker(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings
) {
  if (!settings.hitMarkerEnabled || settings.hitMarkerAmount <= 0.0F) {
    return;
  }

  const float centerX = static_cast<float>(width) * 0.5F;
  const float centerY = static_cast<float>(height) * 0.5F;
  const float size = settings.hitMarkerSize;
  const float inner = size * 0.35F;
  const RenderColor color = {
    settings.hitMarkerRed,
    settings.hitMarkerGreen,
    settings.hitMarkerBlue,
    static_cast<std::uint8_t>(
      std::clamp(settings.hitMarkerAmount, 0.0F, 1.0F) * 255.0F
    ),
  };
  addLine(
    drawList,
    {centerX - size, centerY - size},
    {centerX - inner, centerY - inner},
    color,
    settings.hitMarkerThickness
  );
  addLine(
    drawList,
    {centerX + inner, centerY + inner},
    {centerX + size, centerY + size},
    color,
    settings.hitMarkerThickness
  );
  addLine(
    drawList,
    {centerX + inner, centerY - inner},
    {centerX + size, centerY - size},
    color,
    settings.hitMarkerThickness
  );
  addLine(
    drawList,
    {centerX - size, centerY + size},
    {centerX - inner, centerY + inner},
    color,
    settings.hitMarkerThickness
  );
}

void addDamageNumbers(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  const float baseX = static_cast<float>(width) * 0.5F;
  const float baseY = static_cast<float>(height) * 0.5F;
  const float alphaScale = std::clamp(settings.damageNumbersAlpha, 0.0F, 1.0F);
  const float duration = std::max(0.001F, settings.damageNumbersDuration);
  const float scale = std::max(0.1F, settings.damageNumbersSize);
  const RenderColor baseColor = {
    settings.damageNumbersRed,
    settings.damageNumbersGreen,
    settings.damageNumbersBlue,
    255,
  };

  for (const DamageNumberEntry& entry : hud.damageNumbers.entries) {
    const float life = std::clamp(entry.ageSeconds / duration, 0.0F, 1.0F);
    const float fade = 1.0F - life;
    const float drift = life * 28.0F * scale;
    const float stackSlot = static_cast<float>(entry.sequence % 8U);
    const float wobble = (entry.sequence % 2U == 0U ? -1.0F : 1.0F) *
      2.0F * scale;
    const float textScale = scale;
    const std::string text = std::to_string(entry.damage);
    const float textWidth =
      static_cast<float>(text.size()) * kGlyphSize * textScale;
    const float x =
      baseX + settings.damageNumbersOffsetX + wobble - textWidth * 0.5F;
    const float y =
      baseY + settings.damageNumbersOffsetY - drift +
      stackSlot * 10.0F * scale;
    addText(
      drawList,
      x,
      y,
      text,
      withAlpha(baseColor, alphaScale * fade),
      textScale
    );
  }

  for (const DamageNumberTally& tally : hud.damageNumbers.tallies) {
    if (!tally.active || tally.hasWorldPosition) {
      continue;
    }
    const float life =
      std::clamp(tally.secondsSinceLastHit / duration, 0.0F, 1.0F);
    const float fade = 1.0F - life * 0.55F;
    const float textScale = scale * 1.35F;
    const std::string text = std::to_string(tally.damage);
    const float textWidth =
      static_cast<float>(text.size()) * kGlyphSize * textScale;
    addText(
      drawList,
      baseX + settings.damageNumbersOffsetX - textWidth * 0.5F,
      baseY + settings.damageNumbersOffsetY - 34.0F * scale,
      text,
      withAlpha(baseColor, alphaScale * fade),
      textScale
    );
  }
}

void addFloatingDamageNumbers(
  DrawList2D& drawList,
  int width,
  int height,
  const PerspectiveCamera& camera,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  const float alphaScale = std::clamp(settings.damageNumbersAlpha, 0.0F, 1.0F);
  const float duration = std::max(0.001F, settings.damageNumbersDuration);
  const float scale = std::max(0.1F, settings.damageNumbersSize);
  const RenderColor baseColor = {
    settings.damageNumbersRed,
    settings.damageNumbersGreen,
    settings.damageNumbersBlue,
    255,
  };
  for (const DamageNumberTally& tally : hud.damageNumbers.tallies) {
    if (!tally.active || !tally.hasWorldPosition) {
      continue;
    }

    ProjectedPoint projected;
    if (!projectPerspectivePoint(camera, tally.worldPosition, projected)) {
      continue;
    }

    const ScreenPoint anchorScreen =
      screenPointFromProjection(projected, width, height);
    const float life =
      std::clamp(tally.secondsSinceLastHit / duration, 0.0F, 1.0F);
    const float fade = 1.0F - life * 0.55F;
    const float textScale = scale * 1.35F;
    const std::string text = std::to_string(tally.damage);
    const float textWidth =
      static_cast<float>(text.size()) * kGlyphSize * textScale;
    addText(
      drawList,
      anchorScreen.x + settings.damageNumbersOffsetX - textWidth * 0.5F,
      anchorScreen.y + settings.damageNumbersOffsetY - 34.0F * scale,
      text,
      withAlpha(baseColor, alphaScale * fade),
      textScale
    );
  }
}

void addHud(
  DrawList2D& drawList,
  int width,
  int height,
  const PlayerState& localPlayer,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  constexpr float textScale = 2.0F;
  constexpr float characterWidth = kGlyphSize * textScale;
  constexpr RenderColor defaultText = {235, 242, 250, 255};
  constexpr float fpsRightOffset = 8.0F;
  constexpr float fpsTopOffset = 4.0F;

  if (!hud.fpsText.empty()) {
    const float fpsScale = std::clamp(settings.fpsTextScale, 0.5F, 6.0F);
    addText(
      drawList,
      static_cast<float>(width) - fpsRightOffset,
      fpsTopOffset,
      hud.fpsText,
      {245, 248, 252, 245},
      fpsScale,
      TextHorizontalAlignment::Right
    );
  }

  if (hud.scoreboardOpen) {
    const float panelWidth =
      std::min(720.0F, static_cast<float>(width) - 80.0F);
    const float panelHeight =
      72.0F + static_cast<float>(hud.scoreboardLines.size()) * 28.0F;
    const float panelX =
      (static_cast<float>(width) - panelWidth) * 0.5F;
    const float panelY =
      (static_cast<float>(height) - panelHeight) * 0.35F;
    addRect(
      drawList,
      panelX,
      panelY,
      panelWidth,
      panelHeight,
      {7, 11, 17, 225}
    );
    addOutline(
      drawList,
      panelX,
      panelY,
      panelWidth,
      panelHeight,
      {78, 168, 235, 255}
    );

    const float scoreboardX =
      panelX + std::max(16.0F, (panelWidth - 660.0F) * 0.5F);
    const float nameX = scoreboardX;
    const float scoreX = scoreboardX + 280.0F;
    const float accuracyX = scoreboardX + 360.0F;
    const float percentX = accuracyX + textWidth("LG ", textScale);
    const float damageX = scoreboardX + 470.0F;

    constexpr std::size_t kScoreboardNameColumnChars = 16U;
    constexpr std::size_t kScoreboardScoreColumnChars =
      kScoreboardNameColumnChars + 4U;
    constexpr std::size_t kScoreboardAccuracyColumnChars =
      kScoreboardScoreColumnChars + 6U;
    constexpr std::size_t kScoreboardDamageColumnChars =
      kScoreboardAccuracyColumnChars + 8U;

    float scoreboardY = panelY + 20.0F;
    for (std::size_t index = 0; index < hud.scoreboardLines.size(); ++index) {
      const std::string& line = hud.scoreboardLines[index];
      const Team team = index < hud.scoreboardLineTeams.size()
        ? hud.scoreboardLineTeams[index]
        : Team::None;
      const std::size_t weaponColumn =
        index < hud.scoreboardLineAccuracyWeaponColumns.size()
          ? hud.scoreboardLineAccuracyWeaponColumns[index]
          : std::string::npos;
      const bool hasWeaponColumn =
        weaponColumn != std::string::npos &&
        weaponColumn + 2U <= line.size() &&
        index < hud.scoreboardLineAccuracyWeapons.size();
      if (index == 0) {
        addText(
          drawList,
          scoreboardX,
          scoreboardY,
          line,
          {255, 220, 120, 255},
          textScale
        );
      } else {
        const RenderColor baseColor = {225, 235, 245, 255};
        const RenderColor teamColor = team == Team::Red
          ? RenderColor{224, 82, 92, 255}
          : RenderColor{82, 190, 224, 255};
        const RenderColor nameColor =
          team == Team::None ? baseColor : teamColor;
        const auto cell = [&line](std::size_t start, std::size_t end) {
          if (start >= line.size()) {
            return std::string();
          }
          return trimCell(line.substr(start, std::min(end, line.size()) - start));
        };
        const std::string name = cell(0U, kScoreboardNameColumnChars);
        const std::string score = cell(
          kScoreboardScoreColumnChars,
          kScoreboardAccuracyColumnChars - 1U
        );
        const std::string accuracy = cell(
          kScoreboardAccuracyColumnChars,
          kScoreboardDamageColumnChars - 1U
        );
        const std::string damage =
          cell(kScoreboardDamageColumnChars, line.size());

        if (!name.empty()) {
          addText(
            drawList,
            nameX,
            scoreboardY,
            name,
            nameColor,
            textScale
          );
        }
        if (!score.empty()) {
          addText(drawList, scoreX, scoreboardY, score, baseColor, textScale);
        }
        if (hasWeaponColumn && accuracy.size() >= 2U) {
          addText(
            drawList,
            accuracyX,
            scoreboardY,
            accuracy.substr(0U, 2U),
            quakeLiveWeaponColor(hud.scoreboardLineAccuracyWeapons[index]),
            textScale
          );
          const std::string percent = trimCell(accuracy.substr(2U));
          if (!percent.empty()) {
            addText(
              drawList,
              percentX,
              scoreboardY,
              percent,
              baseColor,
              textScale
            );
          }
        } else if (!accuracy.empty()) {
          addText(
            drawList,
            accuracyX,
            scoreboardY,
            accuracy,
            baseColor,
            textScale
          );
        }
        if (!damage.empty()) {
          addText(drawList, damageX, scoreboardY, damage, baseColor, textScale);
        }
      }
      scoreboardY += 28.0F;
    }
  }

  float y = 12.0F;
  for (const std::string& line : hud.topLeftLines) {
    addText(drawList, 12.0F, y, line, defaultText, textScale);
    y += 20.0F;
  }

  y = 12.0F;
  for (const std::string& line : hud.topCenterLines) {
    addText(
      drawList,
      static_cast<float>(width) * 0.5F,
      y,
      line,
      defaultText,
      textScale,
      TextHorizontalAlignment::Center
    );
    y += 20.0F;
  }

  y = hud.fpsText.empty()
    ? 12.0F
    : std::max(24.0F, fpsTopOffset + kGlyphSize *
        std::clamp(settings.fpsTextScale, 0.5F, 6.0F) + 6.0F);
  addKillFeed(drawList, width, y, hud);
  y += static_cast<float>(hud.killFeedLines.size()) * 31.5F;
  for (const std::string& line : hud.topRightLines) {
    const float x = std::max(
      12.0F,
      static_cast<float>(width) - 12.0F -
        static_cast<float>(line.size()) * characterWidth
    );
    addText(drawList, x, y, line, defaultText, textScale);
    y += 20.0F;
  }

  y = (static_cast<float>(height) * 0.5F) -
    static_cast<float>(hud.centerLines.size()) * 11.0F +
    hud.centerOffsetY;
  for (const std::string& line : hud.centerLines) {
    addText(
      drawList,
      static_cast<float>(width) * 0.5F,
      y,
      line,
      defaultText,
      textScale,
      TextHorizontalAlignment::Center
    );
    y += 22.0F;
  }

  if (!hud.countdownText.empty()) {
    const float pulse = std::clamp(hud.countdownPulse, 0.0F, 1.0F);
    const float scale = 10.0F + pulse * 3.0F;
    const float textWidth =
      static_cast<float>(hud.countdownText.size()) * kGlyphSize * scale;
    const float textHeight = kGlyphSize * scale;
    const float cellX = (static_cast<float>(width) - textWidth) * 0.5F;
    const float cellY =
      (static_cast<float>(height) - textHeight) * 0.5F;
    const float textX = cellX + countdownGlyphOffsetX(hud.countdownText, scale);
    const float textY = cellY + 0.5F * scale;
    const float padding = 24.0F + pulse * 12.0F;
    addRect(
      drawList,
      cellX - padding,
      cellY - padding,
      textWidth + padding * 2.0F,
      textHeight + padding * 2.0F,
      {
        8,
        12,
        18,
        static_cast<std::uint8_t>(150.0F + pulse * 55.0F),
      }
    );
    addText(
      drawList,
      textX,
      textY,
      hud.countdownText,
      {
        255,
        static_cast<std::uint8_t>(175.0F + pulse * 70.0F),
        static_cast<std::uint8_t>(75.0F + pulse * 80.0F),
        255,
      },
      scale
    );
  }

  const float bottomY = static_cast<float>(height) - 24.0F;
  if (settings.healthStyle == 2) {
    addCrosshairHealthAndAmmo(
      drawList,
      width,
      height,
      localPlayer,
      hud,
      settings
    );
  } else if (settings.healthStyle == 1) {
    addLocalHealthNumber(drawList, width, height, localPlayer, hud, settings);
  } else {
    addLocalHealthBar(
      drawList,
      width,
      height,
      localPlayer,
      hud,
      settings,
      bottomY
    );
  }

  std::vector<std::string> bottomLines;
  for (const std::string& line : hud.bottomCenterLines) {
    if (line.rfind("HEALTH ", 0) == 0) {
      continue;
    }
    bottomLines.push_back(line);
  }
  const float healthCharacterWidth = kGlyphSize * settings.healthTextScale;
  const float healthLineHeight = 11.0F * settings.healthTextScale;
  const float healthScale = std::clamp(settings.healthTextScale, 0.5F, 20.0F);
  y = bottomY -
    kGlyphSize * std::max(0.75F, healthScale * 0.72F) -
    std::max(4.0F, 3.0F * healthScale) -
    13.0F * healthScale -
    std::max(1.0F, std::round(1.0F * healthScale)) * 2.0F -
    static_cast<float>(bottomLines.size()) * healthLineHeight -
    8.0F * healthScale;
  for (const std::string& line : bottomLines) {
    const float x = std::max(
      12.0F,
      (static_cast<float>(width) -
       static_cast<float>(line.size()) * healthCharacterWidth) * 0.5F
    );
    addText(
      drawList,
      x,
      y,
      line,
      defaultText,
      settings.healthTextScale
    );
    y += healthLineHeight;
  }

  const ChatTextLayout chatLayout = buildChatTextLayout(width, height, hud);
  for (const ChatLayoutRow& row : chatLayout.rows) {
    addText(drawList, row.x, row.y, row.text, {225, 235, 245, 255}, 2.0F);
  }
  if (hud.chatInputOpen) {
    if (!chatLayout.inputRows.empty()) {
      addRect(
        drawList,
        chatLayout.input.x - 6.0F,
        chatLayout.input.y - 3.0F,
        static_cast<float>(width) - chatLayout.input.x * 2.0F + 12.0F,
        static_cast<float>(chatLayout.inputRows.size()) * chatLayout.input.lineHeight + 6.0F,
        {6, 9, 13, 170}
      );
    }
    if (hud.chatHasSelection && hud.chatSelectionAnchor != hud.chatSelectionFocus) {
      const std::size_t begin =
        std::min(hud.chatSelectionAnchor, hud.chatSelectionFocus);
      const std::size_t end =
        std::min(
          std::max(hud.chatSelectionAnchor, hud.chatSelectionFocus),
          hud.chatInput.size()
        );
      for (const ChatInputRow& row : chatLayout.inputRows) {
        const std::size_t rowBegin = row.inputBegin;
        const std::size_t rowEnd = row.inputEnd;
        const std::size_t highlightBegin = std::max(begin, rowBegin);
        const std::size_t highlightEnd = std::min(end, rowEnd);
        if (highlightBegin >= highlightEnd) {
          continue;
        }
        const float selectionX =
          row.x +
          static_cast<float>(
            row.contentColumn +
            utf8GlyphCount(
              hud.chatInput.substr(rowBegin, highlightBegin - rowBegin)
            )
          ) * chatLayout.input.characterWidth;
        const float selectionWidth =
          static_cast<float>(
            utf8GlyphCount(
              hud.chatInput.substr(highlightBegin, highlightEnd - highlightBegin)
            )
          ) * chatLayout.input.characterWidth;
        addRect(
          drawList,
          selectionX,
          row.y,
          selectionWidth,
          chatLayout.input.lineHeight,
          {58, 118, 188, 170}
        );
      }
    }
    const ScreenPoint cursor =
      chatInputCursorPosition(chatLayout, hud.chatInput, hud.chatCursorIndex);
    for (const ChatInputRow& row : chatLayout.inputRows) {
      std::string text = row.text;
      if (std::abs(row.y - cursor.y) < 0.01F) {
        const auto cursorColumn = static_cast<std::size_t>(
          std::round((cursor.x - row.x) / chatLayout.input.characterWidth)
        );
        text.insert(utf8ByteOffsetForGlyph(text, cursorColumn), 1U, '_');
      }
      addText(
        drawList,
        row.x,
        row.y,
        std::move(text),
        {255, 232, 150, 255},
        2.0F
      );
    }
  }
}

void addSettingsMenu(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud
) {
  if (!hud.settingsOpen) {
    return;
  }

  addRect(
    drawList,
    0.0F,
    0.0F,
    static_cast<float>(width),
    static_cast<float>(height),
    {0, 0, 0, 120}
  );

  const float panelWidth = std::min(760.0F, static_cast<float>(width) - 48.0F);
  const float rowHeight = 30.0F;
  const float panelHeight = std::min(
    static_cast<float>(height) - 48.0F,
    112.0F + rowHeight * static_cast<float>(hud.settingsItems.size())
  );
  const float panelX = (static_cast<float>(width) - panelWidth) * 0.5F;
  const float panelY = (static_cast<float>(height) - panelHeight) * 0.45F;
  addRect(drawList, panelX, panelY, panelWidth, panelHeight, {6, 10, 15, 238});
  addOutline(drawList, panelX, panelY, panelWidth, panelHeight, {88, 176, 232, 255});
  addRect(drawList, panelX, panelY, panelWidth, 3.0F, {255, 212, 92, 255});

  addText(
    drawList,
    panelX + 22.0F,
    panelY + 20.0F,
    "SETTINGS / VIDEO",
    {255, 226, 132, 255},
    2.25F
  );

  float y = panelY + 64.0F;
  constexpr float textScale = 2.0F;
  constexpr float characterWidth = kGlyphSize * textScale;
  const float labelX = panelX + 28.0F;
  const float valueRight = panelX + panelWidth - 28.0F;
  for (const HudRenderState::SettingsMenuItem& item : hud.settingsItems) {
    const RenderColor labelColor = item.active
      ? RenderColor{255, 244, 184, 255}
      : RenderColor{214, 226, 238, 255};
    const RenderColor valueColor = item.changed
      ? RenderColor{255, 210, 95, 255}
      : RenderColor{156, 214, 242, 255};
    if (item.active) {
      addRect(
        drawList,
        panelX + 14.0F,
        y - 5.0F,
        panelWidth - 28.0F,
        rowHeight,
        {32, 54, 70, 220}
      );
      addRect(drawList, panelX + 18.0F, y + 1.0F, 4.0F, 18.0F, {255, 212, 92, 255});
    }
    addText(drawList, labelX, y, item.label, labelColor, textScale);
    const float valueX = std::max(
      labelX + 220.0F,
      valueRight - static_cast<float>(item.value.size()) * characterWidth
    );
    addText(drawList, valueX, y, item.value, valueColor, textScale);
    if (!item.command) {
      addText(drawList, valueRight - 9.0F * characterWidth, y, "<  >", {110, 128, 144, 255}, textScale);
    }
    y += rowHeight;
  }

  if (!hud.settingsFooter.empty()) {
    addText(
      drawList,
      panelX + 22.0F,
      panelY + panelHeight - 30.0F,
      hud.settingsFooter,
      {174, 190, 204, 255},
      1.5F
    );
  }
}

void addConsole(
  DrawList2D& drawList,
  int width,
  int height,
  const ConsoleRenderState& console
) {
  if (!console.open) {
    return;
  }

  const float consoleHeight = static_cast<float>(height) * 0.55F;
  addRect(
    drawList,
    0.0F,
    0.0F,
    static_cast<float>(width),
    consoleHeight,
    {5, 8, 12, 235}
  );
  addRect(
    drawList,
    0.0F,
    consoleHeight - 2.0F,
    static_cast<float>(width),
    2.0F,
    {92, 170, 230, 255}
  );

  constexpr float textScale = 2.0F;
  const ConsoleTextLayout layout = buildConsoleTextLayout(width, height, console);
  if (console.hasSelection && console.selectionAnchor != console.selectionFocus) {
    const std::size_t selectionBegin =
      std::min(console.selectionAnchor, console.selectionFocus);
    const std::size_t selectionEnd =
      std::max(console.selectionAnchor, console.selectionFocus);
    for (const ConsoleLayoutLine& line : layout.lines) {
      const std::size_t lineBegin = line.textOffset;
      const std::size_t lineEnd = line.textOffset + line.text.size();
      const std::size_t begin = std::max(selectionBegin, lineBegin);
      const std::size_t end = std::min(selectionEnd, lineEnd);
      if (begin >= end) {
        continue;
      }
      const float x =
        line.x + static_cast<float>(begin - lineBegin) * layout.characterWidth;
      addRect(
        drawList,
        x,
        line.y,
        static_cast<float>(end - begin) * layout.characterWidth,
        layout.lineHeight,
        {58, 118, 188, 170}
      );
    }
  }

  const std::size_t clampedCursor =
    std::min(console.cursorIndex, console.input.size());
  const std::size_t promptCursorOffset = 2U + clampedCursor;
  std::size_t promptBaseOffset = layout.text.size();
  for (const ConsoleLayoutLine& line : layout.lines) {
    if (line.prompt) {
      promptBaseOffset = line.textOffset;
      break;
    }
  }
  const std::size_t cursorTextOffset =
    std::min(promptBaseOffset + promptCursorOffset, layout.text.size());
  bool cursorDrawn = false;
  for (std::size_t index = 0; index < layout.lines.size(); ++index) {
    const ConsoleLayoutLine& line = layout.lines[index];
    std::string text = line.text;
    if (line.prompt) {
      const std::size_t lineBegin = line.textOffset;
      const std::size_t lineEnd = line.textOffset + line.text.size();
      if (cursorTextOffset >= lineBegin && cursorTextOffset <= lineEnd) {
        text.insert(cursorTextOffset - lineBegin, 1U, '_');
        cursorDrawn = true;
      } else if (!cursorDrawn && index + 1U == layout.lines.size()) {
        text.push_back('_');
        cursorDrawn = true;
      }
    }
    addText(
      drawList,
      line.x,
      line.y,
      std::move(text),
      line.prompt ? RenderColor{255, 255, 255, 255} : RenderColor{215, 225, 235, 255},
      textScale
    );
  }
}

} // namespace

DrawList2D buildPerspectiveWeaponOverlay(
  int outputWidth,
  int outputHeight,
  const LightningGunResult& localLightningGun,
  Weapon selectedWeapon,
  Weapon previousWeapon,
  float weaponSwitchProgress,
  const RenderSettings& settings
) {
  DrawList2D drawList;
  const float hitAmount = std::clamp(settings.beamHitAmount, 0.0F, 1.0F);
  const RenderColor color = {
    blendChannel(settings.beamRed, settings.beamHitRed, hitAmount),
    blendChannel(settings.beamGreen, settings.beamHitGreen, hitAmount),
    blendChannel(settings.beamBlue, settings.beamHitBlue, hitAmount),
    static_cast<std::uint8_t>(
      std::clamp(settings.beamAlpha, 0.0F, 1.0F) * 255.0F
    ),
  };
  const float pulse = localLightningGun.active
    ? std::clamp(settings.beamPulse, -1.0F, 1.0F)
    : 0.0F;
  const float brightness = 1.0F + pulse * 0.05F;
  const RenderColor animatedColor = {
    static_cast<std::uint8_t>(
      std::clamp(
        static_cast<float>(color.red) * brightness,
        0.0F,
        255.0F
      )
    ),
    static_cast<std::uint8_t>(
      std::clamp(
        static_cast<float>(color.green) * brightness,
        0.0F,
        255.0F
      )
    ),
    static_cast<std::uint8_t>(
      std::clamp(
        static_cast<float>(color.blue) * brightness,
        0.0F,
        255.0F
      )
    ),
    color.alpha,
  };
  const RenderColor emitterColor = {
    static_cast<std::uint8_t>(
      static_cast<float>(animatedColor.red) * 0.8F
    ),
    static_cast<std::uint8_t>(
      static_cast<float>(animatedColor.green) * 0.8F
    ),
    static_cast<std::uint8_t>(
      static_cast<float>(animatedColor.blue) * 0.8F
    ),
    animatedColor.alpha,
  };
  const float centerX = static_cast<float>(outputWidth) * 0.5F;
  const float height = static_cast<float>(outputHeight);
  const float scale = std::max(0.7F, height / 720.0F);
  const float muzzleY = height - 154.0F * scale;
  if (localLightningGun.active) {
    // The covered lower section keeps the beam stable while making its
    // visible origin coincide with the viewmodel emitter.
    addLine(
      drawList,
      {centerX, height * 1.15F},
      {centerX, height * 0.5F},
      animatedColor,
      settings.beamWidth * (1.0F + pulse * 0.04F)
    );
  }
  if (!settings.showOwnWeapons) {
    return drawList;
  }

  const auto quad =
    [&](std::array<ScreenPoint, 4> points, RenderColor quadColor) {
      drawList.overlayCommands.emplace_back(
        FilledQuad2D{points, quadColor}
      );
    };
  const auto drawWeapon = [&](Weapon weapon, float yOffset) {
    const float muzzle = muzzleY + yOffset;
    const float bodyTop = muzzle + 20.0F * scale;
    const float bodyBottom = height + 18.0F * scale + yOffset;

    if (weapon == Weapon::LightningGun) {
      const float bodyHalfTop = 38.0F * scale;
      const float bodyHalfBottom = 104.0F * scale;
      quad(
        {{
          {centerX - bodyHalfTop, bodyTop},
          {centerX + bodyHalfTop, bodyTop},
          {centerX + bodyHalfBottom, bodyBottom},
          {centerX - bodyHalfBottom, bodyBottom},
        }},
        {34, 42, 52, 255}
      );
      quad(
        {{
          {centerX - 21.0F * scale, muzzle},
          {centerX + 21.0F * scale, muzzle},
          {centerX + 34.0F * scale, bodyTop + 32.0F * scale},
          {centerX - 34.0F * scale, bodyTop + 32.0F * scale},
        }},
        {67, 82, 98, 255}
      );
      quad(
        {{
          {centerX - 10.0F * scale, muzzle - 5.0F * scale},
          {centerX + 10.0F * scale, muzzle - 5.0F * scale},
          {centerX + 14.0F * scale, muzzle + 14.0F * scale},
          {centerX - 14.0F * scale, muzzle + 14.0F * scale},
        }},
        animatedColor
      );
      addRect(
        drawList,
        centerX - 72.0F * scale,
        bodyTop + 44.0F * scale,
        24.0F * scale,
        58.0F * scale,
        {52, 65, 80, 255}
      );
      addRect(
        drawList,
        centerX + 48.0F * scale,
        bodyTop + 44.0F * scale,
        24.0F * scale,
        58.0F * scale,
        {52, 65, 80, 255}
      );
      addRect(
        drawList,
        centerX - 63.0F * scale,
        bodyTop + 53.0F * scale,
        6.0F * scale,
        40.0F * scale,
        emitterColor
      );
      addRect(
        drawList,
        centerX + 57.0F * scale,
        bodyTop + 53.0F * scale,
        6.0F * scale,
        40.0F * scale,
        emitterColor
      );
      return;
    }

    if (weapon == Weapon::Railgun) {
      quad(
        {{
          {centerX - 18.0F * scale, muzzle - 8.0F * scale},
          {centerX + 18.0F * scale, muzzle - 8.0F * scale},
          {centerX + 30.0F * scale, bodyBottom},
          {centerX - 30.0F * scale, bodyBottom},
        }},
        {28, 32, 42, 255}
      );
      addRect(
        drawList,
        centerX - 15.0F * scale,
        muzzle - 18.0F * scale,
        30.0F * scale,
        132.0F * scale,
        {70, 86, 106, 255}
      );
      addRect(
        drawList,
        centerX - 8.0F * scale,
        muzzle - 26.0F * scale,
        16.0F * scale,
        142.0F * scale,
        {24, 28, 36, 255}
      );
      addRect(
        drawList,
        centerX - 4.0F * scale,
        muzzle - 24.0F * scale,
        8.0F * scale,
        132.0F * scale,
        {90, 220, 255, 255}
      );
      return;
    }

    if (weapon == Weapon::MachineGun) {
      quad(
        {{
          {centerX - 30.0F * scale, muzzle + 8.0F * scale},
          {centerX + 30.0F * scale, muzzle + 8.0F * scale},
          {centerX + 72.0F * scale, bodyBottom},
          {centerX - 72.0F * scale, bodyBottom},
        }},
        {36, 40, 43, 255}
      );
      addRect(
        drawList,
        centerX - 18.0F * scale,
        muzzle - 42.0F * scale,
        36.0F * scale,
        82.0F * scale,
        {74, 82, 88, 255}
      );
      addRect(
        drawList,
        centerX - 7.0F * scale,
        muzzle - 58.0F * scale,
        14.0F * scale,
        66.0F * scale,
        {24, 27, 30, 255}
      );
      addRect(
        drawList,
        centerX - 42.0F * scale,
        muzzle + 28.0F * scale,
        84.0F * scale,
        11.0F * scale,
        {218, 196, 116, 255}
      );
      return;
    }

    if (weapon == Weapon::Shotgun) {
      const RenderColor darkIron = {18, 20, 23, 255};
      const RenderColor barrelSteel = {84, 88, 94, 255};
      const RenderColor barrelHighlight = {162, 168, 176, 255};
      const RenderColor woodDark = {58, 35, 24, 255};
      const RenderColor woodWarm = {112, 68, 39, 255};
      const float loweredMuzzle = muzzle + 54.0F * scale;

      // Long, clean twin iron barrels, kept low and centered like a classic
      // first-person sawed-off.
      quad(
        {{
          {centerX - 48.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX - 12.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX - 4.0F * scale, bodyBottom},
          {centerX - 68.0F * scale, bodyBottom},
        }},
        barrelSteel
      );
      quad(
        {{
          {centerX + 12.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX + 48.0F * scale, loweredMuzzle - 84.0F * scale},
          {centerX + 68.0F * scale, bodyBottom},
          {centerX + 4.0F * scale, bodyBottom},
        }},
        barrelSteel
      );
      quad(
        {{
          {centerX - 22.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX - 12.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX - 6.0F * scale, bodyBottom},
          {centerX - 22.0F * scale, bodyBottom},
        }},
        barrelHighlight
      );
      quad(
        {{
          {centerX + 12.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX + 22.0F * scale, loweredMuzzle - 78.0F * scale},
          {centerX + 22.0F * scale, bodyBottom},
          {centerX + 6.0F * scale, bodyBottom},
        }},
        barrelHighlight
      );
      addRect(
        drawList,
        centerX - 5.0F * scale,
        loweredMuzzle - 80.0F * scale,
        10.0F * scale,
        100.0F * scale,
        darkIron
      );

      // Squared-off muzzle lips and dark bore openings.
      addRect(
        drawList,
        centerX - 52.0F * scale,
        loweredMuzzle - 96.0F * scale,
        44.0F * scale,
        17.0F * scale,
        barrelHighlight
      );
      addRect(
        drawList,
        centerX + 8.0F * scale,
        loweredMuzzle - 96.0F * scale,
        44.0F * scale,
        17.0F * scale,
        barrelHighlight
      );
      addRect(
        drawList,
        centerX - 36.0F * scale,
        loweredMuzzle - 93.0F * scale,
        14.0F * scale,
        12.0F * scale,
        darkIron
      );
      addRect(
        drawList,
        centerX + 22.0F * scale,
        loweredMuzzle - 93.0F * scale,
        14.0F * scale,
        12.0F * scale,
        darkIron
      );

      // Wooden holder/foregrip: the only broad mass under the clean barrels.
      quad(
        {{
          {centerX - 56.0F * scale, muzzle + 62.0F * scale},
          {centerX + 56.0F * scale, muzzle + 62.0F * scale},
          {centerX + 86.0F * scale, bodyBottom},
          {centerX - 86.0F * scale, bodyBottom},
        }},
        woodDark
      );
      addRect(
        drawList,
        centerX - 46.0F * scale,
        muzzle + 76.0F * scale,
        92.0F * scale,
        22.0F * scale,
        woodWarm
      );
      addRect(
        drawList,
        centerX - 38.0F * scale,
        muzzle + 106.0F * scale,
        76.0F * scale,
        12.0F * scale,
        {82, 48, 29, 255}
      );
      return;
    }

    if (weapon == Weapon::GrenadeLauncher) {
      quad(
        {{
          {centerX - 50.0F * scale, muzzle + 4.0F * scale},
          {centerX + 50.0F * scale, muzzle + 4.0F * scale},
          {centerX + 86.0F * scale, bodyBottom},
          {centerX - 86.0F * scale, bodyBottom},
        }},
        {33, 45, 39, 255}
      );
      addRect(
        drawList,
        centerX - 35.0F * scale,
        muzzle - 30.0F * scale,
        70.0F * scale,
        60.0F * scale,
        {72, 86, 74, 255}
      );
      addRect(
        drawList,
        centerX - 26.0F * scale,
        muzzle - 21.0F * scale,
        52.0F * scale,
        42.0F * scale,
        {24, 31, 27, 255}
      );
      addRect(
        drawList,
        centerX - 18.0F * scale,
        muzzle - 13.0F * scale,
        36.0F * scale,
        26.0F * scale,
        {112, 188, 90, 255}
      );
      return;
    }

    if (weapon == Weapon::PlasmaGun) {
      quad(
        {{
          {centerX - 42.0F * scale, muzzle + 6.0F * scale},
          {centerX + 42.0F * scale, muzzle + 6.0F * scale},
          {centerX + 91.0F * scale, bodyBottom},
          {centerX - 91.0F * scale, bodyBottom},
        }},
        {30, 39, 48, 255}
      );
      addRect(
        drawList,
        centerX - 24.0F * scale,
        muzzle - 24.0F * scale,
        48.0F * scale,
        62.0F * scale,
        {68, 82, 102, 255}
      );
      addRect(
        drawList,
        centerX - 65.0F * scale,
        muzzle + 22.0F * scale,
        28.0F * scale,
        72.0F * scale,
        {45, 58, 70, 255}
      );
      addRect(
        drawList,
        centerX + 37.0F * scale,
        muzzle + 22.0F * scale,
        28.0F * scale,
        72.0F * scale,
        {45, 58, 70, 255}
      );
      addRect(
        drawList,
        centerX - 15.0F * scale,
        muzzle - 18.0F * scale,
        30.0F * scale,
        50.0F * scale,
        {95, 235, 210, 255}
      );
      return;
    }

    quad(
      {{
        {centerX - 44.0F * scale, muzzle + 6.0F * scale},
        {centerX + 44.0F * scale, muzzle + 6.0F * scale},
        {centerX + 98.0F * scale, bodyBottom},
        {centerX - 98.0F * scale, bodyBottom},
      }},
      {45, 48, 45, 255}
    );
    addRect(
      drawList,
      centerX - 50.0F * scale,
      muzzle - 12.0F * scale,
      100.0F * scale,
      34.0F * scale,
      {80, 84, 76, 255}
    );
    addRect(
      drawList,
      centerX - 32.0F * scale,
      muzzle - 25.0F * scale,
      64.0F * scale,
      18.0F * scale,
      {34, 38, 34, 255}
    );
    addRect(
      drawList,
      centerX - 20.0F * scale,
      muzzle - 20.0F * scale,
      40.0F * scale,
      8.0F * scale,
      {185, 120, 58, 255}
    );
  };

  const float progress = std::clamp(weaponSwitchProgress, 0.0F, 1.0F);
  const auto smooth = [](float value) {
    const float t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
  };
  if (progress < 1.0F && previousWeapon != selectedWeapon) {
    constexpr float kDropPhase = 0.45F;
    if (progress < kDropPhase) {
      drawWeapon(previousWeapon, smooth(progress / kDropPhase) * 260.0F * scale);
    } else {
      const float raiseProgress = (progress - kDropPhase) / (1.0F - kDropPhase);
      drawWeapon(
        selectedWeapon,
        (1.0F - smooth(raiseProgress)) * 260.0F * scale
      );
    }
  } else {
    drawWeapon(selectedWeapon, 0.0F);
  }
  return drawList;
}

DrawList2D buildScreenUi(
  int outputWidth,
  int outputHeight,
  const PlayerState& localPlayer,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console
) {
  DrawList2D drawList;
  drawList.clip = {
    0.0F,
    0.0F,
    static_cast<float>(outputWidth),
    static_cast<float>(outputHeight),
  };
  addCrosshair(drawList, outputWidth, outputHeight, settings);
  addHitMarker(drawList, outputWidth, outputHeight, settings);
  addDamageNumbers(drawList, outputWidth, outputHeight, settings, hud);
  addSpeedText(drawList, outputWidth, outputHeight, hud, settings);
  addHud(drawList, outputWidth, outputHeight, localPlayer, hud, settings);
  addSelectedWeaponIndicator(drawList, outputWidth, outputHeight, hud, settings);
  addSettingsMenu(drawList, outputWidth, outputHeight, hud);
  addConsole(drawList, outputWidth, outputHeight, console);
  return drawList;
}

DrawList2D buildFloatingHealthBars(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const Arena& arena,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const std::array<bool, kDuelPlayerCount>& remoteRenderVisible,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  DrawList2D drawList;
  drawList.clip = {
    0.0F,
    0.0F,
    static_cast<float>(outputWidth),
    static_cast<float>(outputHeight),
  };
  for (std::size_t index = 0; index < remotePlayers.size(); ++index) {
    const RemotePlayerView& remote = remotePlayers[index];
    if (!remote.visible) {
      continue;
    }
    if (!remoteRenderVisible[index]) {
      continue;
    }
    if (
      !remote.teammate &&
      !enemyBodyVisibleFromCamera(camera, arena, remote.player)
    ) {
      continue;
    }
    addFloatingNameTag(
      drawList,
      outputWidth,
      outputHeight,
      camera,
      remote,
      settings
    );
    addFloatingHealthBar(
      drawList,
      outputWidth,
      outputHeight,
      camera,
      remote.player,
      remote.enemyHealthAlpha,
      remote.teammate,
      settings,
      hud
    );
  }
  return drawList;
}

DrawList2D buildFloatingDamageNumbers(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  DrawList2D drawList;
  drawList.clip = {
    0.0F,
    0.0F,
    static_cast<float>(outputWidth),
    static_cast<float>(outputHeight),
  };
  addFloatingDamageNumbers(
    drawList,
    outputWidth,
    outputHeight,
    camera,
    settings,
    hud
  );
  return drawList;
}

} // namespace lg
