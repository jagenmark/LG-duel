#include "render/ScreenUi.hpp"
#include "app/TextInput.hpp"
#include "render/ChatLayout.hpp"
#include "render/ConsoleLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

[[nodiscard]] const char* hudWeaponShortName(Weapon weapon) {
  switch (weapon) {
  case Weapon::MachineGun:
    return "MG";
  case Weapon::Shotgun:
    return "SG";
  case Weapon::GrenadeLauncher:
    return "GL";
  case Weapon::LightningGun:
    return "LG";
  case Weapon::Railgun:
    return "RG";
  case Weapon::RocketLauncher:
    return "RL";
  case Weapon::PlasmaGun:
    return "PG";
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

  if (weapon == Weapon::MachineGun) {
    rect(-15.0F, -14.0F, 30.0F, 20.0F);
    rect(-5.0F, -22.0F, 10.0F, 26.0F);
    rect(-20.0F, 8.0F, 40.0F, 5.0F);
    line(-2.0F, -22.0F, -2.0F, -30.0F, 2.0F);
    line(2.0F, -22.0F, 2.0F, -30.0F, 2.0F);
    return;
  }

  if (weapon == Weapon::Shotgun) {
    rect(-21.0F, -15.0F, 42.0F, 6.0F);
    rect(-21.0F, -5.0F, 42.0F, 6.0F);
    rect(-17.0F, 8.0F, 34.0F, 8.0F);
    line(-23.0F, 15.0F, 23.0F, 15.0F, 3.0F);
    return;
  }

  if (weapon == Weapon::GrenadeLauncher) {
    rect(-21.0F, -11.0F, 42.0F, 20.0F);
    rect(-14.0F, -18.0F, 28.0F, 32.0F);
    rect(-8.0F, -12.0F, 16.0F, 22.0F);
    line(-20.0F, 13.0F, 20.0F, 13.0F, 3.0F);
    return;
  }

  if (weapon == Weapon::RocketLauncher) {
    rect(-23.0F, -10.0F, 46.0F, 20.0F);
    rect(-15.0F, -20.0F, 30.0F, 8.0F);
    line(-17.0F, -1.0F, 17.0F, -1.0F, 4.0F);
    line(-11.0F, 11.0F, 11.0F, 11.0F, 3.0F);
    return;
  }

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
    rect(-20.0F, -3.0F, 40.0F, 6.0F);
    rect(-4.0F, -19.0F, 8.0F, 34.0F);
    rect(10.0F, -9.0F, 8.0F, 18.0F);
    line(-17.0F, -9.0F, 17.0F, -9.0F, 2.0F);
    return;
  }

  if (weapon == Weapon::PlasmaGun) {
    rect(-12.0F, -20.0F, 24.0F, 30.0F);
    rect(-25.0F, -1.0F, 10.0F, 24.0F);
    rect(15.0F, -1.0F, 10.0F, 24.0F);
    rect(-7.0F, -14.0F, 14.0F, 20.0F);
    line(-18.0F, 12.0F, 18.0F, 12.0F, 3.0F);
  }
}

void addSelectedWeaponIndicator(
  DrawList2D& drawList,
  int width,
  int height,
  const HudRenderState& hud
) {
  constexpr std::array<Weapon, 7> weapons = {{
    Weapon::MachineGun,
    Weapon::Shotgun,
    Weapon::GrenadeLauncher,
    Weapon::RocketLauncher,
    Weapon::LightningGun,
    Weapon::Railgun,
    Weapon::PlasmaGun,
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
      slotY + slotSize * 0.36F,
      weapon,
      icon,
      scale
    );
    const char* label = hudWeaponShortName(weapon);
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

[[nodiscard]] bool segmentIntersectsWallBeforeEnd(
  Vec3 start,
  Vec3 end,
  const ArenaWall& wall
) {
  const Vec3 direction = end - start;
  float entry = 0.0F;
  float exit = 1.0F;

  const auto clipAxis = [&entry, &exit](
    float origin,
    float axisDirection,
    float minimum,
    float maximum
  ) {
    if (std::fabs(axisDirection) <= 0.00001F) {
      return origin >= minimum && origin <= maximum;
    }
    const float first = (minimum - origin) / axisDirection;
    const float second = (maximum - origin) / axisDirection;
    entry = std::max(entry, std::min(first, second));
    exit = std::min(exit, std::max(first, second));
    return entry <= exit;
  };

  if (
    !clipAxis(start.x, direction.x, wall.min.x, wall.max.x) ||
    !clipAxis(start.y, direction.y, wall.min.y, wall.max.y) ||
    !clipAxis(start.z, direction.z, wall.min.z, wall.max.z)
  ) {
    return false;
  }
  return exit >= 0.0F && entry < 0.999F;
}

[[nodiscard]] bool hasClearLineToPoint(
  const PerspectiveCamera& camera,
  const Arena& arena,
  Vec3 point
) {
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    if (
      segmentIntersectsWallBeforeEnd(
        camera.position,
        point,
        arena.walls[index]
      )
    ) {
      return false;
    }
  }
  return true;
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

void addDamageNumbers(
  DrawList2D& drawList,
  int width,
  int height,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  const float baseX = settings.crosshairUseScreenPosition
    ? std::clamp(settings.crosshairScreenX, 0.0F, static_cast<float>(width))
    : static_cast<float>(width) * 0.5F;
  const float baseY = settings.crosshairUseScreenPosition
    ? std::clamp(settings.crosshairScreenY, 0.0F, static_cast<float>(height))
    : static_cast<float>(height) * 0.5F;
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
      quad(
        {{
          {centerX - 58.0F * scale, muzzle + 12.0F * scale},
          {centerX + 58.0F * scale, muzzle + 12.0F * scale},
          {centerX + 94.0F * scale, bodyBottom},
          {centerX - 94.0F * scale, bodyBottom},
        }},
        {42, 38, 34, 255}
      );
      addRect(
        drawList,
        centerX - 42.0F * scale,
        muzzle - 31.0F * scale,
        84.0F * scale,
        19.0F * scale,
        {30, 34, 36, 255}
      );
      addRect(
        drawList,
        centerX - 42.0F * scale,
        muzzle - 7.0F * scale,
        84.0F * scale,
        19.0F * scale,
        {30, 34, 36, 255}
      );
      addRect(
        drawList,
        centerX - 54.0F * scale,
        muzzle + 32.0F * scale,
        108.0F * scale,
        22.0F * scale,
        {188, 120, 84, 255}
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
  addDamageNumbers(drawList, outputWidth, outputHeight, settings, hud);
  addHud(drawList, outputWidth, outputHeight, hud, settings);
  addSelectedWeaponIndicator(drawList, outputWidth, outputHeight, hud);
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
