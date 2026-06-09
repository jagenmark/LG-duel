#include "render/Renderer.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>

namespace lg {
namespace {

#if LG_DUEL_HAS_SDL3
[[nodiscard]] SDL_FRect rect(float x, float y, float w, float h) {
  return SDL_FRect{x, y, w, h};
}

[[nodiscard]] float remap(float value, float inMin, float inMax, float outMin, float outMax) {
  const float t = (value - inMin) / (inMax - inMin);
  return outMin + (std::clamp(t, 0.0F, 1.0F) * (outMax - outMin));
}
#endif

} // namespace

Renderer::~Renderer() {
  shutdown();
}

bool Renderer::initialize(void* window) {
#if LG_DUEL_HAS_SDL3
  renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window), nullptr);
  return renderer_ != nullptr;
#else
  (void)window;
  return false;
#endif
}

void Renderer::render(
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& lightningGun
) {
#if LG_DUEL_HAS_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(renderer_);
  if (renderer == nullptr) {
    return;
  }

  int width = 0;
  int height = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

  SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
  SDL_RenderClear(renderer);

  const float margin = 40.0F;
  const float arenaSize = static_cast<float>(std::min(width, height)) - (margin * 2.0F);
  const float arenaLeft = margin;
  const float arenaTop = margin;

  SDL_SetRenderDrawColor(renderer, 54, 61, 72, 255);
  const SDL_FRect arenaRect = rect(arenaLeft, arenaTop, arenaSize, arenaSize);
  SDL_RenderRect(renderer, &arenaRect);

  const float playerX = remap(player.position.x, arena.min.x, arena.max.x, arenaLeft, arenaLeft + arenaSize);
  const float playerY = remap(player.position.y, arena.min.y, arena.max.y, arenaTop + arenaSize, arenaTop);
  const float opponentX = remap(
    opponent.position.x,
    arena.min.x,
    arena.max.x,
    arenaLeft,
    arenaLeft + arenaSize
  );
  const float opponentY = remap(
    opponent.position.y,
    arena.min.y,
    arena.max.y,
    arenaTop + arenaSize,
    arenaTop
  );

  if (lightningGun.active) {
    const float beamStartX = remap(
      lightningGun.start.x,
      arena.min.x,
      arena.max.x,
      arenaLeft,
      arenaLeft + arenaSize
    );
    const float beamStartY = remap(
      lightningGun.start.y,
      arena.min.y,
      arena.max.y,
      arenaTop + arenaSize,
      arenaTop
    );
    const float beamEndX = remap(
      lightningGun.end.x,
      arena.min.x,
      arena.max.x,
      arenaLeft,
      arenaLeft + arenaSize
    );
    const float beamEndY = remap(
      lightningGun.end.y,
      arena.min.y,
      arena.max.y,
      arenaTop + arenaSize,
      arenaTop
    );
    if (lightningGun.hit) {
      SDL_SetRenderDrawColor(renderer, 245, 250, 255, 255);
    } else {
      SDL_SetRenderDrawColor(renderer, 74, 166, 255, 255);
    }
    SDL_RenderLine(renderer, beamStartX, beamStartY, beamEndX, beamEndY);
  }

  const float radius = 7.0F;
  SDL_SetRenderDrawColor(renderer, 224, 82, 92, 255);
  const SDL_FRect opponentRect = rect(
    opponentX - radius,
    opponentY - radius,
    radius * 2.0F,
    radius * 2.0F
  );
  SDL_RenderFillRect(renderer, &opponentRect);

  const float opponentHealthRatio =
    std::clamp(static_cast<float>(opponent.health) / 100.0F, 0.0F, 1.0F);
  SDL_SetRenderDrawColor(renderer, 224, 82, 92, 255);
  const SDL_FRect opponentHealthRect = rect(
    opponentX - 18.0F,
    opponentY - 16.0F,
    36.0F * opponentHealthRatio,
    4.0F
  );
  SDL_RenderFillRect(renderer, &opponentHealthRect);

  SDL_SetRenderDrawColor(renderer, 66, 211, 146, 255);
  const SDL_FRect playerRect = rect(playerX - radius, playerY - radius, radius * 2.0F, radius * 2.0F);
  SDL_RenderFillRect(renderer, &playerRect);

  const Vec3 forward = yawForward(player.viewYawRadians);
  SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
  SDL_RenderLine(renderer, playerX, playerY, playerX + (forward.x * 24.0F), playerY - (forward.y * 24.0F));

  const float speed = length(player.velocity);
  const float speedBarWidth = std::min(speed / 12.0F, 1.0F) * 220.0F;
  SDL_SetRenderDrawColor(renderer, 86, 156, 214, 255);
  const SDL_FRect speedRect = rect(24.0F, static_cast<float>(height) - 34.0F, speedBarWidth, 10.0F);
  SDL_RenderFillRect(renderer, &speedRect);

  const float zValue = remap(player.position.z, arena.min.z, arena.max.z, 0.0F, 180.0F);
  SDL_SetRenderDrawColor(renderer, 234, 196, 106, 255);
  const SDL_FRect heightRect = rect(static_cast<float>(width) - 34.0F, static_cast<float>(height) - 24.0F - zValue, 10.0F, zValue);
  SDL_RenderFillRect(renderer, &heightRect);

  SDL_RenderPresent(renderer);
#else
  (void)arena;
  (void)player;
  (void)opponent;
  (void)lightningGun;
#endif
}

void Renderer::shutdown() {
#if LG_DUEL_HAS_SDL3
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
    renderer_ = nullptr;
  }
#endif
}

} // namespace lg
