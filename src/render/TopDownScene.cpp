#include "render/TopDownScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lg {
namespace {

struct ViewProjection {
  float arenaLeft = 0.0F;
  float arenaTop = 0.0F;
  float arenaSize = 0.0F;
  float worldHalfExtent = 10.0F;
  Vec3 origin = {};
  Vec3 forward = {};
  Vec3 right = {};
  bool rotated = false;
};

[[nodiscard]] float project(
  float value,
  float inMin,
  float inMax,
  float outMin,
  float outMax
) {
  const float t = (value - inMin) / (inMax - inMin);
  return outMin + (t * (outMax - outMin));
}

[[nodiscard]] float remap(
  float value,
  float inMin,
  float inMax,
  float outMin,
  float outMax
) {
  const float t = (value - inMin) / (inMax - inMin);
  return outMin + (std::clamp(t, 0.0F, 1.0F) * (outMax - outMin));
}

[[nodiscard]] std::uint8_t alphaChannel(float alpha) {
  return static_cast<std::uint8_t>(
    std::clamp(alpha, 0.0F, 1.0F) * 255.0F
  );
}

[[nodiscard]] std::uint8_t blendChannel(
  std::uint8_t base,
  std::uint8_t highlight,
  float amount
) {
  return static_cast<std::uint8_t>(
    std::clamp(
      static_cast<float>(base) +
        ((static_cast<float>(highlight) - static_cast<float>(base)) * amount),
      0.0F,
      255.0F
    )
  );
}

[[nodiscard]] RenderColor enemyColor(const RenderSettings& settings) {
  const float amount = std::clamp(settings.enemyHitAmount, 0.0F, 1.0F);
  return {
    blendChannel(settings.enemyRed, settings.enemyHitRed, amount),
    blendChannel(settings.enemyGreen, settings.enemyHitGreen, amount),
    blendChannel(settings.enemyBlue, settings.enemyHitBlue, amount),
    alphaChannel(settings.enemyAlpha),
  };
}

[[nodiscard]] RenderColor localBeamColor(const RenderSettings& settings) {
  const float amount = std::clamp(settings.beamHitAmount, 0.0F, 1.0F);
  return {
    blendChannel(settings.beamRed, settings.beamHitRed, amount),
    blendChannel(settings.beamGreen, settings.beamHitGreen, amount),
    blendChannel(settings.beamBlue, settings.beamHitBlue, amount),
    alphaChannel(settings.beamAlpha),
  };
}

[[nodiscard]] RenderColor animateBeamColor(
  RenderColor color,
  float pulse
) {
  const float scale = 1.0F + std::clamp(pulse, -1.0F, 1.0F) * 0.05F;
  color.red = static_cast<std::uint8_t>(
    std::clamp(static_cast<float>(color.red) * scale, 0.0F, 255.0F)
  );
  color.green = static_cast<std::uint8_t>(
    std::clamp(static_cast<float>(color.green) * scale, 0.0F, 255.0F)
  );
  color.blue = static_cast<std::uint8_t>(
    std::clamp(static_cast<float>(color.blue) * scale, 0.0F, 255.0F)
  );
  return color;
}

[[nodiscard]] Vec3 worldToView(
  const ViewProjection& view,
  Vec3 worldPosition
) {
  if (!view.rotated) {
    return worldPosition;
  }

  const Vec3 offset = worldPosition - view.origin;
  return {
    dot(offset, view.right),
    dot(offset, view.forward),
    worldPosition.z,
  };
}

[[nodiscard]] ScreenPoint worldToScreen(
  const ViewProjection& view,
  Vec3 worldPosition
) {
  const Vec3 viewPosition = worldToView(view, worldPosition);
  const float minX = view.rotated
    ? -view.worldHalfExtent
    : view.origin.x - view.worldHalfExtent;
  const float maxX = view.rotated
    ? view.worldHalfExtent
    : view.origin.x + view.worldHalfExtent;
  const float minY = view.rotated
    ? -view.worldHalfExtent
    : view.origin.y - view.worldHalfExtent;
  const float maxY = view.rotated
    ? view.worldHalfExtent
    : view.origin.y + view.worldHalfExtent;
  return {
    project(
      viewPosition.x,
      minX,
      maxX,
      view.arenaLeft,
      view.arenaLeft + view.arenaSize
    ),
    project(
      viewPosition.y,
      minY,
      maxY,
      view.arenaTop + view.arenaSize,
      view.arenaTop
    ),
  };
}

[[nodiscard]] std::array<ScreenPoint, 4> rectanglePoints(
  float x,
  float y,
  float width,
  float height
) {
  return {{
    {x, y},
    {x + width, y},
    {x + width, y + height},
    {x, y + height},
  }};
}

void addFilledQuad(
  std::vector<DrawCommand2D>& commands,
  std::array<ScreenPoint, 4> points,
  RenderColor color
) {
  commands.emplace_back(FilledQuad2D{points, color});
}

void addFilledRect(
  std::vector<DrawCommand2D>& commands,
  float x,
  float y,
  float width,
  float height,
  RenderColor color
) {
  addFilledQuad(
    commands,
    rectanglePoints(x, y, width, height),
    color
  );
}

void addLine(
  std::vector<DrawCommand2D>& commands,
  ScreenPoint start,
  ScreenPoint end,
  RenderColor color,
  float width = 1.0F
) {
  commands.emplace_back(Line2D{
    start,
    end,
    color,
    width,
  });
}

void addQuadOutline(
  std::vector<DrawCommand2D>& commands,
  const std::array<ScreenPoint, 4>& points,
  RenderColor color
) {
  for (std::size_t index = 0; index < points.size(); ++index) {
    addLine(
      commands,
      points[index],
      points[(index + 1U) % points.size()],
      color
    );
  }
}

void addHitMarker(
  std::vector<DrawCommand2D>& commands,
  ScreenPoint center
) {
  constexpr float inner = 5.0F;
  constexpr float outer = 10.0F;
  constexpr RenderColor color = {255, 244, 196, 255};
  addLine(
    commands,
    {center.x - outer, center.y - outer},
    {center.x - inner, center.y - inner},
    color
  );
  addLine(
    commands,
    {center.x + inner, center.y + inner},
    {center.x + outer, center.y + outer},
    color
  );
  addLine(
    commands,
    {center.x + inner, center.y - inner},
    {center.x + outer, center.y - outer},
    color
  );
  addLine(
    commands,
    {center.x - outer, center.y + outer},
    {center.x - inner, center.y + inner},
    color
  );
}

} // namespace

