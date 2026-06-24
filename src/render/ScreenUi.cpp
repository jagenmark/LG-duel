#include "render/ScreenUi.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace lg {
namespace {

constexpr float kGlyphSize = 8.0F;

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
  float scale
) {
  drawList.overlayCommands.emplace_back(Text2D{
    {x, y},
    std::move(text),
    color,
    scale,
  });
}

[[nodiscard]] std::vector<std::string> wrapText(
  const std::string& text,
  std::size_t maxCharacters
) {
  if (maxCharacters == 0U) {
    return {""};
  }
  if (text.empty()) {
    return {""};
  }

  std::vector<std::string> lines;
  std::size_t lineStart = 0U;
  while (lineStart < text.size()) {
    const std::size_t remaining = text.size() - lineStart;
    if (remaining <= maxCharacters) {
      lines.push_back(text.substr(lineStart));
      break;
    }

    const std::size_t lineEnd = lineStart + maxCharacters;
    std::size_t breakAt = std::string::npos;
    if (lineEnd < text.size() && text[lineEnd] == ' ') {
      breakAt = lineEnd;
    }
    for (
      std::size_t index = lineEnd;
      index > lineStart && breakAt == std::string::npos;
      --index
    ) {
      if (text[index - 1U] == ' ') {
        breakAt = index - 1U;
        break;
      }
    }

    if (breakAt == std::string::npos) {
      lines.push_back(text.substr(lineStart, maxCharacters));
      lineStart += maxCharacters;
    } else {
      lines.push_back(text.substr(lineStart, breakAt - lineStart));
      lineStart = breakAt;
      while (lineStart < text.size() && text[lineStart] == ' ') {
        ++lineStart;
      }
    }
  }

  return lines;
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

[[nodiscard]] const char* weaponShortName(Weapon weapon) {
  switch (weapon) {
  case Weapon::LightningGun:
    return "LG";
  case Weapon::Railgun:
    return "RG";
  case Weapon::RocketLauncher:
    return "RL";
  }
  return "??";
}

void addWeaponIcon(
  DrawList2D& drawList,
  float centerX,
  float centerY,
  Weapon weapon,
  RenderColor color,
  float scale
) {
  if (weapon == Weapon::LightningGun) {
    addLine(
      drawList,
      {centerX - 18.0F * scale, centerY + 12.0F * scale},
      {centerX + 16.0F * scale, centerY - 13.0F * scale},
      color,
      4.0F * scale
    );
    addLine(
      drawList,
      {centerX + 16.0F * scale, centerY - 13.0F * scale},
      {centerX + 5.0F * scale, centerY + 2.0F * scale},
      color,
      2.0F * scale
    );
    addLine(
      drawList,
      {centerX + 5.0F * scale, centerY + 2.0F * scale},
      {centerX + 19.0F * scale, centerY + 1.0F * scale},
      color,
      2.0F * scale
    );
    return;
  }

  if (weapon == Weapon::Railgun) {
    addRect(
      drawList,
      centerX - 22.0F * scale,
      centerY - 3.0F * scale,
      44.0F * scale,
      6.0F * scale,
      color
    );
    addRect(
      drawList,
      centerX + 10.0F * scale,
      centerY - 9.0F * scale,
      8.0F * scale,
      18.0F * scale,
      color
    );
    return;
  }

  addRect(
    drawList,
    centerX - 22.0F * scale,
    centerY - 8.0F * scale,
    36.0F * scale,
    16.0F * scale,
    color
  );
  addRect(
    drawList,
    centerX + 13.0F * scale,
    centerY - 5.0F * scale,
    10.0F * scale,
    10.0F * scale,
    color
  );
}

void addSelectedWeaponIndicator(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud
) {
  constexpr std::array<Weapon, 3> weapons = {{
    Weapon::RocketLauncher,
    Weapon::LightningGun,
    Weapon::Railgun,
  }};
  const float scale = std::clamp(
    static_cast<float>(height) / 720.0F,
    0.75F,
    1.25F
  );
  const float slotSize = 58.0F * scale;
  const float gap = 8.0F * scale;
  const float panelWidth = slotSize + 10.0F * scale;
  const float panelHeight =
    slotSize * static_cast<float>(weapons.size()) +
    gap * static_cast<float>(weapons.size() - 1U) +
    10.0F * scale;
  const float x = static_cast<float>(width) - panelWidth - 22.0F * scale;
  const float y = (static_cast<float>(height) - panelHeight) * 0.5F;

  addRect(
    drawList,
    x,
    y,
    panelWidth,
    panelHeight,
    {6, 9, 13, 150}
  );

  float slotY = y + 5.0F * scale;
  for (Weapon weapon : weapons) {
    const bool selected = weapon == hud.selectedWeapon;
    const RenderColor frame = selected
      ? RenderColor{255, 224, 92, 255}
      : RenderColor{70, 82, 96, 210};
    const RenderColor fill = selected
      ? RenderColor{28, 34, 42, 230}
      : RenderColor{14, 18, 24, 175};
    const RenderColor icon = selected
      ? RenderColor{255, 242, 174, 255}
      : RenderColor{156, 170, 184, 220};
    const float slotX = x + 5.0F * scale;

    addRect(drawList, slotX, slotY, slotSize, slotSize, fill);
    addOutline(drawList, slotX, slotY, slotSize, slotSize, frame);
    if (selected) {
      addRect(
        drawList,
        slotX - 5.0F * scale,
        slotY + 8.0F * scale,
        3.0F * scale,
        slotSize - 16.0F * scale,
        {255, 224, 92, 255}
      );
    }

    addWeaponIcon(
      drawList,
      slotX + slotSize * 0.5F,
      slotY + slotSize * 0.42F,
      weapon,
      icon,
      scale
    );
    const char* label = weaponShortName(weapon);
    const float textScale = selected ? 1.45F * scale : 1.2F * scale;
    const float textWidth = 2.0F * kGlyphSize * textScale;
    addText(
      drawList,
      slotX + (slotSize - textWidth) * 0.5F,
      slotY + slotSize - 17.0F * scale,
      label,
      icon,
      textScale
    );

    slotY += slotSize + gap;
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

void addCrosshair(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings
) {
  if (!settings.crosshairEnabled) {
    return;
  }

  const float centerX = settings.crosshairUseScreenPosition
    ? std::clamp(settings.crosshairScreenX, 0.0F, static_cast<float>(width))
    : static_cast<float>(width) * 0.5F;
  const float centerY = settings.crosshairUseScreenPosition
    ? std::clamp(settings.crosshairScreenY, 0.0F, static_cast<float>(height))
    : static_cast<float>(height) * 0.5F;
  const float size = settings.crosshairSize;
  const float gap = settings.crosshairGap;
  const float thickness = settings.crosshairThickness;
  const float hitAmount = std::clamp(settings.crosshairHitAmount, 0.0F, 1.0F);
  const RenderColor color = {
    blendChannel(settings.crosshairRed, settings.crosshairHitRed, hitAmount),
    blendChannel(settings.crosshairGreen, settings.crosshairHitGreen, hitAmount),
    blendChannel(settings.crosshairBlue, settings.crosshairHitBlue, hitAmount),
    static_cast<std::uint8_t>(
      std::clamp(settings.crosshairAlpha, 0.0F, 1.0F) * 255.0F
    ),
  };

  if (settings.crosshairStyle == 2) {
    addRect(
      drawList,
      centerX - thickness * 0.5F,
      centerY - thickness * 0.5F,
      thickness,
      thickness,
      color
    );
    return;
  }

  addRect(
    drawList,
    centerX - gap - size,
    centerY - thickness * 0.5F,
    size,
    thickness,
    color
  );
  addRect(
    drawList,
    centerX + gap,
    centerY - thickness * 0.5F,
    size,
    thickness,
    color
  );
  addRect(
    drawList,
    centerX - thickness * 0.5F,
    centerY - gap - size,
    thickness,
    size,
    color
  );
  addRect(
    drawList,
    centerX - thickness * 0.5F,
    centerY + gap,
    thickness,
    size,
    color
  );
  if (settings.crosshairStyle == 1) {
    addRect(
      drawList,
      centerX - 1.0F,
      centerY - 1.0F,
      2.0F,
      2.0F,
      color
    );
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

void addHud(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud,
  const RenderSettings& settings
) {
  constexpr float textScale = 2.0F;
  constexpr float characterWidth = kGlyphSize * textScale;
  constexpr RenderColor defaultText = {235, 242, 250, 255};

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

    float scoreboardY = panelY + 20.0F;
    for (std::size_t index = 0; index < hud.scoreboardLines.size(); ++index) {
      const std::string& line = hud.scoreboardLines[index];
      const float lineWidth =
        static_cast<float>(line.size()) * characterWidth;
      const float x =
        panelX + std::max(16.0F, (panelWidth - lineWidth) * 0.5F);
      const Team team = index < hud.scoreboardLineTeams.size()
        ? hud.scoreboardLineTeams[index]
        : Team::None;
      if (team == Team::None) {
        addText(
          drawList,
          x,
          scoreboardY,
          line,
          index == 0
            ? RenderColor{255, 220, 120, 255}
            : RenderColor{225, 235, 245, 255},
          textScale
        );
      } else {
        constexpr std::size_t nameColumnWidth = 22U;
        const std::size_t split = std::min(nameColumnWidth, line.size());
        const RenderColor teamColor = team == Team::Red
          ? RenderColor{224, 82, 92, 255}
          : RenderColor{82, 190, 224, 255};
        addText(
          drawList,
          x,
          scoreboardY,
          line.substr(0, split),
          teamColor,
          textScale
        );
        addText(
          drawList,
          x + static_cast<float>(split) * characterWidth,
          scoreboardY,
          line.substr(split),
          RenderColor{225, 235, 245, 255},
          textScale
        );
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
    const float x = std::max(
      12.0F,
      (static_cast<float>(width) -
       static_cast<float>(line.size()) * characterWidth) * 0.5F
    );
    addText(drawList, x, y, line, defaultText, textScale);
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

  const float healthCharacterWidth = kGlyphSize * settings.healthTextScale;
  const float healthLineHeight = 11.0F * settings.healthTextScale;
  y = static_cast<float>(height) - 24.0F -
    static_cast<float>(hud.bottomCenterLines.size()) * healthLineHeight;
  for (const std::string& line : hud.bottomCenterLines) {
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

  y = static_cast<float>(height) - 150.0F -
    static_cast<float>(hud.chatLines.size()) * 18.0F;
  for (const std::string& line : hud.chatLines) {
    addText(drawList, 16.0F, y, line, {225, 235, 245, 255}, 2.0F);
    y += 18.0F;
  }
  if (hud.chatInputOpen) {
    addText(
      drawList,
      16.0F,
      static_cast<float>(height) - 125.0F,
      "SAY: " + hud.chatInput + '_',
      {255, 232, 150, 255},
      2.0F
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
  constexpr float lineHeight = 20.0F;
  constexpr float marginX = 10.0F;
  const std::size_t maxCharacters = std::max(
    1,
    static_cast<int>(
      (static_cast<float>(width) - marginX * 2.0F) /
      (kGlyphSize * textScale)
    )
  );
  std::vector<std::string> wrappedOutput;
  for (const std::string& line : console.lines) {
    std::vector<std::string> wrappedLine = wrapText(line, maxCharacters);
    wrappedOutput.insert(
      wrappedOutput.end(),
      std::make_move_iterator(wrappedLine.begin()),
      std::make_move_iterator(wrappedLine.end())
    );
  }

  const std::vector<std::string> wrappedPrompt =
    wrapText("] " + console.input + '_', maxCharacters);
  const float promptY =
    consoleHeight - 24.0F -
    static_cast<float>(wrappedPrompt.size() - 1U) * lineHeight;
  const float outputHeight = std::max(0.0F, promptY - 10.0F);
  const int visibleLines =
    std::max(0, static_cast<int>(outputHeight / lineHeight));
  const std::size_t firstLine =
    wrappedOutput.size() > static_cast<std::size_t>(visibleLines)
      ? wrappedOutput.size() - static_cast<std::size_t>(visibleLines)
      : 0U;
  float y = 10.0F;
  for (std::size_t index = firstLine; index < wrappedOutput.size(); ++index) {
    addText(
      drawList,
      marginX,
      y,
      wrappedOutput[index],
      {215, 225, 235, 255},
      textScale
    );
    y += lineHeight;
  }
  y = promptY;
  for (const std::string& line : wrappedPrompt) {
    addText(
      drawList,
      marginX,
      y,
      line,
      {255, 255, 255, 255},
      textScale
    );
    y += lineHeight;
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
  const PlayerState& opponent,
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
  (void)opponent;
  addCrosshair(drawList, outputWidth, outputHeight, settings);
  addHitMarker(drawList, outputWidth, outputHeight, settings);
  addHud(drawList, outputWidth, outputHeight, hud, settings);
  addSelectedWeaponIndicator(drawList, outputWidth, outputHeight, hud);
  addConsole(drawList, outputWidth, outputHeight, console);
  return drawList;
}

DrawList2D buildFloatingHealthBars(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
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
  for (const RemotePlayerView& remote : remotePlayers) {
    if (!remote.visible) {
      continue;
    }
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

} // namespace lg
