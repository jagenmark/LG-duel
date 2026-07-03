#include "shared/Math.hpp"
#include "sim/Collision.hpp"
#include "sim/PlayerState.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
  int failures = 0;
  const lg::Arena arena;

  {
    lg::PlayerState first;
    first.position = {0.0F, 0.0F, first.bounds.halfHeight};
    first.velocity = {2.0F, 0.0F, 1.0F};
    lg::PlayerState second;
    second.position = {0.4F, 0.0F, second.bounds.halfHeight};
    second.velocity = {-2.0F, 0.0F, -1.0F};

    failures += expect(
      lg::resolvePlayerCollision(arena, first, second),
      "overlapping players should collide"
    );
    failures += expect(
      nearlyEqual(
        lg::length(second.position - first.position),
        first.bounds.radius + second.bounds.radius
      ),
      "collision should separate players to combined radius"
    );
    failures += expect(nearlyEqual(first.velocity.x, 0.0F), "collision should remove first inward velocity");
    failures += expect(nearlyEqual(second.velocity.x, 0.0F), "collision should remove second inward velocity");
    failures += expect(nearlyEqual(first.velocity.z, 1.0F), "collision should preserve first vertical velocity");
    failures += expect(nearlyEqual(second.velocity.z, -1.0F), "collision should preserve second vertical velocity");
  }

  {
    lg::PlayerState first;
    first.position = {0.0F, 0.0F, 1.0F};
    lg::PlayerState second;
    second.position = {0.0F, 0.0F, 3.0F};

    failures += expect(
      !lg::resolvePlayerCollision(arena, first, second),
      "vertically separated players should not collide"
    );
    failures += expect(nearlyEqual(first.position.x, 0.0F), "non-collision should not move first player");
    failures += expect(nearlyEqual(second.position.x, 0.0F), "non-collision should not move second player");
  }

  {
    lg::PlayerState first;
    first.position = {0.0F, 0.0F, first.bounds.halfHeight};
    lg::PlayerState second = first;

    failures += expect(
      lg::resolvePlayerCollision(arena, first, second),
      "coincident players should separate"
    );
    failures += expect(first.position.x < second.position.x, "coincident separation should be deterministic");
  }

  {
    lg::PlayerState first;
    first.position = {
      arena.min.x + first.bounds.radius,
      0.0F,
      first.bounds.halfHeight,
    };
    lg::PlayerState second = first;
    second.position.x += 0.4F;

    failures += expect(
      lg::resolvePlayerCollision(arena, first, second),
      "wall-adjacent overlap should collide"
    );
    failures += expect(
      nearlyEqual(first.position.x, arena.min.x + first.bounds.radius),
      "wall-adjacent collision should keep blocked player in arena"
    );
    failures += expect(
      nearlyEqual(second.position.x - first.position.x, first.bounds.radius + second.bounds.radius),
      "wall-adjacent collision should move player with available space"
    );
  }

  {
    lg::Arena brushArena;
    brushArena.min = {-200.0F, -200.0F, -200.0F};
    brushArena.max = {200.0F, 200.0F, 200.0F};
    brushArena.brushCount = 1;
    lg::ArenaBrush& brush = brushArena.brushes[0];
    brush.min = {-1.0F, -1.0F, 0.0F};
    brush.max = {1.0F, 1.0F, 2.0F};
    brush.faceCount = 1;
    brush.faces[0].normal = {1.0F, 0.0F, 0.0F};
    brush.faces[0].distance = 100.0F;

    lg::PlayerState player;
    player.position = {10.0F, 0.0F, 1.0F};
    const lg::CollisionResult result = lg::resolvePlayerArenaCollision(
      brushArena,
      player,
      {10.0F, 0.0F, 1.0F},
      {-1.0F, 0.0F, 0.0F}
    );

    failures += expect(
      nearlyEqual(result.position.x, 10.0F) &&
        nearlyEqual(result.position.y, 0.0F) &&
        nearlyEqual(result.position.z, 1.0F),
      "brush planes should not collide when swept player bounds miss brush bounds"
    );
    failures += expect(
      nearlyEqual(result.velocity.x, -1.0F),
      "out-of-bounds brush plane rejection should preserve velocity"
    );
  }

  return failures == 0 ? 0 : 1;
}
