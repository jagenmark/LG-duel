#include "map/MapParser.hpp"
#include "map/MapToArena.hpp"
#include "sim/Arena.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

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
  float maxZ,
  std::string_view material = "stone"
) {
  const std::string materialName(material);
  return
    "{\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) " + materialName + " 0 0 0 1 1\n"
    "( " + std::to_string(maxX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) " + materialName + " 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) " + materialName + " 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) " + materialName + " 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(minZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(minZ) + " ) " + materialName + " 0 0 0 1 1\n"
    "( " + std::to_string(minX) + " " + std::to_string(minY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(maxX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) "
    "( " + std::to_string(minX) + " " + std::to_string(maxY) + " " + std::to_string(maxZ) + " ) " + materialName + " 0 0 0 1 1\n"
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

std::string inwardWoundDodecagonalPrismBrush() {
  return
    "{\n"
    "( -136 -40 -416 ) ( -136 40 -432 ) ( -136 40 -416 ) stone 0 0 0 1 1\n"
    "( -100 -100 -416 ) ( -136 -40 -432 ) ( -136 -40 -416 ) stone 0 0 0 1 1\n"
    "( -100 100 -416 ) ( -136 40 -432 ) ( -100 100 -432 ) stone 0 0 0 1 1\n"
    "( -40 -136 -416 ) ( -100 -100 -432 ) ( -100 -100 -416 ) stone 0 0 0 1 1\n"
    "( -40 136 -416 ) ( -100 100 -432 ) ( -40 136 -432 ) stone 0 0 0 1 1\n"
    "( 40 -136 -416 ) ( -40 -136 -432 ) ( -40 -136 -416 ) stone 0 0 0 1 1\n"
    "( 136 40 -432 ) ( 100 -100 -432 ) ( 136 -40 -432 ) stone 0 0 0 1 1\n"
    "( 136 40 -416 ) ( 40 136 -416 ) ( 100 100 -416 ) stone 0 0 0 1 1\n"
    "( 40 136 -416 ) ( -40 136 -432 ) ( 40 136 -432 ) stone 0 0 0 1 1\n"
    "( 100 -100 -416 ) ( 40 -136 -432 ) ( 40 -136 -416 ) stone 0 0 0 1 1\n"
    "( 100 100 -416 ) ( 40 136 -432 ) ( 100 100 -432 ) stone 0 0 0 1 1\n"
    "( 136 -40 -416 ) ( 100 -100 -432 ) ( 100 -100 -416 ) stone 0 0 0 1 1\n"
    "( 136 40 -416 ) ( 100 100 -432 ) ( 136 40 -432 ) stone 0 0 0 1 1\n"
    "( 136 40 -416 ) ( 136 -40 -432 ) ( 136 -40 -416 ) stone 0 0 0 1 1\n"
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
    const lg::MapParseResult result =
      lg::parseMapDocument(basicMap(cuboidBrush(-1, -1, 0, 1, 1, 1, "common/playerclip")));
    failures += expect(result.ok, "parser should read playerclip brush materials");
    failures += expect(
      result.document.entities[0].brushes[0].faces[0].material == "common/playerclip",
      "parser should preserve playerclip material names"
    );
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
    const std::string text =
      "{\n"
      "\"classname\" \"worldspawn\"\n"
      "\"lg_bounds_min\" \"-120 -120 -40\"\n"
      "\"lg_bounds_max\" \"160 120 120\"\n" +
      cuboidBrush(-80, -80, -8, -48, 80, 0, "stone") +
      cuboidBrush(20, -80, 0, 60, 80, 80, "textures/common/playerclip") +
      "}\n"
      "{\n"
      "\"classname\" \"lg_spawn\"\n"
      "\"origin\" \"-80 0 40\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"lg_spawn\"\n"
      "\"origin\" \"120 0 40\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "playerclip and normal solid map should convert");
    failures += expect(result.arena.wallCount == 2, "playerclip cuboid should remain collision geometry");
    failures += expect(result.arena.walls[0].renderable, "normal textured brush should remain renderable");
    failures += expect(!result.arena.walls[1].renderable, "playerclip brush should be marked non-renderable");
    failures += expect(
      result.arena.walls[1].materialId == 0U &&
        result.arena.walls[1].faceMaterialIds[0] == 0U,
      "playerclip should not preserve render material output"
    );

    lg::PlayerState player;
    player.position = {0.0F, 0.0F, player.bounds.halfHeight};
    player.onGround = true;
    player.movementMode = lg::MovementMode::Grounded;
    const lg::CollisionResult collision = lg::resolvePlayerArenaCollision(
      result.arena,
      player,
      {1.0F, 0.0F, player.bounds.halfHeight},
      {8.0F, 0.0F, 0.0F}
    );
    failures += expect(
      collision.position.x < 1.0F && nearlyEqual(collision.velocity.x, 0.0F),
      "playerclip should block player arena collision like a normal solid"
    );
  }

  {
    const std::string text =
      "{\n"
      "\"classname\" \"worldspawn\"\n"
      "\"lg_bounds_min\" \"-120 -120 -40\"\n"
      "\"lg_bounds_max\" \"160 120 120\"\n" +
      cuboidBrush(-80, -80, -8, -48, 80, 0, "stone") +
      "}\n"
      "{\n"
      "\"classname\" \"func_group\"\n" +
      cuboidBrush(20, -80, 0, 60, 80, 80, "common/playerclip") +
      "}\n"
      "{\n"
      "\"classname\" \"lg_spawn\"\n"
      "\"origin\" \"-80 0 40\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"lg_spawn\"\n"
      "\"origin\" \"120 0 40\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "func_group playerclip map should convert");
    failures += expect(
      result.arena.wallCount == 2 && !result.arena.walls[1].renderable,
      "func_group playerclip brush should become non-renderable collision geometry"
    );
  }

  {
    const std::string mixedBrush =
      "{\n"
      "( 0 0 0 ) ( 0 0 16 ) ( 0 16 16 ) common/playerclip 0 0 0 1 1\n"
      "( 16 0 0 ) ( 16 16 16 ) ( 16 0 16 ) stone 0 0 0 1 1\n"
      "( 0 0 0 ) ( 16 0 16 ) ( 0 0 16 ) stone 0 0 0 1 1\n"
      "( 0 16 0 ) ( 0 16 16 ) ( 16 16 16 ) stone 0 0 0 1 1\n"
      "( 0 0 0 ) ( 0 16 0 ) ( 16 16 0 ) stone 0 0 0 1 1\n"
      "( 0 0 16 ) ( 16 16 16 ) ( 0 16 16 ) stone 0 0 0 1 1\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(basicMap(mixedBrush));
    failures += expect(!result.ok, "mixed playerclip brushes should be rejected");
    failures += expect(
      result.error.find("collision-only") != std::string::npos,
      "mixed playerclip rejection should explain the whole-brush rule"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-80, -80, -8, -48, 80, 0, "stone")) +
      "{\n"
      "\"classname\" \"func_group\"\n"
      "\"lg_source_entity_index\" \"7\"\n"
      "\"lg_source_brush_index\" \"42\"\n"
      "\"lg_collision_class\" \"weapclip\"\n" +
      cuboidBrush(20, -80, 0, 60, 80, 80, "common/weapclip") +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "weapclip provenance entity should convert");
    failures += expect(
      result.arena.wallCount == 2 &&
        !result.arena.walls[1].renderable &&
        result.arena.walls[1].collisionKind == lg::ArenaCollisionKind::WeaponClip &&
        result.arena.walls[1].sourceEntityIndex == 7U &&
        result.arena.walls[1].sourceBrushIndex == 42U,
      "weapclip should retain its diagnostic class and stable source locator"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-80, -80, -8, -48, 80, 0, "stone")) +
      "{\n"
      "\"classname\" \"func_group\"\n"
      "\"lg_source_entity_index\" \"7\"\n"
      "\"lg_collision_class\" \"weapclip\"\n" +
      cuboidBrush(20, -80, 0, 60, 80, 80, "common/weapclip") +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(
      !result.ok && result.error.find("requires both") != std::string::npos,
      "partial source brush provenance should fail instead of silently losing identity"
    );
  }

  {
    std::string text = basicMap(cuboidBrush(-80, -80, -8, -48, 80, 0, "stone"));
    const std::string worldClass = "\"classname\" \"worldspawn\"\n";
    text.insert(
      text.find(worldClass) + worldClass.size(),
      "\"lg_source_bsp_sha256\" "
      "\"0000000000000000000000000000000000000000000000000000000000000000\"\n"
      "\"lg_raw_decompile_sha256\" "
      "\"1111111111111111111111111111111111111111111111111111111111111111\"\n"
    );
    text +=
      "{\n"
      "\"classname\" \"func_group\"\n"
      "\"lg_collision_class\" \"visible_solid\"\n" +
      cuboidBrush(20, -20, 0, 40, 20, 40, "stone") +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(
      !result.ok && result.error.find("source-bound imported func_group") !=
        std::string::npos,
      "source-bound imports should reject func_group brushes with both locator fields missing"
    );
  }

  {
    std::string text = basicMap(cuboidBrush(-80, -80, -8, -48, 80, 0, "stone"));
    const std::string worldClass = "\"classname\" \"worldspawn\"\n";
    text.insert(
      text.find(worldClass) + worldClass.size(),
      "\"lg_source_bsp_sha256\" "
      "\"0000000000000000000000000000000000000000000000000000000000000000\"\n"
    );
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(
      !result.ok && result.error.find("requires both") != std::string::npos,
      "source-bound imports should require the BSP and raw-map hash pair"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-80, -80, -8, -48, 80, 0, "stone")) +
      "{\n"
      "\"classname\" \"func_group\"\n"
      "\"lg_source_entity_index\" \"7\"\n"
      "\"lg_source_brush_index\" \"42\"\n" +
      cuboidBrush(-20, -20, 0, 0, 20, 40, "stone") +
      "}\n"
      "{\n"
      "\"classname\" \"func_group\"\n"
      "\"lg_source_entity_index\" \"7\"\n"
      "\"lg_source_brush_index\" \"42\"\n" +
      cuboidBrush(20, -20, 0, 40, 20, 40, "stone") +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(
      !result.ok && result.error.find("duplicate imported source brush locator") !=
        std::string::npos,
      "duplicate source brush provenance should fail instead of making audit rules ambiguous"
    );
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
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light_sun\"\n"
      "\"direction\" \"0 0 -2\"\n"
      "\"color\" \"255 240 200\"\n"
      "\"intensity\" \"0.8\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "light_sun entity should convert");
    failures += expect(result.arena.sunLight.enabled, "light_sun should enable arena sun light");
    failures += expect(
      nearlyEqual(result.arena.sunLight.direction.z, -1.0F) &&
        nearlyEqual(result.arena.sunLight.intensity, 0.8F),
      "light_sun direction should be normalized and intensity preserved"
    );
    failures += expect(
      nearlyEqual(result.arena.sunLight.color.x, 1.0F) &&
        result.arena.sunLight.color.y > 0.93F &&
        result.arena.sunLight.color.z > 0.78F,
      "light_sun 0..255 color should normalize"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light_sun\"\n"
      "\"direction\" \"1 0 0\"\n"
      "\"_color\" \"0.25 0.5 1\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "light_sun should accept _color");
    failures += expect(
      nearlyEqual(result.arena.sunLight.color.x, 0.25F) &&
        nearlyEqual(result.arena.sunLight.color.y, 0.5F) &&
        nearlyEqual(result.arena.sunLight.color.z, 1.0F),
      "light_sun 0..1 color should be preserved"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light_sun\"\n"
      "\"direction\" \"0 0 0\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(!result.ok, "zero light_sun direction should be rejected");
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light_sun\"\n"
      "\"angle\" \"90\"\n"
      "\"pitch\" \"0\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "light_sun angle/pitch fallback should convert");
    failures += expect(
      nearlyEqual(result.arena.sunLight.direction.x, 0.0F) &&
        nearlyEqual(result.arena.sunLight.direction.y, 1.0F) &&
        nearlyEqual(result.arena.sunLight.direction.z, 0.0F),
      "light_sun angle/pitch fallback should derive direction"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"light_sun\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"light_sun\"\n"
      "\"direction\" \"0 0 -1\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(!result.ok, "multiple light_sun entities should be rejected");
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"item_health_small\"\n"
      "\"origin\" \"40 0 40\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"item_health_large\"\n"
      "\"origin\" \"80 0 40\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "health pickup map should convert");
    failures += expect(result.arena.healthPickupCount == 2, "health pickups should be stored in arena data");
    failures += expect(
      result.arena.healthPickups[0].type == lg::HealthPickupType::Small &&
        result.arena.healthPickups[1].type == lg::HealthPickupType::Large,
      "health pickup classnames should choose small and large types"
    );
    failures += expect(
      nearlyEqual(result.arena.healthPickups[0].position.x, 1.0F) &&
        nearlyEqual(result.arena.healthPickups[1].position.x, 2.0F),
      "health pickup origins should use Quake-to-LG scale"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"item_health_small\"\n"
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(!result.ok, "health pickup without origin should be rejected");
    failures += expect(
      result.error.find("line ") != std::string::npos &&
        result.error.find("origin") != std::string::npos,
      "invalid health pickup error should be line-numbered"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"target_position\"\n"
      "\"targetname\" \"jp_land\"\n"
      "\"origin\" \"80 0 120\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"trigger_jumppad\"\n"
      "\"target\" \"jp_land\"\n"
      "\"speed\" \"12\"\n" +
      cuboidBrush(-8, -8, 16, 8, 8, 24) +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "target-based jumppad map should convert");
    failures += expect(result.arena.jumpPadCount == 1, "jumppad trigger should be stored in arena data");
    failures += expect(result.arena.wallCount == 1, "jumppad trigger should not become a solid wall");
    failures += expect(result.arena.brushCount == 0, "jumppad trigger should not become a renderable brush");
    failures += expect(result.arena.jumpPads[0].hasTarget, "targeted jumppad should keep target launch mode");
    failures += expect(result.arena.jumpPads[0].hasTargetSpeed, "targeted jumppad should keep optional speed");
    failures += expect(
      nearlyEqual(result.arena.jumpPads[0].targetPosition.x, 2.0F) &&
        nearlyEqual(result.arena.jumpPads[0].targetPosition.z, 3.0F),
      "target_position origin should use Quake-to-LG scale"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"target_position\"\n"
      "\"targetname\" \"tele_exit\"\n"
      "\"origin\" \"400 800 120\"\n"
      "\"angle\" \"90\"\n"
      "}\n"
      "{\n"
      "\"classname\" \"trigger_teleport\"\n"
      "\"target\" \"tele_exit\"\n" +
      cuboidBrush(-8, -8, 16, 8, 8, 24) +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "target-based teleport map should convert");
    failures += expect(result.arena.teleportCount == 1, "teleport trigger should be stored in arena data");
    failures += expect(result.arena.wallCount == 1, "teleport trigger should not become solid geometry");
    failures += expect(
      nearlyEqual(result.arena.teleports[0].destination.x, 10.0F) &&
        nearlyEqual(result.arena.teleports[0].destination.y, 20.0F) &&
        nearlyEqual(result.arena.teleports[0].exitVelocity.x, 0.0F) &&
        nearlyEqual(result.arena.teleports[0].exitVelocity.y, 10.0F),
      "teleport destination and authored exit angle should use LG units"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"trigger_jumppad\"\n"
      "\"target\" \"missing_target\"\n" +
      cuboidBrush(-8, -8, 16, 8, 8, 24) +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(!result.ok, "jumppad with missing target should be rejected");
    failures += expect(
      result.error.find("line ") != std::string::npos &&
        result.error.find("missing_target") != std::string::npos,
      "missing jumppad target error should be line-numbered"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"trigger_jumppad\"\n"
      "\"speed\" \"fast\"\n"
      "\"direction\" \"0 0 1\"\n" +
      cuboidBrush(-8, -8, 16, 8, 8, 24) +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(!result.ok, "jumppad with invalid speed should be rejected");
    failures += expect(
      result.error.find("line ") != std::string::npos &&
        result.error.find("speed") != std::string::npos,
      "invalid jumppad speed error should be line-numbered"
    );
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n"
      "\"classname\" \"trigger_jumppad\"\n"
      "\"direction\" \"0 0 1\"\n"
      "\"speed\" \"10\"\n" +
      cuboidBrush(-8, -8, 16, 8, 8, 24) +
      "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "direction fallback jumppad should convert");
    failures += expect(result.arena.wallCount == 1, "trigger brush should not become solid geometry");
    failures += expect(result.arena.brushCount == 0, "trigger brush should not become rendered geometry");
    failures += expect(result.arena.jumpPadCount == 1, "trigger brush should become jumppad data");
    failures += expect(
      !result.arena.jumpPads[0].hasTarget &&
        nearlyEqual(result.arena.jumpPads[0].launchVelocity.z, 10.0F),
      "direction fallback should precompute launch velocity"
    );
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
      lg::loadArenaFromMapText(basicMap(inwardWoundDodecagonalPrismBrush()));
    if (!result.ok) {
      std::cerr << "inward-wound prism error: " << result.error << '\n';
    }
    failures += expect(result.ok, "inward-wound 14-face convex prism should convert");
    failures += expect(
      result.ok && result.arena.brushCount == 1,
      "inward-wound 14-face prism should produce one convex brush"
    );
    failures += expect(
      result.ok &&
        result.arena.brushes[0].faceCount == 14 &&
        result.arena.brushes[0].vertexCount == 24,
      "inward-wound 14-face prism should keep all faces and vertices"
    );
  }

  {
    std::string brushes;
    const std::string convexBrush = inwardWoundDodecagonalPrismBrush();
    brushes.reserve(convexBrush.size() * (lg::Arena::kBrushCount + 1U));
    for (std::size_t index = 0; index <= lg::Arena::kBrushCount; ++index) {
      brushes += convexBrush;
    }
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(basicMap(std::move(brushes)));
    failures += expect(!result.ok, "maps above the fixed convex-brush limit should fail");
    failures += expect(
      result.error.find("too many convex brushes") != std::string::npos,
      "convex-brush capacity failures should preserve the loader diagnostic"
    );
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
    std::string text = basicMap(cuboidBrush(-64, -64, -8, 64, 64, 0));
    for (std::size_t index = 2; index < lg::Arena::kSpawnCount; ++index) {
      text += "{\n\"classname\" \"lg_spawn\"\n\"origin\" \"" +
        std::to_string(static_cast<int>(index) - 16) + " 0 8\"\n}\n";
    }
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(
      result.ok && result.arena.spawnCount == lg::Arena::kSpawnCount,
      "map conversion should retain thirty-two authored spawns"
    );

    text += "{\n\"classname\" \"lg_spawn\"\n\"origin\" \"24 0 8\"\n}\n";
    const lg::ArenaLoadResult overflow = lg::loadArenaFromMapText(text);
    failures += expect(
      !overflow.ok && overflow.error.find("too many spawn points") != std::string::npos,
      "map conversion should reject a thirty-third authored spawn"
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

  {
    lg::ArenaLoadResult result = lg::loadArenaFromFile("maps/mcg.map");
    if (!result.ok) result = lg::loadArenaFromFile("../maps/mcg.map");
    if (!result.ok) result = lg::loadArenaFromFile("../../maps/mcg.map");
    failures += expect(result.ok && lg::hasValidMcGuffinLayout(result.arena),
      "mcg.map should ship with a complete playable McGuffin layout");
    failures += expect(
      result.ok && result.arena.teamSpawnCount >= 8,
      "mcg.map should provide multiple physical candidates for both bases"
    );
    if (result.ok) {
      for (std::size_t index = 0; index < result.arena.teamSpawnCount; ++index) {
        const lg::ArenaTeamSpawn& spawn = result.arena.teamSpawns[index];
        lg::PlayerState probe;
        probe.position = spawn.position;
        probe.position.z += probe.bounds.halfHeight;
        failures += expect(
          !lg::playerPositionSolid(result.arena, probe, probe.position) &&
            !lg::pointInsideMcGuffinBase(
              spawn.position, result.arena.mcguffin.redBase
            ) &&
            !lg::pointInsideMcGuffinBase(
              spawn.position, result.arena.mcguffin.blueBase
            ),
          "authored McGuffin spawn candidates should be non-solid and outside base triggers"
        );
      }
    }
  }

  {
    const std::string text =
      basicMap(cuboidBrush(-64, -64, -8, 64, 64, 0)) +
      "{\n\"classname\" \"info_mcguffin_spawn\"\n\"origin\" \"0 0 32\"\n}\n"
      "{\n\"classname\" \"info_player_team\"\n\"spawn_group\" \"red_base\"\n\"origin\" \"-32 0 16\"\n\"angle\" \"90\"\n}\n"
      "{\n\"classname\" \"info_player_team\"\n\"spawn_group\" \"blue_base\"\n\"origin\" \"32 0 16\"\n\"angle\" \"180\"\n}\n"
      "{\n\"classname\" \"trigger_mcguffin_base\"\n\"team\" \"red\"\n" +
      cuboidBrush(-64, -16, 0, -40, 16, 48, "common/trigger") + "}\n"
      "{\n\"classname\" \"trigger_mcguffin_base\"\n\"team\" \"blue\"\n" +
      cuboidBrush(40, -16, 0, 64, 16, 48, "common/trigger") + "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(text);
    failures += expect(result.ok, "valid McGuffin entities should convert");
    failures += expect(
      result.ok && lg::hasValidMcGuffinLayout(result.arena),
      "McGuffin layout should require neutral spawn, bases, and team spawns"
    );
    failures += expect(
      result.ok && result.arena.mcguffin.redBase.max.x < 0.0F &&
        result.arena.mcguffin.blueBase.min.x > 0.0F,
      "McGuffin base trigger bounds should use map scale"
    );
    failures += expect(
      result.ok && result.arena.teamSpawnCount == 2 &&
        result.arena.teamSpawns[0].group == lg::ArenaSpawnGroup::RedBase &&
        nearlyEqual(result.arena.teamSpawns[0].yawRadians, 1.5707963F) &&
        result.arena.teamSpawns[1].group == lg::ArenaSpawnGroup::BlueBase,
      "physical spawn groups and authored facing should survive map conversion"
    );
  }

  {
    const std::string invalidSpawnGroup =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n\"classname\" \"info_player_team\"\n\"spawn_group\" \"middle\"\n"
      "\"origin\" \"0 0 8\"\n}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(invalidSpawnGroup);
    failures += expect(
      !result.ok && result.error.find("spawn_group") != std::string::npos,
      "invalid physical spawn groups should be rejected clearly"
    );
  }

  {
    const std::string duplicate =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n\"classname\" \"info_mcguffin_spawn\"\n\"origin\" \"0 0 32\"\n}\n"
      "{\n\"classname\" \"info_mcguffin_spawn\"\n\"origin\" \"8 0 32\"\n}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(duplicate);
    failures += expect(!result.ok, "duplicate neutral McGuffin spawns should be rejected");
  }

  {
    const std::string invalidBase =
      basicMap(cuboidBrush(-16, -16, 0, 16, 16, 16)) +
      "{\n\"classname\" \"trigger_mcguffin_base\"\n\"team\" \"green\"\n" +
      cuboidBrush(-8, -8, 0, 8, 8, 16, "common/trigger") + "}\n";
    const lg::ArenaLoadResult result = lg::loadArenaFromMapText(invalidBase);
    failures += expect(
      !result.ok && result.error.find("team") != std::string::npos,
      "invalid McGuffin base teams should be rejected clearly"
    );
  }

  return failures == 0 ? 0 : 1;
}
