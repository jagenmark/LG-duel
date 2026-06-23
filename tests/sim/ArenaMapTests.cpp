#include "sim/Arena.hpp"

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

  {
    const lg::Arena arena = lg::thunderstruckArena();
    failures += expect(arena.wallCount == 19, "default Thunderstruck map should load all boxes");
    failures += expect(nearlyEqual(arena.min.x, -15.0F), "default map min x should match file");
    failures += expect(nearlyEqual(arena.max.z, 10.0F), "default map max z should match file");
    failures += expect(
      nearlyEqual(arena.spawnPositions[0].x, -8.0F) &&
        nearlyEqual(arena.spawnPositions[1].x, 8.0F),
      "default map spawns should match file"
    );
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box center -1,-1,0 1,1,1
spawn p1 -2,0,0 yaw=0
spawn p2 2,0,0 yaw=180
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(result.ok, "valid map text should load");
    failures += expect(result.arena.wallCount == 1, "valid map should include one box");
    failures += expect(nearlyEqual(result.arena.spawnPositions[1].x, 2.0F), "valid map should parse spawns");
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box bad 2,0,0 1,1,1
spawn p1 -2,0,0
spawn p2 2,0,0
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(!result.ok, "inverted box should be rejected");
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box first -2,-2,0 1,1,1
box second 0,0,0 2,2,1
spawn p1 -2,0,0
spawn p2 2,0,0
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(!result.ok, "overlapping boxes should be rejected");
  }

  {
    constexpr std::string_view text = R"(version 1
bounds min=-4,-4,0 max=4,4,4
box center -1,-1,0 1,1,1
spawn p1 -2,0,0
)";
    const lg::ArenaLoadResult result = lg::loadArenaFromText(text);
    failures += expect(!result.ok, "maps need at least two spawns");
  }

  return failures == 0 ? 0 : 1;
}
