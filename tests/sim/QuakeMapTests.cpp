#include "map/MapParser.hpp"
#include "map/MapToArena.hpp"
#include "sim/Arena.hpp"

#include <cmath>
#include <iostream>
#include <string>
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

std::string cuboidBrush(
  float minX,
  float minY,
  float minZ,
  float maxX,
  float maxY,
  float maxZ
) {
  return
    "{\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) stone 0 0 0 1 1\n"
    "( " + std::to_string(maxX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) stone 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) stone 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) stone 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(minZ) + " ) stone 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) stone 0 0 0 1 1\n"
    "}\n";
}

std::string basicMap(std::string brush) {
  return
    "{\n"
    "\"classname\" \"worldspawn\"\n" +
    brush +
    "}\n"
    "{\n"
    "\"classname\" \"lg_spawn\"\n"
    "\"origin\" \"-2 0 1\"\n"
    "\"angle\" \"90\"\n"
    "}\n"
    "{\n"
    "\"classname\" \"lg_spawn\"\n"
    "\"origin\" \"2 0 1\"\n"
    "\"yaw\" \"180\"\n"
    "}\n";
}

} // namespace

int main() {
  int failures = 0;

  {
    constexpr std::string_view text = R"({
"classname" "worldspawn"
"message" "hello"
}
)";
    const lg::MapParseResult result = lg::parseMapDocument(text);
    failures += expect(result.ok, "parser should read one entity");
    failures += expect(result.document.entities.size() == 1, "parser should store entity");
    failures += expect(
      result.document.entities[0].property("classname") != nullptr &&
        *result.document.entities[0].property("classname") == "worldspawn",
      "parser should read key/value pairs"
    );
  }

  {
    const lg::MapParseResult result = lg::parseMapDocument(basicMap(cuboidBrush(-1, -1, 0, 1, 1, 1)));
    failures += expect(result.ok, "parser should read a worldspawn brush");
    failures += expect(result.document.entities[0].brushes.size() == 1, "parser should store brush");
    failures += expect(result.document.entities[0].brushes[0].faces.size() == 6, "parser should store brush faces");
  }

  {
    const lg::ArenaLoadResult result =
      lg::loadArenaFromMapText(basicMap(cuboidBrush(-1, -1, 0, 1, 1, 1)));
    failures += expect(result.ok, "cuboid map should convert");
    failures += expect(result.arena.wallCount == 1, "cuboid map should produce one wall");
    failures += expect(nearlyEqual(result.arena.walls[0].min.x, -0.025F), "wall min should use Quake-to-LG scale");
    failures += expect(nearlyEqual(result.arena.walls[0].max.z, 0.025F), "wall max should use Quake-to-LG scale");
    failures += expect(result.arena.walls[0].materialId != 0U, "wall material should be preserved");
  }

  {
    const std::string brush =
      "{\n"
      "( 0 0 0 ) ( 0 0 128 ) ( 0 128 128 ) stone 16 32 90 0.5 2\n"
      "( 128 0 0 ) ( 128 128 128 ) ( 128 0 128 ) stone 0 0 0 1 1\n"
      "( 0 0 0 ) ( 128 0 128 ) ( 0 0 128 ) stone 0 0 0 1 1\n"
      "( 0 128 0 ) ( 0 128 128 ) ( 128 128 128 ) stone 0 0 0 1 1\n"
      "( 0 0 0 ) ( 0 128 0 ) ( 128 128 0 ) stone 0 0 0 1 1\n"
      "( 0 0 128 ) ( 128 128 128 ) ( 0 128 128 ) stone 0 0 0 1 1\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(basicMap(brush));
    failures += expect(result.ok, "classic texture projection map should convert");
    const lg::TextureProjection& projection = result.arena.walls[0].faceTextureProjections[5];
    failures += expect(projection.valid, "cuboid wall face should preserve texture projection");
    failures += expect(
      nearlyEqual(projection.uOffset, 16.0F) && nearlyEqual(projection.vOffset, 32.0F),
      "texture projection should preserve offsets"
    );
    failures += expect(
      std::fabs(projection.uAxis.z) > 1.9F && std::fabs(projection.vAxis.y) > 0.49F,
      "texture projection should apply rotation and scale to axes"
    );
  }

  {
    const std::string brush =
      "{\n"
      "( 0 0 0 ) ( 0 0 128 ) ( 0 128 128 ) stone [ 1 0 0 0 ] [ 0 1 0 0 ] 0 1 1\n"
      "}\n";
    const lg::MapParseResult result = lg::parseMapDocument(basicMap(brush));
    failures += expect(!result.ok, "Valve 220 texture axes should be rejected until supported");
    failures += expect(
      result.error.find("Valve 220") != std::string::npos,
      "Valve 220 rejection should explain unsupported texture axes"
    );
  }

  {
    const lg::ArenaLoadResult result =
      lg::loadArenaFromMapText(basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)));
    failures += expect(result.ok, "sixteen-unit cuboid map should convert");
    failures += expect(
      nearlyEqual(result.arena.walls[0].max.z - result.arena.walls[0].min.z, 0.4F),
      "sixteen TrenchBroom units should convert to 0.4 LG units"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light\"\n"
      "\"origin\" \"0 0 160\"\n"
      "\"_color\" \"1 0.5 0.25\"\n"
      "\"light\" \"600\"\n"
      "\"radius\" \"400\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"light_point\"\n"
      "\"origin\" \"80 0 120\"\n"
      "\"_light\" \"0.25 0.5 1 300\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "map lights should convert");
    failures += expect(result.arena.staticLightCount == 2, "light entities should be stored in arena data");
    failures += expect(
      nearlyEqual(result.arena.staticLights[0].position.z, 4.0F) &&
        nearlyEqual(result.arena.staticLights[0].intensity, 2.0F) &&
        nearlyEqual(result.arena.staticLights[0].radius, 10.0F),
      "light origin, intensity, and radius should use importer scale"
    );
    failures += expect(
      nearlyEqual(result.arena.staticLights[0].color.y, 0.5F) &&
        nearlyEqual(result.arena.staticLights[1].color.z, 1.0F),
      "light colors should be preserved"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light\"\n"
      "\"origin\" \"0 0 160\"\n"
      "\"_color\" \"300 0 0\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(!result.ok, "invalid light color should be rejected");
  }

  {
    const std::string text =
      "{\n"
      "\"classname\" \"worldspawn\"\n"
      "\"lg_bounds_min\" \"-80 -80 -40\"\n"
      "\"lg_bounds_max\" \"80 80 80\"\n" +
      cuboidBrush(-16, -16, 0, 16, 16, 16) +
      "}\n"
      "{\n"
      "\"classname\" \"lg_spawn\"\n"
      "\"origin\" \"-40 0 40\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"lg_spawn\"\n"
      "\"origin\" \"40 0 40\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "map with explicit bounds should convert");
    failures += expect(
      nearlyEqual(result.arena.min.x, -2.0F) && nearlyEqual(result.arena.max.z, 2.0F),
      "explicit map bounds should use Quake-to-LG scale"
    );
  }

  {
    const std::string brush =
      "{\n"
      "( -1 -1 0 ) ( -1 1 0 ) ( -1 1 1.5 ) stone 0 0 0 1 1\n"
      "( 1 -1 0 ) ( 1 -1 0.5 ) ( 1 1 0.5 ) stone 0 0 0 1 1\n"
      "( -1 -1 0 ) ( 1 -1 0 ) ( 1 -1 0.5 ) stone 0 0 0 1 1\n"
      "( -1 1 0 ) ( -1 1 1.5 ) ( 1 1 0.5 ) stone 0 0 0 1 1\n"
      "( -1 -1 0 ) ( -1 1 0 ) ( 1 1 0 ) stone 0 0 0 1 1\n"
      "( -1 -1 1.5 ) ( 1 -1 0.5 ) ( 1 1 0.5 ) stone 0 0 0 1 1\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(basicMap(brush));
    if (!result.ok) {
      std::cerr << "non-axis brush error: " << result.error << '\n';
    }
    failures += expect(result.ok, "non-axis-aligned brush should convert");
    failures += expect(result.arena.wallCount == 0, "non-axis-aligned brush should not become a wall box");
    failures += expect(result.arena.brushCount == 1, "non-axis-aligned brush should produce one convex brush");
    failures += expect(
      nearlyEqual(result.arena.brushes[0].min.x, -0.025F) &&
        nearlyEqual(result.arena.brushes[0].max.x, 0.025F),
      "non-axis-aligned brush bounds should use Quake-to-LG scale"
    );
    failures += expect(
      result.arena.brushes[0].faces[0].vertexCount >= 3,
      "non-axis-aligned brush should keep renderable face polygons"
    );
  }

  {
    const lg::ArenaLoadResult result =
      lg::loadArenaFromMapText(basicMap(cuboidBrush(-1, -1, 0, 1, 1, 0)));
    failures += expect(!result.ok, "degenerate cuboid should be rejected");
  }

  {
    const lg::ArenaLoadResult result =
      lg::loadArenaFromMapText(basicMap(cuboidBrush(-1, -1, 0, 1, 1, 1)));
    failures += expect(result.ok, "spawn map should convert");
    failures += expect(
      nearlyEqual(result.arena.spawnPositions[0].x, -0.05F) &&
        nearlyEqual(result.arena.spawnPositions[1].x, 0.05F),
      "converter should parse two spawn origins"
    );
    failures += expect(
      nearlyEqual(result.arena.min.x, -1.05F) && nearlyEqual(result.arena.max.x, 1.05F),
      "converter should auto-compute bounds with padding"
    );
  }

  {
    lg::ArenaLoadResult result = lg::loadArenaFromFile("maps/dev_cuboids.map");
    if (!result.ok) {
      result = lg::loadArenaFromFile("../maps/dev_cuboids.map");
    }
    if (!result.ok) {
      result = lg::loadArenaFromFile("../../maps/dev_cuboids.map");
    }
    failures += expect(result.ok, "sample dev_cuboids.map should load");
  }

  return failures == 0 ? 0 : 1;
}