DrawList2D buildTopDownScene(
  int outputWidth,
  int outputHeight,
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& localLightningGun,
  const LightningGunResult& opponentLightningGun,
  const RenderSettings& settings,
  const HudRenderState& hud
) {
  constexpr float margin = 40.0F;
  const float arenaSize =
    static_cast<float>(std::min(outputWidth, outputHeight)) - (margin * 2.0F);
  const float arenaLeft =
    (static_cast<float>(outputWidth) - arenaSize) * 0.5F;
  const float arenaTop =
    (static_cast<float>(outputHeight) - arenaSize) * 0.5F;
  const float worldHalfExtent =
    10.0F * (settings.fieldOfView / 90.0F) / settings.cameraZoom;
  const ViewProjection view{
    arenaLeft,
    arenaTop,
    arenaSize,
    worldHalfExtent,
    player.position,
    yawForward(player.viewYawRadians),
    yawRight(player.viewYawRadians),
    settings.rotateView,
  };

  DrawList2D drawList;
  drawList.clip = {arenaLeft, arenaTop, arenaSize, arenaSize};

  const std::array<ScreenPoint, 4> arenaCorners = {
    worldToScreen(view, {arena.min.x, arena.min.y, 0.0F}),
    worldToScreen(view, {arena.max.x, arena.min.y, 0.0F}),
    worldToScreen(view, {arena.max.x, arena.max.y, 0.0F}),
    worldToScreen(view, {arena.min.x, arena.max.y, 0.0F}),
  };
  addQuadOutline(drawList.commands, arenaCorners, {54, 61, 72, 255});

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    const std::array<ScreenPoint, 4> wallCorners = {
      worldToScreen(view, {wall.min.x, wall.min.y, 0.0F}),
      worldToScreen(view, {wall.max.x, wall.min.y, 0.0F}),
      worldToScreen(view, {wall.max.x, wall.max.y, 0.0F}),
      worldToScreen(view, {wall.min.x, wall.max.y, 0.0F}),
    };
    addFilledQuad(drawList.commands, wallCorners, {31, 38, 48, 255});
    addQuadOutline(drawList.commands, wallCorners, {82, 95, 112, 255});
  }

  if (settings.showLagCompensation && localLightningGun.hasRewindDebug) {
    const auto addTargetBounds =
      [&](Vec3 position, CollisionBounds bounds, RenderColor color) {
        const std::array<ScreenPoint, 4> corners = {
          worldToScreen(
            view,
            {position.x - bounds.radius, position.y - bounds.radius, position.z}
          ),
          worldToScreen(
            view,
            {position.x + bounds.radius, position.y - bounds.radius, position.z}
          ),
          worldToScreen(
            view,
            {position.x + bounds.radius, position.y + bounds.radius, position.z}
          ),
          worldToScreen(
            view,
            {position.x - bounds.radius, position.y + bounds.radius, position.z}
          ),
        };
        addQuadOutline(drawList.commands, corners, color);
      };
    addTargetBounds(
      localLightningGun.currentTargetPosition,
      localLightningGun.currentTargetBounds,
      {64, 220, 255, 255}
    );
    addTargetBounds(
      localLightningGun.rewoundTargetPosition,
      localLightningGun.rewoundTargetBounds,
      {255, 190, 64, 255}
    );
  }

  const auto addBeam =
    [&](const LightningGunResult& beam, bool local) {
      if (!beam.active) {
        return;
      }

      RenderColor color = local
        ? localBeamColor(settings)
        : RenderColor{
            settings.enemyBeamRed,
            settings.enemyBeamGreen,
            settings.enemyBeamBlue,
            alphaChannel(settings.enemyBeamAlpha),
          };
      color = animateBeamColor(color, settings.beamPulse);
      if (!local && beam.hit) {
        color.red = static_cast<std::uint8_t>(
          std::min(255, static_cast<int>(color.red) + 60)
        );
        color.green = static_cast<std::uint8_t>(
          std::min(255, static_cast<int>(color.green) + 60)
        );
        color.blue = static_cast<std::uint8_t>(
          std::min(255, static_cast<int>(color.blue) + 60)
        );
      }

      const ScreenPoint start = worldToScreen(view, beam.start);
      const ScreenPoint end = worldToScreen(view, beam.end);
      addLine(
        drawList.commands,
        start,
        end,
        color,
        (local ? settings.beamWidth : settings.enemyBeamWidth) *
          (1.0F + settings.beamPulse * 0.04F)
      );
      if (beam.hit) {
        addHitMarker(drawList.commands, end);
      }
    };
  addBeam(opponentLightningGun, false);
  addBeam(localLightningGun, true);

  const ScreenPoint playerScreen = worldToScreen(view, player.position);
  const ScreenPoint opponentScreen = worldToScreen(view, opponent.position);
  const float playerSize = settings.playerSizePixels;
  const float radius = playerSize * 0.5F;
  addFilledRect(
    drawList.commands,
    opponentScreen.x - radius,
    opponentScreen.y - radius,
    playerSize,
    playerSize,
    enemyColor(settings)
  );

  if (hud.showOpponentHealthBar) {
    const float healthRatio =
      std::clamp(static_cast<float>(opponent.health) / 100.0F, 0.0F, 1.0F);
    const float healthBarHalfWidth = playerSize * (18.0F / 14.0F);
    const float healthBarOffset = playerSize + 2.0F;
    const float healthBarHeight =
      std::max(2.0F, playerSize * (4.0F / 14.0F));
    addFilledRect(
      drawList.commands,
      opponentScreen.x - healthBarHalfWidth,
      opponentScreen.y - healthBarOffset,
      healthBarHalfWidth * 2.0F * healthRatio,
      healthBarHeight,
      {224, 82, 92, 255}
    );
  }

  addFilledRect(
    drawList.commands,
    playerScreen.x - radius,
    playerScreen.y - radius,
    playerSize,
    playerSize,
    {66, 211, 146, 255}
  );
  addLine(
    drawList.commands,
    playerScreen,
    worldToScreen(
      view,
      player.position + (yawForward(player.viewYawRadians) * 1.5F)
    ),
    {230, 240, 255, 255}
  );

  const float speed = length(player.velocity);
  const float speedBarWidth = std::min(speed / 12.0F, 1.0F) * 220.0F;
  addFilledRect(
    drawList.overlayCommands,
    24.0F,
    static_cast<float>(outputHeight) - 34.0F,
    speedBarWidth,
    10.0F,
    {86, 156, 214, 255}
  );

  const float zValue =
    remap(player.position.z, arena.min.z, arena.max.z, 0.0F, 180.0F);
  addFilledRect(
    drawList.overlayCommands,
    static_cast<float>(outputWidth) - 34.0F,
    static_cast<float>(outputHeight) - 24.0F - zValue,
    10.0F,
    zValue,
    {234, 196, 106, 255}
  );

  return drawList;
}

} // namespace lg
