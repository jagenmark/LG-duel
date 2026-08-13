#include "render/Renderer.hpp"

#include "dev/PngWriter.hpp"
#include "app/TextInput.hpp"
#include "render/BitmapFont.hpp"
#include "render/GltfSkinnedModel.hpp"
#include "render/Scene3D.hpp"
#include "render/StaticAmbientProbe.hpp"
#include "render/Sky.hpp"
#include "render/ScreenUi.hpp"
#include "render/Perspective.hpp"
#include "render/WeaponPresentation.hpp"
#include "render/WorldVisibility.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#include "render/BitmapFontData.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lg {
namespace {

#if LG_DUEL_HAS_SDL3
[[nodiscard]] bool gpuBackendRequested() {
  const char* value = std::getenv("LG_DUEL_RENDER_BACKEND");
  if (value == nullptr) {
    return false;
  }

  const std::string_view requested = value;
  return requested == "gpu" ||
    requested == "sdl_gpu" ||
    requested == "vulkan";
}

[[nodiscard]] SDL_GPUDevice* createGpuDevice() {
  constexpr SDL_GPUShaderFormat shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV;
#if defined(NDEBUG)
  constexpr bool debugMode = false;
#else
  constexpr bool debugMode = true;
#endif

  const auto create = [=](const char* name) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0) {
      return static_cast<SDL_GPUDevice*>(nullptr);
    }
    SDL_SetBooleanProperty(
      properties,
      SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,
      (shaderFormats & SDL_GPU_SHADERFORMAT_SPIRV) != 0
    );
    SDL_SetBooleanProperty(
      properties,
      SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
      debugMode
    );
    // A requested GPU visual session must never select SwiftShader/Lavapipe.
    // The launcher still attests the concrete device after control startup.
    SDL_SetBooleanProperty(
      properties,
      SDL_PROP_GPU_DEVICE_CREATE_VULKAN_REQUIRE_HARDWARE_ACCELERATION_BOOLEAN,
      true
    );
    if (name != nullptr) {
      SDL_SetStringProperty(
        properties,
        SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,
        name
      );
    }
    SDL_GPUDevice* result = SDL_CreateGPUDeviceWithProperties(properties);
    SDL_DestroyProperties(properties);
    return result;
  };

  SDL_GPUDevice* device = create("vulkan");
  if (device == nullptr) {
    std::cerr << "SDL_GPU Vulkan initialization failed: " << SDL_GetError() << '\n';
  }
  return device;
}

[[nodiscard]] std::string environmentValue(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? value : "";
}

[[nodiscard]] bool looksLikeSoftwareRenderer(std::string value) {
  std::transform(
    value.begin(),
    value.end(),
    value.begin(),
    [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    }
  );
  return value.find("swiftshader") != std::string::npos ||
    value.find("llvmpipe") != std::string::npos ||
    value.find("lavapipe") != std::string::npos ||
    value.find("software") != std::string::npos;
}

constexpr std::size_t kMaxGpuVertices = 131072;
constexpr float kLegacyOutlineWorldUnitsPerPixel = 0.015F;
constexpr float kMaxVisualStepSmoothHeight = 1.0F;
constexpr float kVisualStepSmoothSpeed = 6.0F;

using RenderClock = std::chrono::steady_clock;

[[nodiscard]] float millisecondsBetween(
  RenderClock::time_point start,
  RenderClock::time_point end
) {
  return std::chrono::duration<float, std::milli>(end - start).count();
}

[[nodiscard]] std::uint64_t clockNanoseconds(
  RenderClock::time_point time
) {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      time.time_since_epoch()
    ).count()
  );
}

[[nodiscard]] std::uint64_t sampleLateView(
  LateViewSampler sampler,
  PlayerState& player,
  RendererFrameDiagnostics& diagnostics
) {
  diagnostics.lateMouseSampleEnabled = sampler.sample != nullptr;
  if (sampler.sample == nullptr) {
    return 0;
  }

  const auto sampleStart = RenderClock::now();
  const LateViewSample sample = sampler.sample(sampler.context);
  const auto sampleEnd = RenderClock::now();
  diagnostics.lateMouseSampleMilliseconds =
    millisecondsBetween(sampleStart, sampleEnd);
  diagnostics.mouseSamplePhaseGainMilliseconds =
    std::isfinite(sample.samplePhaseGainMilliseconds)
    ? std::max(0.0F, sample.samplePhaseGainMilliseconds)
    : 0.0F;
  if (
    sample.hasView &&
    std::isfinite(sample.yawRadians) &&
    std::isfinite(sample.pitchRadians)
  ) {
    player.viewYawRadians = sample.yawRadians;
    player.viewPitchRadians = sample.pitchRadians;
    diagnostics.lateMouseSampleApplied = true;
  }
  return sample.sampleCompletedNanoseconds != 0
    ? sample.sampleCompletedNanoseconds
    : clockNanoseconds(sampleEnd);
}

void recordMouseSampleToSubmit(
  std::uint64_t sampleCompletedNanoseconds,
  RendererFrameDiagnostics& diagnostics
) {
  if (sampleCompletedNanoseconds == 0) {
    return;
  }
  const std::uint64_t submitNanoseconds =
    clockNanoseconds(RenderClock::now());
  if (submitNanoseconds < sampleCompletedNanoseconds) {
    return;
  }
  diagnostics.mouseSampleToSubmitMilliseconds =
    static_cast<float>(submitNanoseconds - sampleCompletedNanoseconds) /
    1'000'000.0F;
}

[[nodiscard]] float secondsBetween(
  RenderClock::time_point start,
  RenderClock::time_point end
) {
  return std::chrono::duration<float>(end - start).count();
}

[[nodiscard]] std::string_view presentModeName(SDL_GPUPresentMode mode) {
  switch (mode) {
  case SDL_GPU_PRESENTMODE_IMMEDIATE:
    return "Immediate";
  case SDL_GPU_PRESENTMODE_MAILBOX:
    return "Mailbox";
  case SDL_GPU_PRESENTMODE_VSYNC:
    return "VSync";
  default:
    return "Unknown";
  }
}

[[nodiscard]] SDL_GPUPresentMode sdlGpuPresentMode(PresentMode mode) {
  switch (mode) {
  case PresentMode::Fifo:
    return SDL_GPU_PRESENTMODE_VSYNC;
  case PresentMode::Mailbox:
    return SDL_GPU_PRESENTMODE_MAILBOX;
  case PresentMode::Immediate:
    return SDL_GPU_PRESENTMODE_IMMEDIATE;
  }
  return SDL_GPU_PRESENTMODE_VSYNC;
}

[[nodiscard]] std::string_view presentModeName(PresentMode mode) {
  switch (mode) {
  case PresentMode::Fifo:
    return "FIFO/VSync";
  case PresentMode::Mailbox:
    return "Mailbox";
  case PresentMode::Immediate:
    return "Immediate";
  }
  return "Unknown";
}

struct GpuVertex {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  float u = 0.0F;
  float v = 0.0F;
  float normal[3] = {};
  std::uint32_t materialSlot = 0;
};

static_assert(sizeof(GpuVertex) == 40U);

struct GpuMaterialVertex {
  float position[3] = {};
  float normal[3] = {};
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  float metallic = 0.0F;
  float roughness = 1.0F;
};

static_assert(sizeof(GpuMaterialVertex) == 36);

constexpr Uint32 kFontAtlasWidth = 2048;
constexpr Uint32 kFontAtlasHeight = 2048;
constexpr float kBitmapGlyphSize = 8.0F;
constexpr std::array<float, 10> kUiFontPixelHeights = {
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
constexpr std::size_t kDefaultUiFontPixelHeightIndex = 2U;
constexpr float kSolidTextureU = 4.0F / static_cast<float>(kFontAtlasWidth);
constexpr float kSolidTextureV = 4.0F / static_cast<float>(kFontAtlasHeight);

struct FontGlyph {
  float u0 = kSolidTextureU;
  float v0 = kSolidTextureV;
  float u1 = kSolidTextureU;
  float v1 = kSolidTextureV;
  float xOffset = 0.0F;
  float yOffset = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  float advance = kBitmapGlyphSize;
  bool drawable = false;
};

struct FontAtlas {
  SDL_GPUTexture* texture = nullptr;
  std::unordered_map<std::uint32_t, FontGlyph> glyphs;
  std::string requestedFont;
  std::string loadedFont;
  float lineHeight = kBitmapGlyphSize;
  float baseScaleDenominator = kBitmapGlyphSize;
  float nominalPixelHeight = kBitmapGlyphSize;
  bool truetype = false;
};

struct FontAtlasSet {
  std::string requestedFont;
  std::array<FontAtlas*, kUiFontPixelHeights.size()> atlases = {};
};

struct OverlayDrawBatch {
  FontAtlas* fontAtlas = nullptr;
  Uint32 firstVertex = 0;
  Uint32 vertexCount = 0;
};

struct TextureAtlasEntry {
  float u0 = kSolidTextureU;
  float v0 = kSolidTextureV;
  float u1 = kSolidTextureU;
  float v1 = kSolidTextureV;
  float uScale = 1.0F;
  float vScale = 1.0F;
  int sourceWidth = 1;
  int sourceHeight = 1;
  std::string material;
};

struct TextureAtlas {
  SDL_GPUTexture* texture = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  std::unordered_map<std::uint32_t, TextureAtlasEntry> entries;
};

struct TextureMaterialFile {
  std::filesystem::path path;
  std::array<std::string, 2> aliases = {};
};

[[nodiscard]] std::size_t nearestUiFontPixelHeightIndex(float scale) {
  const float targetHeight = kBitmapGlyphSize * std::max(0.1F, scale);
  std::size_t nearestIndex = 0U;
  float nearestDistance = std::abs(targetHeight - kUiFontPixelHeights[0]);
  for (std::size_t index = 1U; index < kUiFontPixelHeights.size(); ++index) {
    const float distance = std::abs(targetHeight - kUiFontPixelHeights[index]);
    if (distance < nearestDistance) {
      nearestIndex = index;
      nearestDistance = distance;
    }
  }
  return nearestIndex;
}

[[nodiscard]] FontAtlas* fontAtlasForTextScale(
  FontAtlasSet& fontAtlasSet,
  float scale
) {
  FontAtlas* atlas =
    fontAtlasSet.atlases[nearestUiFontPixelHeightIndex(scale)];
  if (atlas != nullptr) {
    return atlas;
  }
  return fontAtlasSet.atlases[kDefaultUiFontPixelHeightIndex];
}

struct WorldTexture {
  SDL_GPUTexture* texture = nullptr;
  int width = 1;
  int height = 1;
  std::uint32_t mipLevels = 1;
  std::string material;
  WorldMaterialTraits traits = {};
  bool opaque = true;
  bool fallback = false;
};

struct StaticWorldBatch {
  std::uint32_t materialId = 0;
  Uint32 firstVertex = 0;
  Uint32 vertexCount = 0;
  WorldTexture* texture = nullptr;
  WorldMaterialTraits traits = {};
};

struct StaticWorldMesh {
  SDL_GPUBuffer* vertexBuffer = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  std::vector<WorldTexture> textures;
  std::vector<StaticWorldBatch> batches;
  std::vector<StaticWorldBatch> chunkBatches;
  std::vector<StaticWorldBatch> visibleBatches;
  WorldVisibility visibility;
  WorldVisibilityQueryScratch visibilityScratch;
  StaticAmbientProbeGrid ambientProbeGrid;
  StaticAmbientBakeStats ambientBakeStats = {};
  bool useCulledBatches = false;
  std::uint64_t arenaFingerprint = 0;
  std::uint32_t arenaRevision = 0;
  std::uint32_t sourceTriangles = 0;
  std::uint32_t duplicateTrianglesCulled = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t referencedMaterials = 0;
  std::uint32_t loadedTextures = 0;
  std::uint32_t missingTextures = 0;
  std::uint32_t buildCount = 0;
  int ambientQuality = 0;
  int ambientDebugMode = 0;
  bool opaqueVertices = true;
  int samplerTextureFilter = -1;
  int samplerTextureAnisotropy = -1;
  int samplerAppliedTextureAnisotropy = -1;
  float samplerTextureLodBias = 999.0F;
  float buildMilliseconds = 0.0F;
};

struct GpuStaticMesh {
  SDL_GPUBuffer* vertexBuffer = nullptr;
  Uint32 vertexCount = 0;
  MeshHandle handle = MeshHandle::Invalid;
  bool materialLit = false;
  bool opaque = true;
};

struct GpuBillboardMesh {
  SDL_GPUBuffer* vertexBuffer = nullptr;
  Uint32 vertexCount = 0;
  BillboardHandle handle = BillboardHandle::Invalid;
};

// Vertex stream 1 for instanced mesh/billboard shaders:
// location 3: instancePosition float3, offset 0
// location 4: instanceScale float3, offset 12
// location 5: instanceRotation float, offset 24
// location 6: instancePitch float, offset 28
// location 7: instanceColor ubyte4 normalized, offset 32
// location 8: instancePhase float, offset 36
struct GpuSimpleInstance {
  float position[3] = {};
  float scale[3] = {1.0F, 1.0F, 1.0F};
  float rotationRadians = 0.0F;
  float pitchRadians = 0.0F;
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  float visualPhase = 0.0F;
};

static_assert(sizeof(GpuSimpleInstance) == 40);

// Vertex stream 1 for static mesh instancing:
// locations 3-5: float4 model matrix rows (xyz basis plus translation)
// location 6: instance tint ubyte4 normalized
struct GpuStaticInstance {
  float row0[4] = {};
  float row1[4] = {};
  float row2[4] = {};
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  std::uint8_t ambientVisibility = 255;
  std::uint8_t ambientDebug = 0;
  std::uint8_t padding[2] = {};
};

static_assert(sizeof(GpuStaticInstance) == 56);

struct GpuModelVertex {
  float position[3] = {};
  float normal[3] = {0.0F, 0.0F, 1.0F};
  float texCoord[2] = {};
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  std::uint8_t tintWeight = 0;
  std::uint8_t albedoTextureMode = 0;
  std::uint8_t padding[2] = {};
  std::uint16_t joints[4] = {};
  float weights[4] = {};
};

static_assert(sizeof(GpuModelVertex) == 64);

struct GpuGltfPlayerInstance {
  float row0[4] = {};
  float row1[4] = {};
  float row2[4] = {};
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  std::uint32_t firstBone = 0;
  std::uint32_t boneCount = 0;
  std::uint32_t flags = 0;
  std::uint8_t ambientVisibility = 255;
  std::uint8_t ambientDebug = 0;
  std::uint8_t padding[6] = {};
};

static_assert(sizeof(GpuGltfPlayerInstance) == 72);

struct GpuGltfPrimitive {
  SDL_GPUBuffer* vertexBuffer = nullptr;
  SDL_GPUBuffer* indexBuffer = nullptr;
  Uint32 indexCount = 0;
  Uint32 vertexBytes = 0;
  Uint32 indexBytes = 0;
};

struct GpuGltfPlayerResources {
  std::string sourcePath;
  std::string materialDiagnostic;
  std::vector<GpuGltfPrimitive> primitives;
  SDL_GPUTexture* albedoTexture = nullptr;
  SDL_GPUTexture* packedMaskTexture = nullptr;
  SDL_GPUSampler* materialSampler = nullptr;
  SDL_GPUTexture* fallbackAlbedoTexture = nullptr;
  SDL_GPUTexture* fallbackPackedMaskTexture = nullptr;
  SDL_GPUSampler* fallbackMaterialSampler = nullptr;
  bool materialManifestHasAuthoredTextures = false;
  bool authoredMaterialTexturesReady = false;
  bool materialFallbackTexturesReady = false;
  std::uint32_t materialTextureMipLevels = 0;
  std::uint64_t materialTextureBytes = 0;
  SDL_GPUBuffer* instanceBuffer = nullptr;
  SDL_GPUTransferBuffer* instanceTransfer = nullptr;
  Uint32 instanceCapacity = 0;
  std::vector<GpuGltfPlayerInstance> instanceStaging;
  SDL_GPUBuffer* boneBuffer = nullptr;
  SDL_GPUTransferBuffer* boneTransfer = nullptr;
  Uint32 boneCapacityRows = 0;
  std::vector<std::array<float, 16>> boneStaging;
  std::uint32_t staticVertexBytes = 0;
  std::uint32_t staticIndexBytes = 0;
  bool opaque = true;
};

struct GpuInstanceBuffer {
  SDL_GPUBuffer* buffer = nullptr;
  SDL_GPUTransferBuffer* transfer = nullptr;
  Uint32 capacity = 0;
  std::vector<GpuSimpleInstance> staging;
};

struct GpuStaticInstanceBuffer {
  SDL_GPUBuffer* buffer = nullptr;
  SDL_GPUTransferBuffer* transfer = nullptr;
  Uint32 capacity = 0;
  std::vector<GpuStaticInstance> staging;
};

struct GpuSimpleResources {
  GpuInstanceBuffer instances;
  std::vector<GpuStaticMesh> projectileMeshes;
  std::vector<GpuBillboardMesh> projectileBillboards;
  std::vector<GpuStaticMesh> staticMeshes;
  GpuStaticInstanceBuffer staticInstances;
  SDL_GPUTexture* weaponEnvironment = nullptr;
  SDL_GPUSampler* weaponEnvironmentSampler = nullptr;
};

struct GpuSkyResources {
  SkyAssetLoadCache cache;
  std::array<SDL_GPUTexture*, 3> textures = {};
  SDL_GPUSampler* sampler = nullptr;
};

void destroyTextureAtlas(SDL_GPUDevice* device, TextureAtlas* atlas) {
  if (atlas == nullptr) {
    return;
  }
  if (atlas->sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, atlas->sampler);
  }
  if (atlas->texture != nullptr) {
    SDL_ReleaseGPUTexture(device, atlas->texture);
  }
  delete atlas;
}

void destroyStaticWorldMesh(SDL_GPUDevice* device, StaticWorldMesh* mesh) {
  if (mesh == nullptr) {
    return;
  }
  for (WorldTexture& texture : mesh->textures) {
    if (texture.texture != nullptr) {
      SDL_ReleaseGPUTexture(device, texture.texture);
    }
  }
  if (mesh->sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, mesh->sampler);
  }
  if (mesh->vertexBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, mesh->vertexBuffer);
  }
  delete mesh;
}

void destroyGpuInstanceBuffer(SDL_GPUDevice* device, GpuInstanceBuffer& buffer) {
  if (buffer.transfer != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, buffer.transfer);
    buffer.transfer = nullptr;
  }
  if (buffer.buffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, buffer.buffer);
    buffer.buffer = nullptr;
  }
  buffer.capacity = 0;
  buffer.staging.clear();
}

void destroyGpuStaticInstanceBuffer(
  SDL_GPUDevice* device,
  GpuStaticInstanceBuffer& buffer
) {
  if (buffer.transfer != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, buffer.transfer);
    buffer.transfer = nullptr;
  }
  if (buffer.buffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, buffer.buffer);
    buffer.buffer = nullptr;
  }
  buffer.capacity = 0;
  buffer.staging.clear();
}

void destroyGpuSimpleResources(SDL_GPUDevice* device, GpuSimpleResources* resources) {
  if (resources == nullptr) {
    return;
  }
  for (GpuStaticMesh& mesh : resources->projectileMeshes) {
    if (mesh.vertexBuffer != nullptr) {
      SDL_ReleaseGPUBuffer(device, mesh.vertexBuffer);
    }
  }
  for (GpuBillboardMesh& billboard : resources->projectileBillboards) {
    if (billboard.vertexBuffer != nullptr) {
      SDL_ReleaseGPUBuffer(device, billboard.vertexBuffer);
    }
  }
  for (GpuStaticMesh& mesh : resources->staticMeshes) {
    if (mesh.vertexBuffer != nullptr) {
      SDL_ReleaseGPUBuffer(device, mesh.vertexBuffer);
    }
  }
  if (resources->weaponEnvironmentSampler != nullptr) {
    SDL_ReleaseGPUSampler(device, resources->weaponEnvironmentSampler);
  }
  if (resources->weaponEnvironment != nullptr) {
    SDL_ReleaseGPUTexture(device, resources->weaponEnvironment);
  }
  destroyGpuInstanceBuffer(device, resources->instances);
  destroyGpuStaticInstanceBuffer(device, resources->staticInstances);
  delete resources;
}

void destroyGpuSkyResources(
  SDL_GPUDevice* device,
  GpuSkyResources* resources
) {
  if (resources == nullptr) {
    return;
  }
  for (SDL_GPUTexture* texture : resources->textures) {
    if (texture != nullptr) {
      SDL_ReleaseGPUTexture(device, texture);
    }
  }
  if (resources->sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, resources->sampler);
  }
  delete resources;
}

void destroyGpuGltfPlayerResources(
  SDL_GPUDevice* device,
  GpuGltfPlayerResources* resources
) {
  if (resources == nullptr) {
    return;
  }
  for (GpuGltfPrimitive& primitive : resources->primitives) {
    if (primitive.vertexBuffer != nullptr) {
      SDL_ReleaseGPUBuffer(device, primitive.vertexBuffer);
      primitive.vertexBuffer = nullptr;
    }
    if (primitive.indexBuffer != nullptr) {
      SDL_ReleaseGPUBuffer(device, primitive.indexBuffer);
      primitive.indexBuffer = nullptr;
    }
  }
  if (resources->materialSampler != nullptr) {
    SDL_ReleaseGPUSampler(device, resources->materialSampler);
    resources->materialSampler = nullptr;
  }
  if (resources->packedMaskTexture != nullptr) {
    SDL_ReleaseGPUTexture(device, resources->packedMaskTexture);
    resources->packedMaskTexture = nullptr;
  }
  if (resources->albedoTexture != nullptr) {
    SDL_ReleaseGPUTexture(device, resources->albedoTexture);
    resources->albedoTexture = nullptr;
  }
  if (resources->fallbackMaterialSampler != nullptr) {
    SDL_ReleaseGPUSampler(device, resources->fallbackMaterialSampler);
    resources->fallbackMaterialSampler = nullptr;
  }
  if (resources->fallbackPackedMaskTexture != nullptr) {
    SDL_ReleaseGPUTexture(device, resources->fallbackPackedMaskTexture);
    resources->fallbackPackedMaskTexture = nullptr;
  }
  if (resources->fallbackAlbedoTexture != nullptr) {
    SDL_ReleaseGPUTexture(device, resources->fallbackAlbedoTexture);
    resources->fallbackAlbedoTexture = nullptr;
  }
  if (resources->instanceTransfer != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, resources->instanceTransfer);
    resources->instanceTransfer = nullptr;
  }
  if (resources->instanceBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, resources->instanceBuffer);
    resources->instanceBuffer = nullptr;
  }
  if (resources->boneTransfer != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, resources->boneTransfer);
    resources->boneTransfer = nullptr;
  }
  if (resources->boneBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, resources->boneBuffer);
    resources->boneBuffer = nullptr;
  }
  delete resources;
}

[[nodiscard]] std::string shaderPath(std::string_view filename) {
  const char* basePath = SDL_GetBasePath();
  std::string path = basePath != nullptr ? basePath : "";
  path += "shaders/";
  path += filename;
  return path;
}

[[nodiscard]] std::string basePath() {
  const char* path = SDL_GetBasePath();
  return path != nullptr ? path : "";
}

[[nodiscard]] std::string_view gpuTextureFormatName(SDL_GPUTextureFormat format) {
  switch (format) {
  case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
    return "D32_FLOAT";
  case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
    return "D24_UNORM";
  case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
    return "D16_UNORM";
  case SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT:
    return "R11G11B10_UFLOAT";
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
    return "R16G16B16A16_FLOAT";
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
    return "B8G8R8A8_UNORM";
  default:
    return "unknown";
  }
}

[[nodiscard]] std::uint32_t gpuDepthFormatBits(SDL_GPUTextureFormat format) {
  switch (format) {
  case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
    return 32U;
  case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
    return 24U;
  case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
    return 16U;
  default:
    return 0U;
  }
}

[[nodiscard]] SDL_GPUTextureFormat chooseDepthStencilFormat(SDL_GPUDevice* device) {
  constexpr SDL_GPUTextureUsageFlags kRequiredUsage =
    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
    SDL_GPU_TEXTUREUSAGE_SAMPLER;
  const auto supports = [device](SDL_GPUTextureFormat format) {
    return SDL_GPUTextureSupportsFormat(
      device,
      format,
      SDL_GPU_TEXTURETYPE_2D,
      kRequiredUsage
    );
  };
  const SampledDepthFormatChoice choice = chooseSampledDepthFormat({
    supports(SDL_GPU_TEXTUREFORMAT_D32_FLOAT),
    supports(SDL_GPU_TEXTUREFORMAT_D24_UNORM),
    supports(SDL_GPU_TEXTUREFORMAT_D16_UNORM),
  });
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
  switch (choice) {
  case SampledDepthFormatChoice::D32:
    format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    break;
  case SampledDepthFormatChoice::D24:
    format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    break;
  case SampledDepthFormatChoice::D16:
    format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    break;
  default:
    break;
  }
  if (format != SDL_GPU_TEXTUREFORMAT_INVALID) {
    std::cerr << "SDL_GPU sampled depth format="
      << gpuTextureFormatName(format) << '\n';
    return format;
  }
  std::cerr << "SDL_GPU has no depth format that can also be sampled\n";
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

[[nodiscard]] SDL_GPUTextureFormat chooseLinearSceneColorFormat(
  SDL_GPUDevice* device,
  bool allowR11 = true
) {
  constexpr SDL_GPUTextureUsageFlags kUsage =
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  // Keep RGBA16F first. R11 has no stored alpha and remains a safe fallback
  // because scene blends use shader source alpha, never destination alpha.
  constexpr std::array<SDL_GPUTextureFormat, 4> kCandidates = {{
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
    SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
  }};
  for (SDL_GPUTextureFormat format : kCandidates) {
    if (
      (!allowR11 && format == SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT) ||
      !SDL_GPUTextureSupportsFormat(
          device,
          format,
          SDL_GPU_TEXTURETYPE_2D,
          kUsage
        )
    ) {
      continue;
    }
    // High uses 4x AA. Do not pick R11 if that would force High down to a
    // lower sample count; the wider formats keep their existing fallback.
    if (
      format == SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT &&
      !SDL_GPUTextureSupportsSampleCount(
        device,
        format,
        SDL_GPU_SAMPLECOUNT_4
      )
    ) {
      continue;
    }
    std::cerr << "SDL_GPU linear scene color format="
      << gpuTextureFormatName(format) << '\n';
    return format;
  }
  return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

[[nodiscard]] std::string normalizedMaterialPath(std::string material) {
  std::replace(material.begin(), material.end(), '\\', '/');
  while (!material.empty() && material.front() == '/') {
    material.erase(material.begin());
  }
  return material;
}

[[nodiscard]] bool textureDebugEnabled() {
  const char* value = std::getenv("LG_DUEL_TEXTURE_DEBUG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

[[nodiscard]] bool textureDebugCheckerEnabled() {
  const char* value = std::getenv("LG_DUEL_TEXTURE_DEBUG_UV");
  return value != nullptr && std::string_view(value) == "checker";
}

[[nodiscard]] std::uint32_t textureMipLevelCount(int width, int height) {
  if (width <= 0 || height <= 0) {
    return 1U;
  }
  return gltfTextureMipLevels(
    static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)
  );
}

[[nodiscard]] int normalizedTextureFilter(int filter) {
  return std::clamp(filter, 0, 2);
}

[[nodiscard]] int normalizedTextureAnisotropy(int anisotropy) {
  if (anisotropy <= 1) {
    return 1;
  }
  if (anisotropy <= 2) {
    return 2;
  }
  if (anisotropy <= 4) {
    return 4;
  }
  if (anisotropy <= 8) {
    return 8;
  }
  return 16;
}

[[nodiscard]] float normalizedTextureLodBias(float lodBias) {
  return std::clamp(lodBias, -2.0F, 4.0F);
}

[[nodiscard]] std::string_view textureFilterName(int filter) {
  switch (normalizedTextureFilter(filter)) {
  case 0:
    return "nearest";
  case 1:
    return "bilinear+mips";
  default:
    return "trilinear+mips";
  }
}

[[nodiscard]] std::int64_t quantizeStaticWorldFloat(float value) {
  return static_cast<std::int64_t>(std::llround(value * 10000.0F));
}

struct StaticWorldVertexKey {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t z = 0;
  std::int64_t u = 0;
  std::int64_t v = 0;
  std::int64_t normalX = 0;
  std::int64_t normalY = 0;
  std::int64_t normalZ = 0;
  std::uint32_t materialId = 0;
  std::uint32_t materialSlot = 0;
};

[[nodiscard]] bool operator==(const StaticWorldVertexKey& lhs, const StaticWorldVertexKey& rhs) {
  return lhs.x == rhs.x &&
    lhs.y == rhs.y &&
    lhs.z == rhs.z &&
    lhs.u == rhs.u &&
    lhs.v == rhs.v &&
    lhs.normalX == rhs.normalX &&
    lhs.normalY == rhs.normalY &&
    lhs.normalZ == rhs.normalZ &&
    lhs.materialId == rhs.materialId &&
    lhs.materialSlot == rhs.materialSlot;
}

[[nodiscard]] bool operator<(const StaticWorldVertexKey& lhs, const StaticWorldVertexKey& rhs) {
  if (lhs.x != rhs.x) {
    return lhs.x < rhs.x;
  }
  if (lhs.y != rhs.y) {
    return lhs.y < rhs.y;
  }
  if (lhs.z != rhs.z) {
    return lhs.z < rhs.z;
  }
  if (lhs.u != rhs.u) {
    return lhs.u < rhs.u;
  }
  if (lhs.v != rhs.v) {
    return lhs.v < rhs.v;
  }
  if (lhs.normalX != rhs.normalX) {
    return lhs.normalX < rhs.normalX;
  }
  if (lhs.normalY != rhs.normalY) {
    return lhs.normalY < rhs.normalY;
  }
  if (lhs.normalZ != rhs.normalZ) {
    return lhs.normalZ < rhs.normalZ;
  }
  if (lhs.materialId != rhs.materialId) {
    return lhs.materialId < rhs.materialId;
  }
  return lhs.materialSlot < rhs.materialSlot;
}

struct StaticWorldTriangleKey {
  std::array<StaticWorldVertexKey, 3> vertices = {};
};

[[nodiscard]] bool operator==(const StaticWorldTriangleKey& lhs, const StaticWorldTriangleKey& rhs) {
  return lhs.vertices == rhs.vertices;
}

struct StaticWorldTriangleKeyHash {
  [[nodiscard]] std::size_t operator()(const StaticWorldTriangleKey& key) const {
    std::size_t seed = 1469598103934665603ULL;
    const auto mix = [&seed](std::uint64_t value) {
      seed ^= value;
      seed *= 1099511628211ULL;
    };
    for (const StaticWorldVertexKey& vertex : key.vertices) {
      mix(static_cast<std::uint64_t>(vertex.x));
      mix(static_cast<std::uint64_t>(vertex.y));
      mix(static_cast<std::uint64_t>(vertex.z));
      mix(static_cast<std::uint64_t>(vertex.u));
      mix(static_cast<std::uint64_t>(vertex.v));
      mix(static_cast<std::uint64_t>(vertex.normalX));
      mix(static_cast<std::uint64_t>(vertex.normalY));
      mix(static_cast<std::uint64_t>(vertex.normalZ));
      mix(static_cast<std::uint64_t>(vertex.materialId));
      mix(static_cast<std::uint64_t>(vertex.materialSlot));
    }
    return seed;
  }
};

[[nodiscard]] StaticWorldVertexKey staticWorldVertexKey(const Vertex3D& vertex) {
  return {
    quantizeStaticWorldFloat(vertex.position.x),
    quantizeStaticWorldFloat(vertex.position.y),
    quantizeStaticWorldFloat(vertex.position.z),
    quantizeStaticWorldFloat(vertex.u),
    quantizeStaticWorldFloat(vertex.v),
    quantizeStaticWorldFloat(vertex.normal.x),
    quantizeStaticWorldFloat(vertex.normal.y),
    quantizeStaticWorldFloat(vertex.normal.z),
    vertex.materialId,
    vertex.materialSlot
  };
}

[[nodiscard]] StaticWorldTriangleKey staticWorldTriangleKey(
  const Vertex3D& a,
  const Vertex3D& b,
  const Vertex3D& c
) {
  StaticWorldTriangleKey key = {{{
    staticWorldVertexKey(a),
    staticWorldVertexKey(b),
    staticWorldVertexKey(c)
  }}};
  std::sort(key.vertices.begin(), key.vertices.end());
  return key;
}

[[nodiscard]] std::uint32_t cullDuplicateStaticWorldTriangles(Scene3D& scene) {
  const std::size_t triangleVertexCount = scene.vertices.size() - (scene.vertices.size() % 3U);
  if (triangleVertexCount < 6U) {
    return 0;
  }

  std::unordered_set<StaticWorldTriangleKey, StaticWorldTriangleKeyHash> seen;
  seen.reserve(triangleVertexCount / 3U);
  std::vector<Vertex3D> deduplicated;
  deduplicated.reserve(scene.vertices.size());

  std::uint32_t duplicateTriangles = 0;
  for (std::size_t index = 0; index + 2U < triangleVertexCount; index += 3U) {
    const StaticWorldTriangleKey key = staticWorldTriangleKey(
      scene.vertices[index],
      scene.vertices[index + 1U],
      scene.vertices[index + 2U]
    );
    if (!seen.insert(key).second) {
      ++duplicateTriangles;
      continue;
    }
    deduplicated.push_back(scene.vertices[index]);
    deduplicated.push_back(scene.vertices[index + 1U]);
    deduplicated.push_back(scene.vertices[index + 2U]);
  }

  for (std::size_t index = triangleVertexCount; index < scene.vertices.size(); ++index) {
    deduplicated.push_back(scene.vertices[index]);
  }

  if (duplicateTriangles > 0U) {
    scene.vertices = std::move(deduplicated);
  }
  return duplicateTriangles;
}

[[nodiscard]] std::uint32_t forcedTextureMaterialId() {
  const char* value = std::getenv("LG_DUEL_TEXTURE_DEBUG_FORCE_MATERIAL");
  if (value == nullptr || value[0] == '\0') {
    return 0U;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end != nullptr && *end == '\0') {
    return static_cast<std::uint32_t>(parsed);
  }
  return arenaMaterialId(value);
}

void collectTextureMaterialFiles(
  const std::filesystem::path& textureDirectory,
  std::vector<TextureMaterialFile>& materials
) {
  if (!std::filesystem::is_directory(textureDirectory)) {
    return;
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(textureDirectory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".png") {
      continue;
    }
    std::filesystem::path relative =
      std::filesystem::relative(entry.path(), textureDirectory);
    const std::string withExtension = normalizedMaterialPath(relative.generic_string());
    relative.replace_extension();
    materials.push_back({
      entry.path(),
      {withExtension, normalizedMaterialPath(relative.generic_string())}
    });
  }
}

[[nodiscard]] std::uint64_t hashCombine(std::uint64_t hash, std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  return hash;
}

[[nodiscard]] std::uint64_t arenaStaticWorldFingerprint(const Arena& arena) {
  std::uint64_t hash = 1469598103934665603ULL;
  hash = hashCombine(hash, arena.wallCount);
  hash = hashCombine(hash, arena.brushCount);
  hash = hashCombine(hash, arena.visualWallCount);
  hash = hashCombine(hash, arena.visualBrushCount);
  hash = hashCombine(hash, visualAmbientOccluderFingerprint(arena));
  hash = hashCombine(hash, ambientProbeInputFingerprint(arena));
  hash = hashCombine(hash, arenaSkySurfaceFingerprint(arena));
  hash = hashCombine(hash, arena.staticLightCount);
  hash = hashCombine(hash, arena.renderDefaultFloor ? 1U : 0U);
  const auto hashFloat = [](float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<std::uint64_t>(bits);
  };
  const auto hashTextureProjection = [&hashFloat](std::uint64_t projectionHash,
                                                  const TextureProjection& projection) {
    projectionHash = hashCombine(projectionHash, projection.valid ? 1U : 0U);
    projectionHash = hashCombine(projectionHash, hashFloat(projection.uAxis.x));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.uAxis.y));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.uAxis.z));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.vAxis.x));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.vAxis.y));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.vAxis.z));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.uOffset));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.vOffset));
    projectionHash = hashCombine(projectionHash, hashFloat(projection.uScale));
    return hashCombine(projectionHash, hashFloat(projection.vScale));
  };
  hash = hashCombine(hash, hashFloat(arena.min.x));
  hash = hashCombine(hash, hashFloat(arena.min.y));
  hash = hashCombine(hash, hashFloat(arena.min.z));
  hash = hashCombine(hash, hashFloat(arena.max.x));
  hash = hashCombine(hash, hashFloat(arena.max.y));
  hash = hashCombine(hash, hashFloat(arena.max.z));
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    hash = hashCombine(hash, wall.renderable ? 1U : 0U);
    hash = hashCombine(hash, static_cast<std::uint8_t>(wall.collisionKind));
    hash = hashCombine(hash, hashFloat(wall.min.x));
    hash = hashCombine(hash, hashFloat(wall.min.y));
    hash = hashCombine(hash, hashFloat(wall.min.z));
    hash = hashCombine(hash, hashFloat(wall.max.x));
    hash = hashCombine(hash, hashFloat(wall.max.y));
    hash = hashCombine(hash, hashFloat(wall.max.z));
    hash = hashCombine(hash, wall.materialId);
    for (std::size_t faceIndex = 0; faceIndex < wall.faceMaterialIds.size(); ++faceIndex) {
      hash = hashCombine(hash, wall.faceMaterialIds[faceIndex]);
      hash = hashCombine(
        hash,
        static_cast<std::uint8_t>(wall.faceSurfaceKinds[faceIndex])
      );
      const TextureProjection& projection = wall.faceTextureProjections[faceIndex];
      hash = hashCombine(hash, projection.valid ? 1U : 0U);
      hash = hashCombine(hash, hashFloat(projection.uAxis.x));
      hash = hashCombine(hash, hashFloat(projection.uAxis.y));
      hash = hashCombine(hash, hashFloat(projection.uAxis.z));
      hash = hashCombine(hash, hashFloat(projection.vAxis.x));
      hash = hashCombine(hash, hashFloat(projection.vAxis.y));
      hash = hashCombine(hash, hashFloat(projection.vAxis.z));
      hash = hashCombine(hash, hashFloat(projection.uOffset));
      hash = hashCombine(hash, hashFloat(projection.vOffset));
      hash = hashCombine(hash, hashFloat(projection.uScale));
      hash = hashCombine(hash, hashFloat(projection.vScale));
    }
  }
  for (std::size_t brushIndex = 0; brushIndex < arena.brushCount; ++brushIndex) {
    const ArenaBrush& brush = arena.brushes[brushIndex];
    hash = hashCombine(hash, brush.renderable ? 1U : 0U);
    hash = hashCombine(hash, static_cast<std::uint8_t>(brush.collisionKind));
    hash = hashCombine(hash, brush.faceCount);
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      hash = hashCombine(hash, face.materialId);
      hash = hashCombine(hash, static_cast<std::uint8_t>(face.surfaceKind));
      hash = hashCombine(hash, hashFloat(face.normal.x));
      hash = hashCombine(hash, hashFloat(face.normal.y));
      hash = hashCombine(hash, hashFloat(face.normal.z));
      hash = hashCombine(hash, hashFloat(face.distance));
      hash = hashCombine(hash, face.vertexCount);
      for (std::uint8_t vertexIndex = 0; vertexIndex < face.vertexCount; ++vertexIndex) {
        const Vec3 vertex = brush.vertices[face.vertices[vertexIndex]];
        hash = hashCombine(hash, hashFloat(vertex.x));
        hash = hashCombine(hash, hashFloat(vertex.y));
        hash = hashCombine(hash, hashFloat(vertex.z));
      }
      hash = hashTextureProjection(hash, face.textureProjection);
    }
  }
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    const ArenaWall& wall = arena.visualWalls[index];
    hash = hashCombine(hash, hashFloat(wall.min.x));
    hash = hashCombine(hash, hashFloat(wall.min.y));
    hash = hashCombine(hash, hashFloat(wall.min.z));
    hash = hashCombine(hash, hashFloat(wall.max.x));
    hash = hashCombine(hash, hashFloat(wall.max.y));
    hash = hashCombine(hash, hashFloat(wall.max.z));
    hash = hashCombine(hash, wall.materialId);
    for (std::size_t faceIndex = 0; faceIndex < wall.faceMaterialIds.size(); ++faceIndex) {
      hash = hashCombine(hash, wall.faceMaterialIds[faceIndex]);
      hash = hashCombine(
        hash,
        static_cast<std::uint8_t>(wall.faceSurfaceKinds[faceIndex])
      );
      const TextureProjection& projection = wall.faceTextureProjections[faceIndex];
      hash = hashCombine(hash, projection.valid ? 1U : 0U);
      hash = hashCombine(hash, hashFloat(projection.uAxis.x));
      hash = hashCombine(hash, hashFloat(projection.uAxis.y));
      hash = hashCombine(hash, hashFloat(projection.uAxis.z));
      hash = hashCombine(hash, hashFloat(projection.vAxis.x));
      hash = hashCombine(hash, hashFloat(projection.vAxis.y));
      hash = hashCombine(hash, hashFloat(projection.vAxis.z));
      hash = hashCombine(hash, hashFloat(projection.uOffset));
      hash = hashCombine(hash, hashFloat(projection.vOffset));
      hash = hashCombine(hash, hashFloat(projection.uScale));
      hash = hashCombine(hash, hashFloat(projection.vScale));
    }
  }
  for (std::size_t brushIndex = 0; brushIndex < arena.visualBrushCount; ++brushIndex) {
    const ArenaBrush& brush = arena.visualBrushes[brushIndex];
    hash = hashCombine(hash, brush.faceCount);
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      hash = hashCombine(hash, face.materialId);
      hash = hashCombine(hash, static_cast<std::uint8_t>(face.surfaceKind));
      hash = hashCombine(hash, hashFloat(face.normal.x));
      hash = hashCombine(hash, hashFloat(face.normal.y));
      hash = hashCombine(hash, hashFloat(face.normal.z));
      hash = hashCombine(hash, hashFloat(face.distance));
      hash = hashCombine(hash, face.vertexCount);
      for (std::uint8_t vertexIndex = 0; vertexIndex < face.vertexCount; ++vertexIndex) {
        const Vec3 vertex = brush.vertices[face.vertices[vertexIndex]];
        hash = hashCombine(hash, hashFloat(vertex.x));
        hash = hashCombine(hash, hashFloat(vertex.y));
        hash = hashCombine(hash, hashFloat(vertex.z));
      }
      hash = hashTextureProjection(hash, face.textureProjection);
    }
  }
  for (std::size_t index = 0; index < arena.staticLightCount; ++index) {
    const ArenaStaticLight& light = arena.staticLights[index];
    hash = hashCombine(hash, hashFloat(light.position.x));
    hash = hashCombine(hash, hashFloat(light.position.y));
    hash = hashCombine(hash, hashFloat(light.position.z));
    hash = hashCombine(hash, hashFloat(light.color.x));
    hash = hashCombine(hash, hashFloat(light.color.y));
    hash = hashCombine(hash, hashFloat(light.color.z));
    hash = hashCombine(hash, hashFloat(light.intensity));
    hash = hashCombine(hash, hashFloat(light.radius));
    hash = hashCombine(hash, hashFloat(light.sourceRadius));
    hash = hashCombine(
      hash,
      static_cast<std::uint64_t>(
        static_cast<std::int64_t>(light.priority) + 1000
      )
    );
    hash = hashCombine(hash, light.castsShadows ? 1U : 0U);
    hash = hashCombine(hash, light.flickerEnabled ? 1U : 0U);
    hash = hashCombine(hash, light.flickerSeed);
    hash = hashCombine(hash, hashFloat(light.flickerFrequencyHz));
    hash = hashCombine(hash, hashFloat(light.flickerMinFactor));
    hash = hashCombine(hash, hashFloat(light.flickerMaxFactor));
  }
  hash = hashCombine(hash, hashFloat(arena.ambientLight.color.x));
  hash = hashCombine(hash, hashFloat(arena.ambientLight.color.y));
  hash = hashCombine(hash, hashFloat(arena.ambientLight.color.z));
  hash = hashCombine(hash, hashFloat(arena.ambientLight.intensity));
  hash = hashCombine(hash, arena.sunLight.enabled ? 1U : 0U);
  hash = hashCombine(hash, hashFloat(arena.sunLight.direction.x));
  hash = hashCombine(hash, hashFloat(arena.sunLight.direction.y));
  hash = hashCombine(hash, hashFloat(arena.sunLight.direction.z));
  hash = hashCombine(hash, hashFloat(arena.sunLight.color.x));
  hash = hashCombine(hash, hashFloat(arena.sunLight.color.y));
  hash = hashCombine(hash, hashFloat(arena.sunLight.color.z));
  hash = hashCombine(hash, hashFloat(arena.sunLight.intensity));
  return hash;
}

[[nodiscard]] std::uint64_t pointShadowCacheFingerprint(
  std::uint64_t staticWorldFingerprint,
  std::span<const LivePointLight> lights,
  std::uint32_t textureSize
) {
  std::uint64_t hash = staticWorldFingerprint;
  const auto hashFloat = [](float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<std::uint64_t>(bits);
  };
  hash = hashCombine(hash, textureSize);
  hash = hashCombine(hash, lights.size());
  for (const LivePointLight& light : lights) {
    hash = hashCombine(hash, light.sourceIndex);
    hash = hashCombine(hash, hashFloat(light.position.x));
    hash = hashCombine(hash, hashFloat(light.position.y));
    hash = hashCombine(hash, hashFloat(light.position.z));
    hash = hashCombine(hash, hashFloat(light.radius));
    hash = hashCombine(hash, hashFloat(light.sourceRadius));
    hash = hashCombine(
      hash,
      static_cast<std::uint64_t>(
        static_cast<std::int64_t>(light.priority) + 1000
      )
    );
  }
  return hash;
}

[[nodiscard]] std::uint64_t sunShadowCacheFingerprint(
  std::uint64_t staticWorldFingerprint,
  const SunShadowProjection& projection
) {
  const auto hashFloat = [](float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<std::uint64_t>(bits);
  };
  std::uint64_t hash = staticWorldFingerprint;
  for (const Vec3 value : {
         projection.origin,
         projection.right,
         projection.up,
         projection.forward,
       }) {
    hash = hashCombine(hash, hashFloat(value.x));
    hash = hashCombine(hash, hashFloat(value.y));
    hash = hashCombine(hash, hashFloat(value.z));
  }
  hash = hashCombine(hash, hashFloat(projection.halfExtent));
  hash = hashCombine(hash, hashFloat(projection.farPlane));
  hash = hashCombine(hash, projection.mapSize);
  hash = hashCombine(hash, hashFloat(projection.normalBias));
  hash = hashCombine(hash, hashFloat(projection.depthBias));
  return hash;
}

[[nodiscard]] SDL_GPUTexture* uploadRgbaTexture(
  SDL_GPUDevice* device,
  const std::uint8_t* pixels,
  int width,
  int height,
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
) {
  if (
    width <= 0 || height <= 0 ||
    !SDL_GPUTextureSupportsFormat(
      device,
      format,
      SDL_GPU_TEXTURETYPE_2D,
      SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
    )
  ) {
    return nullptr;
  }
  const std::uint32_t mipLevels = textureMipLevelCount(width, height);
  const SDL_GPUTextureCreateInfo textureInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    format,
    SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
    static_cast<Uint32>(width),
    static_cast<Uint32>(height),
    1,
    mipLevels,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    static_cast<Uint32>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U),
    0,
  };
  SDL_GPUTransferBuffer* transferBuffer =
    SDL_CreateGPUTransferBuffer(device, &transferInfo);
  if (texture == nullptr || transferBuffer == nullptr) {
    if (transferBuffer != nullptr) {
      SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }
    if (texture != nullptr) {
      SDL_ReleaseGPUTexture(device, texture);
    }
    return nullptr;
  }
  void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  const std::size_t byteCount =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  std::memcpy(mapped, pixels, byteCount);
  SDL_UnmapGPUTransferBuffer(device, transferBuffer);

  SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
    ? SDL_BeginGPUCopyPass(commandBuffer)
    : nullptr;
  if (copyPass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  const SDL_GPUTextureTransferInfo source = {
    transferBuffer,
    0,
    static_cast<Uint32>(width),
    static_cast<Uint32>(height),
  };
  const SDL_GPUTextureRegion destination = {
    texture,
    0,
    0,
    0,
    0,
    0,
    static_cast<Uint32>(width),
    static_cast<Uint32>(height),
    1,
  };
  SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
  SDL_EndGPUCopyPass(copyPass);
  if (mipLevels > 1U) {
    SDL_GenerateMipmapsForGPUTexture(commandBuffer, texture);
  }
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
  if (!submitted) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  return texture;
}

[[nodiscard]] Vec3 cubemapDirection(std::size_t face, float u, float v) {
  switch (face) {
  case 0: return normalize(Vec3{1.0F, -v, -u});
  case 1: return normalize(Vec3{-1.0F, -v, u});
  case 2: return normalize(Vec3{u, 1.0F, v});
  case 3: return normalize(Vec3{u, -1.0F, -v});
  case 4: return normalize(Vec3{u, -v, 1.0F});
  default: return normalize(Vec3{-u, -v, -1.0F});
  }
}

[[nodiscard]] std::array<float, 3> studioCubemapColor(
  std::size_t face,
  float u,
  float v
) {
  const Vec3 direction = cubemapDirection(face, u, v);
  const float skyBlend = std::clamp((direction.z + 0.25F) / 0.90F, 0.0F, 1.0F);
  std::array<float, 3> color = {{
    0.055F + (0.62F - 0.055F) * skyBlend,
    0.070F + (0.74F - 0.070F) * skyBlend,
    0.095F + (0.92F - 0.095F) * skyBlend,
  }};
  const auto addCard = [&](float strength, float warmth) {
    color[0] += strength * (0.92F + warmth * 0.16F);
    color[1] += strength * (0.97F + warmth * 0.04F);
    color[2] += strength * (1.00F - warmth * 0.18F);
  };
  if (face < 4U) {
    const float centerCard =
      std::max(0.0F, 1.0F - std::fabs(u) / 0.24F) *
      std::max(0.0F, 1.0F - std::fabs(v) / 0.82F);
    addCard(centerCard * 1.35F, face >= 2U ? 0.45F : 0.0F);
    const float sideCard =
      std::max(0.0F, 1.0F - std::fabs(u + 0.68F) / 0.12F) *
      std::max(0.0F, 1.0F - std::fabs(v - 0.12F) / 0.52F);
    addCard(sideCard * 0.75F, 0.8F);
  } else if (face == 4U) {
    const float overhead =
      std::max(0.0F, 1.0F - std::fabs(v) / 0.32F) *
      std::max(0.0F, 1.0F - std::fabs(u) / 0.86F);
    addCard(overhead * 1.6F, 0.0F);
  }
  return color;
}

[[nodiscard]] bool createWeaponEnvironment(
  SDL_GPUDevice* device,
  SDL_GPUTexture*& texture,
  SDL_GPUSampler*& sampler
) {
  constexpr Uint32 kFaceSize = 64U;
  constexpr Uint32 kFaceCount = 6U;
  constexpr Uint32 kMipLevels = 7U;
  constexpr std::size_t kFaceBytes = kFaceSize * kFaceSize * 4U;
  std::array<std::uint8_t, kFaceBytes * kFaceCount> pixels = {};
  for (std::size_t face = 0; face < kFaceCount; ++face) {
    for (Uint32 y = 0; y < kFaceSize; ++y) {
      for (Uint32 x = 0; x < kFaceSize; ++x) {
        const float u = ((static_cast<float>(x) + 0.5F) / kFaceSize) * 2.0F - 1.0F;
        const float v = ((static_cast<float>(y) + 0.5F) / kFaceSize) * 2.0F - 1.0F;
        const auto color = studioCubemapColor(face, u, v);
        const std::size_t offset = face * kFaceBytes +
          (static_cast<std::size_t>(y) * kFaceSize + x) * 4U;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          pixels[offset + channel] = static_cast<std::uint8_t>(
            std::clamp(color[channel], 0.0F, 1.0F) * 255.0F
          );
        }
        pixels[offset + 3U] = 255U;
      }
    }
  }

  const SDL_GPUTextureCreateInfo textureInfo = {
    SDL_GPU_TEXTURETYPE_CUBE,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
    kFaceSize, kFaceSize, kFaceCount, kMipLevels,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  texture = SDL_CreateGPUTexture(device, &textureInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    static_cast<Uint32>(pixels.size()),
    0,
  };
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
  void* mapped = transfer != nullptr
    ? SDL_MapGPUTransferBuffer(device, transfer, false)
    : nullptr;
  if (texture == nullptr || mapped == nullptr) {
    if (transfer != nullptr) SDL_ReleaseGPUTransferBuffer(device, transfer);
    if (texture != nullptr) SDL_ReleaseGPUTexture(device, texture);
    texture = nullptr;
    return false;
  }
  std::memcpy(mapped, pixels.data(), pixels.size());
  SDL_UnmapGPUTransferBuffer(device, transfer);
  SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
    ? SDL_BeginGPUCopyPass(commandBuffer)
    : nullptr;
  if (copyPass == nullptr) {
    if (commandBuffer != nullptr) (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    texture = nullptr;
    return false;
  }
  for (Uint32 face = 0; face < kFaceCount; ++face) {
    const SDL_GPUTextureTransferInfo source = {
      transfer, face * static_cast<Uint32>(kFaceBytes), kFaceSize, kFaceSize,
    };
    const SDL_GPUTextureRegion destination = {
      texture, 0, face, 0, 0, 0, kFaceSize, kFaceSize, 1,
    };
    SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
  }
  SDL_EndGPUCopyPass(copyPass);
  SDL_GenerateMipmapsForGPUTexture(commandBuffer, texture);
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  if (!submitted) {
    SDL_ReleaseGPUTexture(device, texture);
    texture = nullptr;
    return false;
  }
  const SDL_GPUSamplerCreateInfo samplerInfo = {
    SDL_GPU_FILTER_LINEAR,
    SDL_GPU_FILTER_LINEAR,
    SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    0.0F, static_cast<float>(kMipLevels - 1U), SDL_GPU_COMPAREOP_ALWAYS,
    0.0F, static_cast<float>(kMipLevels - 1U), false, false, 0, 0, 0,
  };
  sampler = SDL_CreateGPUSampler(device, &samplerInfo);
  if (sampler == nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
    texture = nullptr;
    return false;
  }
  return true;
}

[[nodiscard]] std::string_view skyAssetName(SkyId sky) {
  switch (sky) {
  case SkyId::Aurora: return "aurora";
  case SkyId::CrimsonSunset: return "crimson-sunset";
  case SkyId::None: break;
  }
  return {};
}

[[nodiscard]] SDL_GPUTexture* loadSkyCubemap(
  SDL_GPUDevice* device,
  SkyId sky
) {
  constexpr Uint32 kFaceSize = 512U;
  constexpr Uint32 kFaceCount = 6U;
  constexpr std::size_t kFaceBytes =
    static_cast<std::size_t>(kFaceSize) * kFaceSize * 4U;
  constexpr std::array<std::string_view, kFaceCount> kFaceNames = {{
    "posx.png", "negx.png", "posy.png",
    "negy.png", "posz.png", "negz.png",
  }};
  const std::string_view assetName = skyAssetName(sky);
  if (assetName.empty()) {
    return nullptr;
  }

  std::vector<std::uint8_t> pixels(kFaceBytes * kFaceCount);
  for (std::size_t face = 0; face < kFaceNames.size(); ++face) {
    const std::filesystem::path path =
      std::filesystem::path(basePath()) /
      "sky" /
      assetName /
      kFaceNames[face];
    SDL_Surface* loaded = SDL_LoadPNG(path.string().c_str());
    if (loaded == nullptr) {
      return nullptr;
    }
    SDL_Surface* converted =
      SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (
      converted == nullptr ||
      converted->w != static_cast<int>(kFaceSize) ||
      converted->h != static_cast<int>(kFaceSize)
    ) {
      if (converted != nullptr) {
        SDL_DestroySurface(converted);
      }
      return nullptr;
    }
    const auto* source =
      static_cast<const std::uint8_t*>(converted->pixels);
    auto* destination = pixels.data() + face * kFaceBytes;
    for (Uint32 y = 0; y < kFaceSize; ++y) {
      std::memcpy(
        destination + static_cast<std::size_t>(y) * kFaceSize * 4U,
        source + static_cast<std::size_t>(y) * converted->pitch,
        static_cast<std::size_t>(kFaceSize) * 4U
      );
    }
    SDL_DestroySurface(converted);
  }

  const SDL_GPUTextureCreateInfo textureInfo = {
    SDL_GPU_TEXTURETYPE_CUBE,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    SDL_GPU_TEXTUREUSAGE_SAMPLER,
    kFaceSize,
    kFaceSize,
    kFaceCount,
    1U,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    static_cast<Uint32>(pixels.size()),
    0,
  };
  SDL_GPUTransferBuffer* transfer =
    SDL_CreateGPUTransferBuffer(device, &transferInfo);
  void* mapped = transfer != nullptr
    ? SDL_MapGPUTransferBuffer(device, transfer, false)
    : nullptr;
  if (texture == nullptr || mapped == nullptr) {
    if (transfer != nullptr) {
      SDL_ReleaseGPUTransferBuffer(device, transfer);
    }
    if (texture != nullptr) {
      SDL_ReleaseGPUTexture(device, texture);
    }
    return nullptr;
  }
  std::memcpy(mapped, pixels.data(), pixels.size());
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer* commandBuffer =
    SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
    ? SDL_BeginGPUCopyPass(commandBuffer)
    : nullptr;
  if (copyPass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  for (Uint32 face = 0; face < kFaceCount; ++face) {
    const SDL_GPUTextureTransferInfo source = {
      transfer,
      face * static_cast<Uint32>(kFaceBytes),
      kFaceSize,
      kFaceSize,
    };
    const SDL_GPUTextureRegion destination = {
      texture,
      0,
      face,
      0,
      0,
      0,
      kFaceSize,
      kFaceSize,
      1,
    };
    SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
  }
  SDL_EndGPUCopyPass(copyPass);
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  if (!submitted) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  return texture;
}

[[nodiscard]] SDL_GPUSampler* createSkySampler(SDL_GPUDevice* device) {
  const SDL_GPUSamplerCreateInfo samplerInfo = {
    SDL_GPU_FILTER_LINEAR,
    SDL_GPU_FILTER_LINEAR,
    SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    0.0F,
    1.0F,
    SDL_GPU_COMPAREOP_ALWAYS,
    0.0F,
    0.0F,
    false,
    false,
    0,
    0,
    0,
  };
  return SDL_CreateGPUSampler(device, &samplerInfo);
}

void ensureSkyLoaded(
  SDL_GPUDevice* device,
  GpuSkyResources& resources,
  SkyId sky
) {
  if (!resources.cache.shouldAttempt(sky)) {
    return;
  }
  SDL_GPUTexture* texture = loadSkyCubemap(device, sky);
  if (texture != nullptr && resources.sampler == nullptr) {
    resources.sampler = createSkySampler(device);
  }
  const bool loaded = texture != nullptr && resources.sampler != nullptr;
  if (!loaded && texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
    texture = nullptr;
  }
  if (loaded) {
    resources.textures[static_cast<std::size_t>(sky)] = texture;
  } else {
    std::cerr
      << "SDL_GPU sky '" << skyAssetName(sky)
      << "' failed to load; keeping the existing clear colour\n";
  }
  resources.cache.record(sky, loaded);
}

[[nodiscard]] WorldTexture createFallbackWorldTexture(
  SDL_GPUDevice* device,
  bool missingMaterial
) {
  constexpr int kSize = 64;
  std::array<std::uint8_t, kSize * kSize * 4> pixels = {};
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const bool bright = ((x / 8) + (y / 8)) % 2 == 0;
      const std::size_t offset = (static_cast<std::size_t>(y) * kSize + x) * 4U;
      pixels[offset + 0U] = missingMaterial ? (bright ? 255U : 24U) : 255U;
      pixels[offset + 1U] = missingMaterial ? (bright ? 0U : 24U) : 255U;
      pixels[offset + 2U] = missingMaterial ? (bright ? 255U : 24U) : 255U;
      pixels[offset + 3U] = 255U;
    }
  }
  return {
    uploadRgbaTexture(device, pixels.data(), kSize, kSize),
    kSize,
    kSize,
    textureMipLevelCount(kSize, kSize),
    missingMaterial ? "__missing_world_texture" : "__white_world_texture",
    {},
    true,
    true,
  };
}

[[nodiscard]] WorldTexture loadWorldTexture(
  SDL_GPUDevice* device,
  const TextureMaterialFile& material
) {
  SDL_Surface* loaded = SDL_LoadPNG(material.path.string().c_str());
  if (loaded == nullptr) {
    return {};
  }
  SDL_Surface* converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  if (converted == nullptr) {
    return {};
  }
  bool opaque = true;
  const auto* pixels = static_cast<const std::uint8_t*>(converted->pixels);
  for (int y = 0; y < converted->h && opaque; ++y) {
    const std::uint8_t* row =
      pixels + static_cast<std::size_t>(y) * converted->pitch;
    for (int x = 0; x < converted->w; ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 255U) {
        opaque = false;
        break;
      }
    }
  }
  WorldTexture texture = {
    uploadRgbaTexture(
      device,
      static_cast<const std::uint8_t*>(converted->pixels),
      converted->w,
      converted->h
    ),
    converted->w,
    converted->h,
    textureMipLevelCount(converted->w, converted->h),
    material.aliases[0],
    classifyWorldMaterial(material.aliases[0]),
    opaque,
    false,
  };
  SDL_DestroySurface(converted);
  return texture;
}

[[nodiscard]] SDL_GPUSampler* createWorldSampler(
  SDL_GPUDevice* device,
  int requestedFilter,
  int requestedAnisotropy,
  float requestedLodBias,
  int& appliedFilter,
  int& appliedAnisotropy
) {
  appliedFilter = normalizedTextureFilter(requestedFilter);
  const int normalizedAnisotropy =
    normalizedTextureAnisotropy(requestedAnisotropy);
  const float normalizedLodBias = normalizedTextureLodBias(requestedLodBias);
  const bool nearest = appliedFilter == 0;
  const bool trilinear = appliedFilter == 2;
  SDL_GPUSamplerCreateInfo samplerInfo = {
    nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR,
    nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR,
    trilinear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
    SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    nearest ? 0.0F : normalizedLodBias,
    static_cast<float>(normalizedAnisotropy),
    SDL_GPU_COMPAREOP_ALWAYS,
    0.0F,
    1000.0F,
    !nearest && normalizedAnisotropy > 1,
    false,
    0,
    0,
    0,
  };

  SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &samplerInfo);
  appliedAnisotropy = samplerInfo.enable_anisotropy ? normalizedAnisotropy : 1;
  if (sampler == nullptr && samplerInfo.enable_anisotropy) {
    const std::string anisotropyError = SDL_GetError();
    samplerInfo.enable_anisotropy = false;
    samplerInfo.max_anisotropy = 1.0F;
    sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    appliedAnisotropy = 1;
    std::cerr
      << "SDL_GPU world texture anisotropy " << normalizedAnisotropy
      << " unsupported; disabled anisotropy"
      << (anisotropyError.empty() ? "" : ": ")
      << anisotropyError << '\n';
  }
  if (sampler != nullptr) {
    std::cerr
      << "SDL_GPU world texture sampler filter="
      << textureFilterName(appliedFilter)
      << " anisotropy=" << appliedAnisotropy
      << " lodBias=" << (nearest ? 0.0F : normalizedLodBias)
      << '\n';
  }
  return sampler;
}

[[nodiscard]] bool updateStaticWorldSampler(
  SDL_GPUDevice* device,
  StaticWorldMesh* mesh,
  const RenderSettings& settings
) {
  if (mesh == nullptr) {
    return false;
  }
  const int requestedFilter = normalizedTextureFilter(settings.textureFilter);
  const int requestedAnisotropy =
    normalizedTextureAnisotropy(settings.textureAnisotropy);
  const float requestedLodBias =
    normalizedTextureLodBias(settings.textureLodBias);
  if (
    mesh->sampler != nullptr &&
    mesh->samplerTextureFilter == requestedFilter &&
    mesh->samplerTextureAnisotropy == requestedAnisotropy &&
    mesh->samplerTextureLodBias == requestedLodBias
  ) {
    return true;
  }

  int appliedFilter = requestedFilter;
  int appliedAnisotropy = requestedAnisotropy;
  SDL_GPUSampler* sampler = createWorldSampler(
    device,
    requestedFilter,
    requestedAnisotropy,
    requestedLodBias,
    appliedFilter,
    appliedAnisotropy
  );
  if (sampler == nullptr) {
    std::cerr
      << "SDL_GPU world texture sampler creation failed: "
      << SDL_GetError()
      << '\n';
    return false;
  }
  if (mesh->sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, mesh->sampler);
  }
  mesh->sampler = sampler;
  mesh->samplerTextureFilter = appliedFilter;
  mesh->samplerTextureAnisotropy = requestedAnisotropy;
  mesh->samplerAppliedTextureAnisotropy = appliedAnisotropy;
  mesh->samplerTextureLodBias = requestedLodBias;
  if (appliedAnisotropy != requestedAnisotropy) {
    std::cerr
      << "SDL_GPU world texture anisotropy requested="
      << requestedAnisotropy
      << " applied=" << appliedAnisotropy
      << '\n';
  }
  return true;
}

[[nodiscard]] SDL_GPUShader* loadGpuShader(
  SDL_GPUDevice* device,
  std::string_view filename,
  SDL_GPUShaderStage stage,
  Uint32 samplerCount = 0,
  Uint32 uniformBufferCount = 0,
  Uint32 storageBufferCount = 0
) {
  const std::string path = shaderPath(filename);
  std::size_t codeSize = 0;
  void* code = SDL_LoadFile(path.c_str(), &codeSize);
  if (code == nullptr) {
    return nullptr;
  }

  SDL_GPUShaderCreateInfo createInfo = {};
  createInfo.code_size = codeSize;
  createInfo.code = static_cast<const Uint8*>(code);
  createInfo.entrypoint = "main";
  createInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
  createInfo.stage = stage;
  createInfo.num_samplers = samplerCount;
  createInfo.num_storage_buffers = storageBufferCount;
  createInfo.num_uniform_buffers = uniformBufferCount;
  SDL_GPUShader* shader = SDL_CreateGPUShader(device, &createInfo);
  SDL_free(code);
  return shader;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuSkyPipeline(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat colorFormat,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUSampleCount sampleCount,
  bool directPresent
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "sky.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    directPresent ? "sky_direct.frag.spv" : "sky.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = colorFormat;
  colorTarget.blend_state.enable_blend = false;
  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = sampleCount;
  createInfo.depth_stencil_state.enable_depth_test = false;
  createInfo.depth_stencil_state.enable_depth_write = false;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;
  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipeline(
  SDL_GPUDevice* device,
  SDL_Window* window
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "color2d.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "color2d.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const SDL_GPUVertexBufferDescription vertexBufferDescription = {
    0,
    sizeof(GpuVertex),
    SDL_GPU_VERTEXINPUTRATE_VERTEX,
    0,
  };
  const std::array<SDL_GPUVertexAttribute, 5> vertexAttributes = {{
    {
      0,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuVertex, x),
    },
    {
      1,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
      offsetof(GpuVertex, red),
    },
    {
      2,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
      offsetof(GpuVertex, u),
    },
    {
      3,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuVertex, normal),
    },
    {
      4,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_UINT,
      offsetof(GpuVertex, materialSlot),
    },
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);
  colorTarget.blend_state.src_color_blendfactor =
    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTarget.blend_state.dst_color_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colorTarget.blend_state.dst_alpha_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.enable_blend = true;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    &vertexBufferDescription;
  createInfo.vertex_input_state.num_vertex_buffers = 1;
  createInfo.vertex_input_state.vertex_attributes = vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipeline3D(
  SDL_GPUDevice* device,
  SDL_Window* window,
  bool depthWrite,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUCompareOp depthCompare = SDL_GPU_COMPAREOP_LESS,
  const char* fragmentShaderPath = "world3d.frag.spv",
  Uint32 fragmentSamplerCount = 1,
  SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1,
  SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID,
  Uint32 fragmentUniformBufferCount = 1,
  bool depthOnly = false,
  const char* vertexShaderPath = "world3d.vert.spv",
  bool enableColorBlend = true
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    vertexShaderPath,
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    depthOnly ? "sun_shadow.frag.spv" : fragmentShaderPath,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    depthOnly ? 0U : fragmentSamplerCount,
    depthOnly ? 0U : fragmentUniformBufferCount
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const SDL_GPUVertexBufferDescription vertexBufferDescription = {
    0,
    sizeof(GpuVertex),
    SDL_GPU_VERTEXINPUTRATE_VERTEX,
    0,
  };
  const std::array<SDL_GPUVertexAttribute, 5> vertexAttributes = {{
    {
      0,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuVertex, x),
    },
    {
      1,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
      offsetof(GpuVertex, red),
    },
    {
      2,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
      offsetof(GpuVertex, u),
    },
    {
      3,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuVertex, normal),
    },
    {
      4,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_UINT,
      offsetof(GpuVertex, materialSlot),
    },
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID
    ? SDL_GetGPUSwapchainTextureFormat(device, window)
    : colorFormat;
  colorTarget.blend_state.src_color_blendfactor =
    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTarget.blend_state.dst_color_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colorTarget.blend_state.dst_alpha_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.enable_blend = enableColorBlend;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    &vertexBufferDescription;
  createInfo.vertex_input_state.num_vertex_buffers = 1;
  createInfo.vertex_input_state.vertex_attributes = vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = sampleCount;
  createInfo.depth_stencil_state.compare_op = depthCompare;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = depthWrite;
  createInfo.target_info.color_target_descriptions =
    depthOnly ? nullptr : &colorTarget;
  createInfo.target_info.num_color_targets = depthOnly ? 0U : 1U;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipelineOutlineMask(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_mask_world.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "outline_mask.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    0,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const SDL_GPUVertexBufferDescription vertexBufferDescription = {
    0,
    sizeof(GpuVertex),
    SDL_GPU_VERTEXINPUTRATE_VERTEX,
    0,
  };
  const std::array<SDL_GPUVertexAttribute, 1> vertexAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, x)},
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    &vertexBufferDescription;
  createInfo.vertex_input_state.num_vertex_buffers = 1;
  createInfo.vertex_input_state.vertex_attributes = vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = false;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipelineOutlineClear(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_clear.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "outline_clear.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = true;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipelineOutlineColorClear(
  SDL_GPUDevice* device
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_composite.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "outline_clear.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipelineOutlineDilation(
  SDL_GPUDevice* device,
  const char* fragmentShaderFile = "outline_dilate.frag.spv"
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_composite.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    fragmentShaderFile,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    1,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPipelineOutlineComposite(
  SDL_GPUDevice* device,
  SDL_Window* window,
  const char* fragmentShaderFile = "outline_composite.frag.spv"
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_composite.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    fragmentShaderFile,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    2,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);
  colorTarget.blend_state.src_color_blendfactor =
    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTarget.blend_state.dst_color_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colorTarget.blend_state.dst_alpha_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.enable_blend = true;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPostProcessPipeline(
  SDL_GPUDevice* device,
  const char* fragmentShaderFile,
  Uint32 samplerCount,
  SDL_GPUTextureFormat colorFormat
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_composite.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    fragmentShaderFile,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    samplerCount,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = colorFormat;
  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuInstancedPipeline3D(
  SDL_GPUDevice* device,
  SDL_Window* window,
  std::string_view vertexShaderName,
  std::string_view fragmentShaderName,
  bool depthWrite,
  bool additiveBlend,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1,
  SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID,
  bool hasDepthTarget = true,
  bool depthOnly = false,
  FragmentResourceLayout fragmentLayout = instancedColorFragmentLayout()
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    vertexShaderName,
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    depthOnly ? "sun_shadow.frag.spv" : fragmentShaderName,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    depthOnly ? 0U : fragmentLayout.samplers,
    depthOnly ? 0U : fragmentLayout.uniformBuffers
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const std::array<SDL_GPUVertexBufferDescription, 2> vertexBufferDescriptions = {{
    {
      0,
      sizeof(GpuVertex),
      SDL_GPU_VERTEXINPUTRATE_VERTEX,
      0,
    },
    {
      1,
      sizeof(GpuSimpleInstance),
      SDL_GPU_VERTEXINPUTRATE_INSTANCE,
      0,
    },
  }};
  const std::array<SDL_GPUVertexAttribute, 10> vertexAttributes = {{
    {
      0,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuVertex, x),
    },
    {
      1,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
      offsetof(GpuVertex, red),
    },
    {
      2,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
      offsetof(GpuVertex, u),
    },
    {
      3,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuSimpleInstance, position),
    },
    {
      4,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuSimpleInstance, scale),
    },
    {
      5,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
      offsetof(GpuSimpleInstance, rotationRadians),
    },
    {
      6,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
      offsetof(GpuSimpleInstance, pitchRadians),
    },
    {
      7,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
      offsetof(GpuSimpleInstance, red),
    },
    {
      8,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
      offsetof(GpuSimpleInstance, visualPhase),
    },
    {
      9,
      0,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
      offsetof(GpuVertex, normal),
    },
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID
    ? SDL_GetGPUSwapchainTextureFormat(device, window)
    : colorFormat;
  if (additiveBlend) {
    colorTarget.blend_state.src_color_blendfactor =
      SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.enable_blend = true;
  } else {
    colorTarget.blend_state.enable_blend = false;
  }

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    vertexBufferDescriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers =
    static_cast<Uint32>(vertexBufferDescriptions.size());
  createInfo.vertex_input_state.vertex_attributes = vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = sampleCount;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = hasDepthTarget;
  createInfo.depth_stencil_state.enable_depth_write =
    hasDepthTarget && depthWrite;
  createInfo.target_info.color_target_descriptions =
    depthOnly ? nullptr : &colorTarget;
  createInfo.target_info.num_color_targets = depthOnly ? 0U : 1U;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = hasDepthTarget;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuStaticMeshPipeline3D(
  SDL_GPUDevice* device,
  SDL_Window* window,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1,
  SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID,
  bool depthOnly = false,
  const char* fragmentShaderPath = "instanced_color.frag.spv",
  FragmentResourceLayout fragmentLayout = instancedColorFragmentLayout(),
  bool enableColorBlend = false
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "static_mesh_instance.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    depthOnly ? "sun_shadow.frag.spv" : fragmentShaderPath,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    depthOnly ? 0U : fragmentLayout.samplers,
    depthOnly ? 0U : fragmentLayout.uniformBuffers
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const std::array<SDL_GPUVertexBufferDescription, 2> vertexBufferDescriptions = {{
    {0, sizeof(GpuVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, sizeof(GpuStaticInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexAttribute, 9> vertexAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, x)},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuVertex, red)},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, u)},
    {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row0)},
    {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row1)},
    {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row2)},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuStaticInstance, red)},
    {7, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, normal)},
    {8, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuStaticInstance, ambientVisibility)},
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID
    ? SDL_GetGPUSwapchainTextureFormat(device, window)
    : colorFormat;
  colorTarget.blend_state.src_color_blendfactor =
    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTarget.blend_state.dst_color_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colorTarget.blend_state.dst_alpha_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.enable_blend = enableColorBlend && !depthOnly;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    vertexBufferDescriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers =
    static_cast<Uint32>(vertexBufferDescriptions.size());
  createInfo.vertex_input_state.vertex_attributes = vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = sampleCount;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = true;
  createInfo.target_info.color_target_descriptions =
    depthOnly ? nullptr : &colorTarget;
  createInfo.target_info.num_color_targets = depthOnly ? 0U : 1U;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuMaterialMeshPipeline3D(
  SDL_GPUDevice* device,
  SDL_Window* window,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1,
  SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID,
  bool depthOnly = false,
  const char* fragmentShaderPath = "material_weapon.frag.spv",
  FragmentResourceLayout fragmentLayout = {3U, 1U},
  bool enableColorBlend = false
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device, "material_mesh_instance.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    depthOnly ? "sun_shadow.frag.spv" : fragmentShaderPath,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    depthOnly ? 0U : fragmentLayout.samplers,
    depthOnly ? 0U : fragmentLayout.uniformBuffers
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }
  const std::array<SDL_GPUVertexBufferDescription, 2> descriptions = {{
    {0, sizeof(GpuMaterialVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, sizeof(GpuStaticInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexAttribute, 9> attributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMaterialVertex, position)},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMaterialVertex, normal)},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuMaterialVertex, red)},
    {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuMaterialVertex, metallic)},
    {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row0)},
    {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row1)},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row2)},
    {7, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuStaticInstance, red)},
    {8, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuStaticInstance, ambientVisibility)},
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID
    ? SDL_GetGPUSwapchainTextureFormat(device, window)
    : colorFormat;
  colorTarget.blend_state.src_color_blendfactor =
    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTarget.blend_state.dst_color_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colorTarget.blend_state.dst_alpha_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.enable_blend = enableColorBlend && !depthOnly;
  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions = descriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(descriptions.size());
  createInfo.vertex_input_state.vertex_attributes = attributes.data();
  createInfo.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = sampleCount;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = true;
  createInfo.target_info.color_target_descriptions =
    depthOnly ? nullptr : &colorTarget;
  createInfo.target_info.num_color_targets = depthOnly ? 0U : 1U;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;
  SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuStaticMeshOutlineMaskPipeline(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat,
  bool materialLayout
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "outline_mask_static.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "outline_mask.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    0,
    1
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const std::array<SDL_GPUVertexBufferDescription, 2> vertexBufferDescriptions = {{
    {
      0,
      static_cast<Uint32>(
        materialLayout ? sizeof(GpuMaterialVertex) : sizeof(GpuVertex)
      ),
      SDL_GPU_VERTEXINPUTRATE_VERTEX,
      0,
    },
    {1, sizeof(GpuStaticInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexAttribute, 4> vertexAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, x)},
    {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row0)},
    {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row1)},
    {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row2)},
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    vertexBufferDescriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers =
    static_cast<Uint32>(vertexBufferDescriptions.size());
  createInfo.vertex_input_state.vertex_attributes = vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = false;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuGltfPlayerModelPipeline(
  SDL_GPUDevice* device,
  SDL_Window* window,
  bool outlineMask,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1,
  SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID,
  bool depthOnly = false,
  const char* fragmentShaderPath = "gltf_player_model.frag.spv",
  FragmentResourceLayout fragmentLayout = {4U, 1U},
  bool enableColorBlend = false
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    outlineMask
      ? "outline_mask_gltf.vert.spv"
      : "gltf_player_model.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    depthOnly
      ? "sun_shadow.frag.spv"
      : outlineMask ? "outline_mask.frag.spv" : fragmentShaderPath,
    SDL_GPU_SHADERSTAGE_FRAGMENT,
    depthOnly ? 0U : outlineMask ? 0U : fragmentLayout.samplers,
    depthOnly ? 0U : outlineMask ? 1U : fragmentLayout.uniformBuffers
  );
  if (fragmentShader == nullptr) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return nullptr;
  }

  const std::array<SDL_GPUVertexBufferDescription, 2> vertexBufferDescriptions = {{
    {0, sizeof(GpuModelVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, sizeof(GpuGltfPlayerInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexAttribute, 15> vertexAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuModelVertex, position)},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuModelVertex, normal)},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuModelVertex, texCoord)},
    {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuModelVertex, red)},
    {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, offsetof(GpuModelVertex, joints)},
    {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuModelVertex, weights)},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row0)},
    {7, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row1)},
    {8, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row2)},
    {9, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuGltfPlayerInstance, red)},
    {10, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, firstBone)},
    {11, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, boneCount)},
    {12, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, flags)},
    {13, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuModelVertex, tintWeight)},
    {14, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GpuGltfPlayerInstance, ambientVisibility)},
  }};
  const std::array<SDL_GPUVertexAttribute, 9> outlineVertexAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuModelVertex, position)},
    {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, offsetof(GpuModelVertex, joints)},
    {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuModelVertex, weights)},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row0)},
    {7, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row1)},
    {8, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row2)},
    {10, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, firstBone)},
    {11, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, boneCount)},
    {12, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, flags)},
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = outlineMask
    ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    : colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID
      ? SDL_GetGPUSwapchainTextureFormat(device, window)
      : colorFormat;
  colorTarget.blend_state.src_color_blendfactor =
    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colorTarget.blend_state.dst_color_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colorTarget.blend_state.dst_alpha_blendfactor =
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  colorTarget.blend_state.enable_blend =
    enableColorBlend && !outlineMask && !depthOnly;

  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    vertexBufferDescriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers =
    static_cast<Uint32>(vertexBufferDescriptions.size());
  createInfo.vertex_input_state.vertex_attributes = outlineMask
    ? outlineVertexAttributes.data()
    : vertexAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    outlineMask
      ? static_cast<Uint32>(outlineVertexAttributes.size())
      : static_cast<Uint32>(vertexAttributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count =
    outlineMask ? SDL_GPU_SAMPLECOUNT_1 : sampleCount;
  createInfo.depth_stencil_state.compare_op = outlineMask
    ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL
    : SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = !outlineMask;
  createInfo.target_info.color_target_descriptions =
    depthOnly ? nullptr : &colorTarget;
  createInfo.target_info.num_color_targets = depthOnly ? 0U : 1U;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

struct GpuSceneColorPipelines {
  SDL_GPUGraphicsPipeline* world = nullptr;
  SDL_GPUGraphicsPipeline* worldSurface = nullptr;
  SDL_GPUGraphicsPipeline* translucent = nullptr;
  SDL_GPUGraphicsPipeline* instancedMesh = nullptr;
  SDL_GPUGraphicsPipeline* staticMesh = nullptr;
  SDL_GPUGraphicsPipeline* materialMesh = nullptr;
  SDL_GPUGraphicsPipeline* staticViewModel = nullptr;
  SDL_GPUGraphicsPipeline* materialViewModel = nullptr;
  SDL_GPUGraphicsPipeline* gltfPlayer = nullptr;
  SDL_GPUGraphicsPipeline* gltfPlayerFlat = nullptr;
  SDL_GPUGraphicsPipeline* instancedGlow = nullptr;
  SDL_GPUGraphicsPipeline* bloomSource = nullptr;
  SDL_GPUGraphicsPipeline* bloomBlur = nullptr;
};

struct GpuDirectPresentPipelines {
  SDL_GPUGraphicsPipeline* world = nullptr;
  SDL_GPUGraphicsPipeline* worldSurface = nullptr;
  SDL_GPUGraphicsPipeline* instancedMesh = nullptr;
  SDL_GPUGraphicsPipeline* staticMesh = nullptr;
  SDL_GPUGraphicsPipeline* materialMesh = nullptr;
  SDL_GPUGraphicsPipeline* gltfPlayer = nullptr;
};

[[nodiscard]] bool directPresentPipelinesReady(
  const GpuDirectPresentPipelines& pipelines
) {
  return pipelines.world != nullptr &&
    pipelines.worldSurface != nullptr &&
    pipelines.instancedMesh != nullptr &&
    pipelines.staticMesh != nullptr &&
    pipelines.materialMesh != nullptr &&
    pipelines.gltfPlayer != nullptr;
}

void destroyGpuDirectPresentPipelines(
  SDL_GPUDevice* device,
  GpuDirectPresentPipelines& pipelines
) {
  for (SDL_GPUGraphicsPipeline** pipeline : {
         &pipelines.world,
         &pipelines.worldSurface,
         &pipelines.instancedMesh,
         &pipelines.staticMesh,
         &pipelines.materialMesh,
         &pipelines.gltfPlayer,
       }) {
    if (*pipeline != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(device, *pipeline);
      *pipeline = nullptr;
    }
  }
}

[[nodiscard]] GpuDirectPresentPipelines createGpuDirectPresentPipelines(
  SDL_GPUDevice* device,
  SDL_Window* window,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUTextureFormat swapchainFormat
) {
  GpuDirectPresentPipelines pipelines;
  pipelines.world = createGpuPipeline3D(
    device,
    window,
    true,
    depthFormat,
    SDL_GPU_COMPAREOP_LESS,
    "world3d_direct.frag.spv",
    1,
    SDL_GPU_SAMPLECOUNT_1,
    swapchainFormat,
    0,
    false,
    "world3d.vert.spv",
    false
  );
  pipelines.worldSurface = createGpuPipeline3D(
    device,
    window,
    true,
    depthFormat,
    SDL_GPU_COMPAREOP_LESS,
    "world_surface_direct.frag.spv",
    1,
    SDL_GPU_SAMPLECOUNT_1,
    swapchainFormat,
    2,
    false,
    "world_surface.vert.spv",
    false
  );
  pipelines.instancedMesh = createGpuInstancedPipeline3D(
    device,
    window,
    "instanced_mesh.vert.spv",
    "instanced_color_direct.frag.spv",
    true,
    false,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    swapchainFormat,
    true,
    false,
    {0U, 1U}
  );
  pipelines.staticMesh = createGpuStaticMeshPipeline3D(
    device,
    window,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    swapchainFormat,
    false,
    "instanced_color_direct.frag.spv",
    {0U, 1U}
  );
  pipelines.materialMesh = createGpuMaterialMeshPipeline3D(
    device,
    window,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    swapchainFormat,
    false,
    "material_weapon_direct.frag.spv",
    {0U, 1U}
  );
  pipelines.gltfPlayer = createGpuGltfPlayerModelPipeline(
    device,
    window,
    false,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    swapchainFormat,
    false,
    "gltf_player_model_direct.frag.spv",
    {0U, 1U}
  );
  return pipelines;
}

[[nodiscard]] bool sceneColorPipelinesReady(
  const GpuSceneColorPipelines& pipelines
) {
  return pipelines.world != nullptr &&
    pipelines.worldSurface != nullptr &&
    pipelines.translucent != nullptr &&
    pipelines.instancedMesh != nullptr &&
    pipelines.staticMesh != nullptr &&
    pipelines.materialMesh != nullptr &&
    pipelines.staticViewModel != nullptr &&
    pipelines.materialViewModel != nullptr &&
    pipelines.gltfPlayer != nullptr &&
    pipelines.gltfPlayerFlat != nullptr &&
    pipelines.instancedGlow != nullptr &&
    pipelines.bloomSource != nullptr &&
    pipelines.bloomBlur != nullptr;
}

void destroyGpuSceneColorPipelines(
  SDL_GPUDevice* device,
  GpuSceneColorPipelines& pipelines
) {
  for (SDL_GPUGraphicsPipeline** pipeline : {
         &pipelines.world,
         &pipelines.worldSurface,
         &pipelines.translucent,
         &pipelines.instancedMesh,
         &pipelines.staticMesh,
         &pipelines.materialMesh,
         &pipelines.staticViewModel,
         &pipelines.materialViewModel,
         &pipelines.gltfPlayer,
         &pipelines.gltfPlayerFlat,
         &pipelines.instancedGlow,
         &pipelines.bloomSource,
         &pipelines.bloomBlur,
       }) {
    if (*pipeline != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(device, *pipeline);
      *pipeline = nullptr;
    }
  }
}

[[nodiscard]] GpuSceneColorPipelines createGpuSceneColorPipelines(
  SDL_GPUDevice* device,
  SDL_Window* window,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUTextureFormat colorFormat
) {
  GpuSceneColorPipelines pipelines;
  pipelines.world = createGpuPipeline3D(
    device,
    window,
    true,
    depthFormat,
    SDL_GPU_COMPAREOP_LESS,
    "world3d.frag.spv",
    1,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat
  );
  pipelines.worldSurface = createGpuPipeline3D(
    device,
    window,
    true,
    depthFormat,
    SDL_GPU_COMPAREOP_LESS,
    "world_surface.frag.spv",
    3,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    2,
    false,
    "world_surface.vert.spv"
  );
  pipelines.translucent = createGpuPipeline3D(
    device,
    window,
    false,
    depthFormat,
    SDL_GPU_COMPAREOP_LESS,
    "world3d.frag.spv",
    1,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat
  );
  pipelines.instancedMesh = createGpuInstancedPipeline3D(
    device,
    window,
    "instanced_mesh.vert.spv",
    "instanced_color.frag.spv",
    true,
    false,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat
  );
  pipelines.staticMesh = createGpuStaticMeshPipeline3D(
    device,
    window,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    false,
    "instanced_color.frag.spv",
    instancedColorFragmentLayout(),
    true
  );
  pipelines.materialMesh = createGpuMaterialMeshPipeline3D(
    device,
    window,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    false,
    "material_weapon.frag.spv",
    {3U, 1U},
    true
  );
  pipelines.staticViewModel = createGpuStaticMeshPipeline3D(
    device,
    window,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat
  );
  pipelines.materialViewModel = createGpuMaterialMeshPipeline3D(
    device,
    window,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat
  );
  pipelines.gltfPlayer = createGpuGltfPlayerModelPipeline(
    device,
    window,
    false,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    false,
    "gltf_player_model.frag.spv",
    {4U, 1U},
    true
  );
  pipelines.gltfPlayerFlat = createGpuGltfPlayerModelPipeline(
    device,
    window,
    false,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    false,
    "gltf_player_model_flat.frag.spv",
    {2U, 1U},
    true
  );
  pipelines.instancedGlow = createGpuInstancedPipeline3D(
    device,
    window,
    "instanced_billboard.vert.spv",
    "instanced_glow.frag.spv",
    false,
    true,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    true,
    false,
    untexturedSceneLightFragmentLayout()
  );
  pipelines.bloomSource = createGpuInstancedPipeline3D(
    device,
    window,
    "instanced_billboard.vert.spv",
    "bloom_source.frag.spv",
    false,
    true,
    depthFormat,
    SDL_GPU_SAMPLECOUNT_1,
    colorFormat,
    true,
    false,
    untexturedSceneLightFragmentLayout()
  );
  pipelines.bloomBlur = createGpuPostProcessPipeline(
    device,
    "bloom_blur.frag.spv",
    1,
    colorFormat
  );
  return pipelines;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuGltfSunShadowPipeline(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "sun_shadow_gltf.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1,
    1
  );
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "sun_shadow.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT
  );
  if (vertexShader == nullptr || fragmentShader == nullptr) {
    if (vertexShader != nullptr) {
      SDL_ReleaseGPUShader(device, vertexShader);
    }
    if (fragmentShader != nullptr) {
      SDL_ReleaseGPUShader(device, fragmentShader);
    }
    return nullptr;
  }

  const std::array<SDL_GPUVertexBufferDescription, 2> descriptions = {{
    {0, sizeof(GpuModelVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, sizeof(GpuGltfPlayerInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexAttribute, 9> attributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuModelVertex, position)},
    {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, offsetof(GpuModelVertex, joints)},
    {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuModelVertex, weights)},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row0)},
    {7, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row1)},
    {8, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuGltfPlayerInstance, row2)},
    {10, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, firstBone)},
    {11, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, boneCount)},
    {12, 1, SDL_GPU_VERTEXELEMENTFORMAT_UINT, offsetof(GpuGltfPlayerInstance, flags)},
  }};
  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    descriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers =
    static_cast<Uint32>(descriptions.size());
  createInfo.vertex_input_state.vertex_attributes = attributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    static_cast<Uint32>(attributes.size());
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
  createInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.depth_bias_constant_factor = 1.25F;
  createInfo.rasterizer_state.depth_bias_slope_factor = 1.75F;
  createInfo.rasterizer_state.enable_depth_bias = true;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = true;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;
  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuSunShadowPipeline(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat,
  int vertexLayout
) {
  const char* vertexShaderName = vertexLayout == 0
    ? "sun_shadow_world.vert.spv"
    : vertexLayout == 1
      ? "sun_shadow_static.vert.spv"
      : "sun_shadow_material.vert.spv";
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    vertexShaderName,
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "sun_shadow.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT
  );
  if (vertexShader == nullptr || fragmentShader == nullptr) {
    if (vertexShader != nullptr) {
      SDL_ReleaseGPUShader(device, vertexShader);
    }
    if (fragmentShader != nullptr) {
      SDL_ReleaseGPUShader(device, fragmentShader);
    }
    return nullptr;
  }

  const std::array<SDL_GPUVertexBufferDescription, 2> worldDescriptions = {{
    {0, sizeof(GpuVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, sizeof(GpuStaticInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexBufferDescription, 2> materialDescriptions = {{
    {0, sizeof(GpuMaterialVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, sizeof(GpuStaticInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
  }};
  const std::array<SDL_GPUVertexAttribute, 4> worldAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, x)},
    {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row0)},
    {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row1)},
    {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row2)},
  }};
  const std::array<SDL_GPUVertexAttribute, 4> materialAttributes = {{
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuMaterialVertex, position)},
    {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row0)},
    {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row1)},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuStaticInstance, row2)},
  }};
  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions =
    vertexLayout == 2 ? materialDescriptions.data() : worldDescriptions.data();
  createInfo.vertex_input_state.num_vertex_buffers =
    vertexLayout == 0 ? 1U : 2U;
  createInfo.vertex_input_state.vertex_attributes =
    vertexLayout == 2 ? materialAttributes.data() : worldAttributes.data();
  createInfo.vertex_input_state.num_vertex_attributes =
    vertexLayout == 0 ? 1U : 4U;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
  createInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.depth_bias_constant_factor = 1.25F;
  createInfo.rasterizer_state.depth_bias_slope_factor = 1.75F;
  createInfo.rasterizer_state.enable_depth_bias = true;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = true;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;
  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* createGpuPointShadowPipeline(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "point_shadow_world.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "sun_shadow.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT
  );
  if (vertexShader == nullptr || fragmentShader == nullptr) {
    if (vertexShader != nullptr) {
      SDL_ReleaseGPUShader(device, vertexShader);
    }
    if (fragmentShader != nullptr) {
      SDL_ReleaseGPUShader(device, fragmentShader);
    }
    return nullptr;
  }
  const SDL_GPUVertexBufferDescription description = {
    0,
    sizeof(GpuVertex),
    SDL_GPU_VERTEXINPUTRATE_VERTEX,
    0,
  };
  const SDL_GPUVertexAttribute attribute = {
    0,
    0,
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
    offsetof(GpuVertex, x),
  };
  SDL_GPUGraphicsPipelineCreateInfo createInfo = {};
  createInfo.vertex_shader = vertexShader;
  createInfo.fragment_shader = fragmentShader;
  createInfo.vertex_input_state.vertex_buffer_descriptions = &description;
  createInfo.vertex_input_state.num_vertex_buffers = 1U;
  createInfo.vertex_input_state.vertex_attributes = &attribute;
  createInfo.vertex_input_state.num_vertex_attributes = 1U;
  createInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  createInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  // Map brush winding can differ by imported source. A depth-only cache is
  // cheap enough at two lights to keep both sides and avoid missing walls.
  createInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  createInfo.rasterizer_state.front_face =
    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  createInfo.rasterizer_state.depth_bias_constant_factor = 1.25F;
  createInfo.rasterizer_state.depth_bias_slope_factor = 1.75F;
  createInfo.rasterizer_state.enable_depth_bias = true;
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = true;
  createInfo.target_info.depth_stencil_format = depthFormat;
  createInfo.target_info.has_depth_stencil_target = true;
  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] std::vector<std::uint8_t> readBinaryFile(
  const std::filesystem::path& path
) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const std::streamsize size = file.tellg();
  if (size <= 0) {
    return {};
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
    return {};
  }
  return bytes;
}

[[nodiscard]] std::vector<std::filesystem::path> fontPathCandidates(
  std::string_view requestedFont
) {
  const std::string requested =
    requestedFont.empty() ? "bahnschrift.ttf" : std::string(requestedFont);
  const std::filesystem::path requestedPath{requested};
  std::vector<std::filesystem::path> candidates;
  candidates.push_back(requestedPath);
  if (!requestedPath.is_absolute()) {
    candidates.push_back(std::filesystem::path{"assets/fonts"} / requestedPath);
    candidates.push_back(std::filesystem::path{"assets/font"} / requestedPath);
    candidates.push_back(std::filesystem::path{"C:/Windows/Fonts"} / requestedPath);
  }

  if (!requestedPath.has_extension()) {
    for (const char* extension : {".ttf", ".otf", ".TTF", ".OTF"}) {
      const std::filesystem::path withExtension = requestedPath.string() + extension;
      candidates.push_back(withExtension);
      if (!withExtension.is_absolute()) {
        candidates.push_back(std::filesystem::path{"assets/fonts"} / withExtension);
        candidates.push_back(std::filesystem::path{"assets/font"} / withExtension);
        candidates.push_back(std::filesystem::path{"C:/Windows/Fonts"} / withExtension);
      }
    }
  }
  return candidates;
}

[[nodiscard]] std::optional<std::filesystem::path> findUiFontPath(
  std::string_view requestedFont
) {
  for (const std::filesystem::path& path : fontPathCandidates(requestedFont)) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
      return path;
    }
  }
  return std::nullopt;
}

void fillSolidFontTexelBlock(std::vector<std::uint8_t>& pixels) {
  for (Uint32 y = 0; y < 8; ++y) {
    for (Uint32 x = 0; x < 8; ++x) {
      pixels[y * kFontAtlasWidth + x] = 255;
    }
  }
}

void addBitmapGlyph(
  FontAtlas& atlas,
  std::uint32_t character,
  float u0,
  float v0,
  float u1,
  float v1,
  bool drawable
) {
  atlas.glyphs[character] = FontGlyph{
    u0,
    v0,
    u1,
    v1,
    0.0F,
    0.0F,
    kBitmapGlyphSize,
    kBitmapGlyphSize,
    kBitmapGlyphSize,
    drawable,
  };
}

[[nodiscard]] bool placeBitmapGlyphRows(
  FontAtlas& atlas,
  std::vector<std::uint8_t>& pixels,
  std::uint32_t codepoint,
  std::array<std::uint8_t, 8> rows,
  int& penX,
  int& penY,
  int& rowHeight,
  int pixelScale,
  float baseline
) {
  const int glyphWidth = 8 * pixelScale;
  const int glyphHeight = 8 * pixelScale;
  if (penX + glyphWidth + 2 >= static_cast<int>(kFontAtlasWidth)) {
    penX = 8;
    penY += rowHeight + 2;
    rowHeight = 0;
  }
  if (penY + glyphHeight + 2 >= static_cast<int>(kFontAtlasHeight)) {
    return false;
  }

  for (int sourceY = 0; sourceY < 8; ++sourceY) {
    const std::uint8_t bits = rows[static_cast<std::size_t>(sourceY)];
    for (int sourceX = 0; sourceX < 8; ++sourceX) {
      const bool set = (bits & (1U << sourceX)) != 0;
      if (!set) {
        continue;
      }
      for (int y = 0; y < pixelScale; ++y) {
        for (int x = 0; x < pixelScale; ++x) {
          pixels[
            (penY + sourceY * pixelScale + y) * kFontAtlasWidth +
              penX + sourceX * pixelScale + x
          ] = 255;
        }
      }
    }
  }

  const float u0 =
    static_cast<float>(penX) / static_cast<float>(kFontAtlasWidth);
  const float v0 =
    static_cast<float>(penY) / static_cast<float>(kFontAtlasHeight);
  atlas.glyphs[codepoint] = FontGlyph{
    u0,
    v0,
    static_cast<float>(penX + glyphWidth) /
      static_cast<float>(kFontAtlasWidth),
    static_cast<float>(penY + glyphHeight) /
      static_cast<float>(kFontAtlasHeight),
    0.0F,
    baseline - static_cast<float>(glyphHeight),
    static_cast<float>(glyphWidth),
    static_cast<float>(glyphHeight),
    static_cast<float>(glyphWidth),
    true,
  };
  penX += glyphWidth + 2;
  rowHeight = std::max(rowHeight, glyphHeight);
  return true;
}

[[nodiscard]] std::vector<std::uint8_t> buildBitmapFontAtlas(
  FontAtlas& atlas
) {
  std::vector<std::uint8_t> pixels(kFontAtlasWidth * kFontAtlasHeight);
  fillSolidFontTexelBlock(pixels);
  atlas.lineHeight = kBitmapGlyphSize;
  atlas.baseScaleDenominator = kBitmapGlyphSize;
  atlas.truetype = false;

  const std::size_t fontGlyphCount =
    sizeof(SDL_RenderDebugTextFontData) / sizeof(SDL_RenderDebugTextFontData[0]) / 8U;
  addBitmapGlyph(atlas, ' ', 0.0F, 0.0F, 0.0F, 0.0F, false);
  for (Uint32 character = 33; character < 256; ++character) {
    const std::size_t glyphIndex = static_cast<std::size_t>(character - 33U);
    if (glyphIndex >= fontGlyphCount) {
      continue;
    }
    const Uint32 cellX = (character % 16U) * 8U;
    const Uint32 cellY = (character / 16U) * 8U;
    const std::size_t glyphOffset = glyphIndex * 8U;
    for (Uint32 y = 0; y < 8; ++y) {
      const std::uint8_t bits =
        SDL_RenderDebugTextFontData[glyphOffset + y];
      for (Uint32 x = 0; x < 8; ++x) {
        const bool set = (bits & (1U << x)) != 0;
        pixels[
          (cellY + y) * kFontAtlasWidth + cellX + x
        ] = set ? 255 : 0;
      }
    }
    const float u0 =
      static_cast<float>(cellX) / static_cast<float>(kFontAtlasWidth);
    const float v0 =
      static_cast<float>(cellY) / static_cast<float>(kFontAtlasHeight);
    addBitmapGlyph(
      atlas,
      character,
      u0,
      v0,
      u0 + kBitmapGlyphSize / static_cast<float>(kFontAtlasWidth),
      v0 + kBitmapGlyphSize / static_cast<float>(kFontAtlasHeight),
      true
    );
  }
  for (const std::uint32_t character : {
         0x221EU,
         0x00C5U,
         0x00C4U,
         0x00D6U,
         0x00E5U,
         0x00E4U,
         0x00F6U,
       }) {
    const auto glyph = supplementalBitmapGlyph(character);
    if (!glyph.has_value()) {
      continue;
    }
    const std::uint32_t atlasSlot = character == 0x221EU ? 0x007FU : character;
    const Uint32 cellX = (atlasSlot % 16U) * 8U;
    const Uint32 cellY = (atlasSlot / 16U) * 8U;
    for (Uint32 y = 0; y < 8; ++y) {
      const std::uint8_t bits = (*glyph)[y];
      for (Uint32 x = 0; x < 8; ++x) {
        const bool set = (bits & (1U << x)) != 0;
        pixels[(cellY + y) * kFontAtlasWidth + cellX + x] = set ? 255 : 0;
      }
    }
    const float u0 =
      static_cast<float>(cellX) / static_cast<float>(kFontAtlasWidth);
    const float v0 =
      static_cast<float>(cellY) / static_cast<float>(kFontAtlasHeight);
    addBitmapGlyph(
      atlas,
      character,
      u0,
      v0,
      u0 + kBitmapGlyphSize / static_cast<float>(kFontAtlasWidth),
      v0 + kBitmapGlyphSize / static_cast<float>(kFontAtlasHeight),
      true
    );
  }
  return pixels;
}

[[nodiscard]] bool tryBuildTrueTypeFontAtlas(
  FontAtlas& atlas,
  std::vector<std::uint8_t>& pixels,
  std::string_view requestedFont,
  float pixelHeight
) {
  const std::optional<std::filesystem::path> fontPath =
    findUiFontPath(requestedFont);
  if (!fontPath.has_value()) {
    return false;
  }
  const std::vector<std::uint8_t> fontBytes = readBinaryFile(*fontPath);
  if (fontBytes.empty()) {
    return false;
  }

  stbtt_fontinfo font = {};
  if (!stbtt_InitFont(&font, fontBytes.data(), 0)) {
    return false;
  }

  const std::optional<std::filesystem::path> fallbackFontPath =
    findUiFontPath("arial.ttf");
  const std::vector<std::uint8_t> fallbackFontBytes = fallbackFontPath.has_value()
    ? readBinaryFile(*fallbackFontPath)
    : std::vector<std::uint8_t>{};
  stbtt_fontinfo fallbackFont = {};
  const bool fallbackFontAvailable =
    !fallbackFontBytes.empty() &&
    stbtt_InitFont(&fallbackFont, fallbackFontBytes.data(), 0);

  pixels.assign(kFontAtlasWidth * kFontAtlasHeight, 0);
  fillSolidFontTexelBlock(pixels);
  atlas.glyphs.clear();
  atlas.loadedFont = fontPath->string();
  atlas.truetype = true;
  atlas.baseScaleDenominator = pixelHeight;
  atlas.nominalPixelHeight = pixelHeight;

  const float fontScale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
  const float fallbackFontScale = fallbackFontAvailable
    ? stbtt_ScaleForPixelHeight(&fallbackFont, pixelHeight)
    : 0.0F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
  atlas.lineHeight =
    static_cast<float>(ascent - descent + lineGap) * fontScale;
  const float baseline = static_cast<float>(ascent) * fontScale;

  std::vector<std::uint32_t> codepoints;
  codepoints.push_back(' ');
  for (std::uint32_t character = 33; character < 127; ++character) {
    codepoints.push_back(character);
  }
  for (std::uint32_t character : {
         0x00C5U,
         0x00C4U,
         0x00D6U,
         0x00E5U,
         0x00E4U,
         0x00F6U,
         0x221EU,
       }) {
    codepoints.push_back(character);
  }

  int penX = 8;
  int penY = 8;
  int rowHeight = 0;
  for (std::uint32_t codepoint : codepoints) {
    const stbtt_fontinfo* glyphFont = &font;
    float glyphScale = fontScale;
    if (
      stbtt_FindGlyphIndex(glyphFont, static_cast<int>(codepoint)) == 0 &&
      fallbackFontAvailable &&
      stbtt_FindGlyphIndex(&fallbackFont, static_cast<int>(codepoint)) != 0
    ) {
      glyphFont = &fallbackFont;
      glyphScale = fallbackFontScale;
    }
    if (stbtt_FindGlyphIndex(glyphFont, static_cast<int>(codepoint)) == 0) {
      const auto supplemental = supplementalBitmapGlyph(codepoint);
      if (
        supplemental.has_value() &&
        !placeBitmapGlyphRows(
          atlas,
          pixels,
          codepoint,
          *supplemental,
          penX,
          penY,
          rowHeight,
          std::max(1, static_cast<int>(std::lround(pixelHeight / 8.0F))),
          baseline
        )
      ) {
        return false;
      }
      continue;
    }

    int advance = 0;
    int leftBearing = 0;
    stbtt_GetCodepointHMetrics(
      glyphFont,
      static_cast<int>(codepoint),
      &advance,
      &leftBearing
    );

    if (codepoint == ' ') {
      atlas.glyphs[codepoint] = FontGlyph{
        kSolidTextureU,
        kSolidTextureV,
        kSolidTextureU,
        kSolidTextureV,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        std::max(4.0F, static_cast<float>(advance) * glyphScale),
        false,
      };
      continue;
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(
      glyphFont,
      static_cast<int>(codepoint),
      glyphScale,
      glyphScale,
      &x0,
      &y0,
      &x1,
      &y1
    );
    const int glyphWidth = std::max(0, x1 - x0);
    const int glyphHeight = std::max(0, y1 - y0);
    if (glyphWidth <= 0 || glyphHeight <= 0) {
      continue;
    }
    if (penX + glyphWidth + 2 >= static_cast<int>(kFontAtlasWidth)) {
      penX = 8;
      penY += rowHeight + 2;
      rowHeight = 0;
    }
    if (penY + glyphHeight + 2 >= static_cast<int>(kFontAtlasHeight)) {
      return false;
    }

    stbtt_MakeCodepointBitmap(
      glyphFont,
      pixels.data() + (penY * kFontAtlasWidth) + penX,
      glyphWidth,
      glyphHeight,
      kFontAtlasWidth,
      glyphScale,
      glyphScale,
      static_cast<int>(codepoint)
    );
    const float u0 =
      static_cast<float>(penX) / static_cast<float>(kFontAtlasWidth);
    const float v0 =
      static_cast<float>(penY) / static_cast<float>(kFontAtlasHeight);
    atlas.glyphs[codepoint] = FontGlyph{
      u0,
      v0,
      static_cast<float>(penX + glyphWidth) /
        static_cast<float>(kFontAtlasWidth),
      static_cast<float>(penY + glyphHeight) /
        static_cast<float>(kFontAtlasHeight),
      static_cast<float>(x0),
      baseline + static_cast<float>(y0),
      static_cast<float>(glyphWidth),
      static_cast<float>(glyphHeight),
      std::max(1.0F, static_cast<float>(advance) * glyphScale),
      true,
    };
    penX += glyphWidth + 2;
    rowHeight = std::max(rowHeight, glyphHeight);
  }

  return atlas.glyphs.find('?') != atlas.glyphs.end();
}

[[nodiscard]] std::uint32_t fontAtlasCodepointAt(
  std::string_view text,
  std::size_t offset,
  std::size_t& byteLength
) {
  const BitmapGlyphLookup bitmapGlyph = bitmapGlyphAt(text, offset);
  byteLength = std::max<std::size_t>(1U, bitmapGlyph.byteLength);
  if (
    offset + 2U < text.size() &&
    static_cast<unsigned char>(text[offset]) == 0xE2U &&
    static_cast<unsigned char>(text[offset + 1U]) == 0x88U &&
    static_cast<unsigned char>(text[offset + 2U]) == 0x9EU
  ) {
    return 0x221EU;
  }
  return bitmapGlyph.atlasCodepoint;
}

[[nodiscard]] FontAtlas* createFontAtlas(
  SDL_GPUDevice* device,
  std::string_view requestedFont,
  float pixelHeight
) {
  auto* atlas = new FontAtlas();
  atlas->requestedFont =
    requestedFont.empty() ? "bahnschrift.ttf" : std::string(requestedFont);
  atlas->nominalPixelHeight = pixelHeight;
  std::vector<std::uint8_t> pixels;
  if (
    !tryBuildTrueTypeFontAtlas(
      *atlas,
      pixels,
      atlas->requestedFont,
      pixelHeight
    )
  ) {
    atlas->glyphs.clear();
    pixels = buildBitmapFontAtlas(*atlas);
    atlas->loadedFont = "bitmap fallback";
    atlas->nominalPixelHeight = pixelHeight;
  }

  const SDL_GPUTextureCreateInfo textureInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    SDL_GPU_TEXTUREFORMAT_R8_UNORM,
    SDL_GPU_TEXTUREUSAGE_SAMPLER,
    kFontAtlasWidth,
    kFontAtlasHeight,
    1,
    1,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
  if (texture == nullptr) {
    delete atlas;
    return nullptr;
  }

  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    kFontAtlasWidth * kFontAtlasHeight,
    0,
  };
  SDL_GPUTransferBuffer* transfer =
    SDL_CreateGPUTransferBuffer(device, &transferInfo);
  if (transfer == nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
    delete atlas;
    return nullptr;
  }

  void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    delete atlas;
    return nullptr;
  }
  std::memcpy(mapped, pixels.data(), pixels.size());
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer* commandBuffer =
    SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
    ? SDL_BeginGPUCopyPass(commandBuffer)
    : nullptr;
  if (copyPass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    delete atlas;
    return nullptr;
  }

  const SDL_GPUTextureTransferInfo source = {
    transfer,
    0,
    kFontAtlasWidth,
    kFontAtlasHeight,
  };
  const SDL_GPUTextureRegion destination = {
    texture,
    0,
    0,
    0,
    0,
    0,
    kFontAtlasWidth,
    kFontAtlasHeight,
    1,
  };
  SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
  SDL_EndGPUCopyPass(copyPass);
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  if (!submitted) {
    SDL_ReleaseGPUTexture(device, texture);
    delete atlas;
    return nullptr;
  }
  atlas->texture = texture;
  return atlas;
}

void destroyFontAtlas(SDL_GPUDevice* device, FontAtlas* atlas) {
  if (atlas == nullptr) {
    return;
  }
  if (atlas->texture != nullptr) {
    SDL_ReleaseGPUTexture(device, atlas->texture);
  }
  delete atlas;
}

void destroyFontAtlasSet(SDL_GPUDevice* device, FontAtlasSet* fontAtlasSet) {
  if (fontAtlasSet == nullptr) {
    return;
  }
  for (FontAtlas*& atlas : fontAtlasSet->atlases) {
    destroyFontAtlas(device, atlas);
    atlas = nullptr;
  }
  delete fontAtlasSet;
}

[[nodiscard]] FontAtlasSet* createFontAtlasSet(
  SDL_GPUDevice* device,
  std::string_view requestedFont
) {
  auto* fontAtlasSet = new FontAtlasSet();
  fontAtlasSet->requestedFont =
    requestedFont.empty() ? "bahnschrift.ttf" : std::string(requestedFont);
  for (std::size_t index = 0; index < kUiFontPixelHeights.size(); ++index) {
    fontAtlasSet->atlases[index] = createFontAtlas(
      device,
      fontAtlasSet->requestedFont,
      kUiFontPixelHeights[index]
    );
    if (fontAtlasSet->atlases[index] == nullptr) {
      destroyFontAtlasSet(device, fontAtlasSet);
      return nullptr;
    }
  }
  return fontAtlasSet;
}

[[nodiscard]] GpuVertex gpuVertex(
  ScreenPoint point,
  RenderColor color,
  float outputWidth,
  float outputHeight,
  float u = kSolidTextureU,
  float v = kSolidTextureV
) {
  return {
    ((point.x / outputWidth) * 2.0F) - 1.0F,
    1.0F - ((point.y / outputHeight) * 2.0F),
    0.0F,
    color.red,
    color.green,
    color.blue,
    color.alpha,
    u,
    v,
    {},
    0U,
  };
}

[[nodiscard]] GpuVertex gpuVertex3D(
  const Vertex3D& vertex,
  float u,
  float v
) {
  return {
    vertex.position.x,
    vertex.position.y,
    vertex.position.z,
    vertex.color.red,
    vertex.color.green,
    vertex.color.blue,
    vertex.color.alpha,
    u,
    v,
    {vertex.normal.x, vertex.normal.y, vertex.normal.z},
    (vertex.materialSlot & 0x0000FFFFU) |
      (static_cast<std::uint32_t>(vertex.ambientVisibility) << 16U) |
      (static_cast<std::uint32_t>(vertex.ambientDebug) << 24U),
  };
}

[[nodiscard]] float wrappedTileCoordinate(float value) {
  return value - std::floor(value);
}

struct MaterialVertex3D {
  Vertex3D vertex = {};
  float u = 0.0F;
  float v = 0.0F;
};

[[nodiscard]] std::uint8_t interpolateChannel(
  std::uint8_t first,
  std::uint8_t second,
  float amount
) {
  return static_cast<std::uint8_t>(std::clamp(
    static_cast<float>(first) +
      (static_cast<float>(second) - static_cast<float>(first)) * amount,
    0.0F,
    255.0F
  ));
}

[[nodiscard]] MaterialVertex3D interpolateMaterialVertex(
  const MaterialVertex3D& first,
  const MaterialVertex3D& second,
  float amount
) {
  MaterialVertex3D result;
  result.vertex.position = first.vertex.position +
    (second.vertex.position - first.vertex.position) * amount;
  result.vertex.color = {
    interpolateChannel(first.vertex.color.red, second.vertex.color.red, amount),
    interpolateChannel(first.vertex.color.green, second.vertex.color.green, amount),
    interpolateChannel(first.vertex.color.blue, second.vertex.color.blue, amount),
    interpolateChannel(first.vertex.color.alpha, second.vertex.color.alpha, amount),
  };
  result.vertex.materialId = first.vertex.materialId;
  result.vertex.normal = normalize(
    first.vertex.normal +
    (second.vertex.normal - first.vertex.normal) * amount
  );
  result.vertex.materialSlot = first.vertex.materialSlot;
  result.u = first.u + (second.u - first.u) * amount;
  result.v = first.v + (second.v - first.v) * amount;
  return result;
}

void appendWrappedMaterialTriangle3D(
  std::vector<GpuVertex>& vertices,
  const TextureAtlasEntry& entry,
  const MaterialVertex3D& first,
  const MaterialVertex3D& second,
  const MaterialVertex3D& third,
  int depth
) {
  constexpr int kMaxTextureSplitDepth = 24;
  const std::array<float, 3> materialU = {{first.u, second.u, third.u}};
  const std::array<float, 3> materialV = {{first.v, second.v, third.v}};
  const auto uBounds = std::minmax_element(materialU.begin(), materialU.end());
  const auto vBounds = std::minmax_element(materialV.begin(), materialV.end());
  const float uRange = *uBounds.second - *uBounds.first;
  const float vRange = *vBounds.second - *vBounds.first;

  if ((uRange > 1.0001F || vRange > 1.0001F) && depth < kMaxTextureSplitDepth) {
    const bool splitU = uRange >= vRange;
    const std::array<MaterialVertex3D, 3> points = {{first, second, third}};
    for (std::size_t index = 0; index < points.size(); ++index) {
      const std::size_t next = (index + 1U) % points.size();
      const float start = splitU ? points[index].u : points[index].v;
      const float end = splitU ? points[next].u : points[next].v;
      if (std::fabs(end - start) <= 0.0001F) {
        continue;
      }
      const float low = std::min(start, end);
      const float high = std::max(start, end);
      const float boundary = std::ceil(low);
      if (boundary <= low + 0.0001F || boundary >= high - 0.0001F) {
        continue;
      }
      const float amount = (boundary - start) / (end - start);
      const MaterialVertex3D split =
        interpolateMaterialVertex(points[index], points[next], amount);
      const MaterialVertex3D& opposite = points[(index + 2U) % points.size()];
      appendWrappedMaterialTriangle3D(
        vertices,
        entry,
        points[index],
        split,
        opposite,
        depth + 1
      );
      appendWrappedMaterialTriangle3D(
        vertices,
        entry,
        split,
        points[next],
        opposite,
        depth + 1
      );
      return;
    }
  }

  const float uBase = std::floor(*uBounds.first);
  const float vBase = std::floor(*vBounds.first);
  const bool singleURepeat = uRange <= 1.0001F;
  const bool singleVRepeat = vRange <= 1.0001F;
  const auto tileCoordinate = [](float value, float base, bool singleRepeat) {
    return singleRepeat
      ? std::clamp(value - base, 0.0F, 1.0F)
      : wrappedTileCoordinate(value);
  };
  const auto atlasU = [&](float value) {
    const float tileU = tileCoordinate(value, uBase, singleURepeat);
    return entry.u0 + tileU * (entry.u1 - entry.u0);
  };
  const auto atlasV = [&](float value) {
    const float tileV = tileCoordinate(value, vBase, singleVRepeat);
    return entry.v0 + tileV * (entry.v1 - entry.v0);
  };
  vertices.push_back(gpuVertex3D(first.vertex, atlasU(first.u), atlasV(first.v)));
  vertices.push_back(gpuVertex3D(second.vertex, atlasU(second.u), atlasV(second.v)));
  vertices.push_back(gpuVertex3D(third.vertex, atlasU(third.u), atlasV(third.v)));
}

void logGpuTriangle3D(
  std::uint32_t requestedMaterialId,
  std::uint32_t effectiveMaterialId,
  const TextureAtlasEntry& entry,
  const MaterialVertex3D& first,
  const MaterialVertex3D& second,
  const MaterialVertex3D& third,
  std::size_t beforeVertexCount,
  std::size_t afterVertexCount
) {
  static int loggedTriangles = 0;
  if (!textureDebugEnabled() || loggedTriangles >= 10) {
    return;
  }
  ++loggedTriangles;
  const std::array<float, 3> u = {{first.u, second.u, third.u}};
  const std::array<float, 3> v = {{first.v, second.v, third.v}};
  const auto uBounds = std::minmax_element(u.begin(), u.end());
  const auto vBounds = std::minmax_element(v.begin(), v.end());
  std::cerr
    << "LG_DUEL_TEXTURE_PIPELINE_V2 renderer tri#" << loggedTriangles
    << " requestedMaterialId=" << requestedMaterialId
    << " effectiveMaterialId=" << effectiveMaterialId
    << " material=" << entry.material
    << " source=" << entry.sourceWidth << 'x' << entry.sourceHeight
    << " preAtlasUvRange=(" << *uBounds.first << ".." << *uBounds.second
    << ", " << *vBounds.first << ".." << *vBounds.second << ")"
    << " atlasRect=" << entry.u0 << ',' << entry.v0
    << " -> " << entry.u1 << ',' << entry.v1
    << " verticesBefore=" << beforeVertexCount
    << " verticesAfter=" << afterVertexCount
    << " outputTriangles=" << ((afterVertexCount - beforeVertexCount) / 3U)
    << '\n';
}

void appendGpuTriangle3D(
  std::vector<GpuVertex>& vertices,
  const Vertex3D& first,
  const Vertex3D& second,
  const Vertex3D& third,
  const TextureAtlas* atlas
) {
  float u0 = kSolidTextureU;
  float v0 = kSolidTextureV;
  float u1 = kSolidTextureU;
  float v1 = kSolidTextureV;
  const std::uint32_t forcedMaterialId = forcedTextureMaterialId();
  const std::uint32_t effectiveMaterialId =
    forcedMaterialId != 0U ? forcedMaterialId : first.materialId;
  if (atlas != nullptr && effectiveMaterialId != 0U) {
    const auto entry = atlas->entries.find(effectiveMaterialId);
    if (entry != atlas->entries.end()) {
      const std::size_t beforeVertexCount = vertices.size();
      const MaterialVertex3D materialFirst = {
        first,
        first.u * entry->second.uScale,
        first.v * entry->second.vScale
      };
      const MaterialVertex3D materialSecond = {
        second,
        second.u * entry->second.uScale,
        second.v * entry->second.vScale
      };
      const MaterialVertex3D materialThird = {
        third,
        third.u * entry->second.uScale,
        third.v * entry->second.vScale
      };
      appendWrappedMaterialTriangle3D(
        vertices,
        entry->second,
        materialFirst,
        materialSecond,
        materialThird,
        0
      );
      logGpuTriangle3D(
        first.materialId,
        effectiveMaterialId,
        entry->second,
        materialFirst,
        materialSecond,
        materialThird,
        beforeVertexCount,
        vertices.size()
      );
      return;
    }
    if (textureDebugEnabled()) {
      static int loggedMisses = 0;
      if (loggedMisses < 10) {
        ++loggedMisses;
        std::cerr
          << "LG_DUEL_TEXTURE_PIPELINE_V2 atlas material miss requestedMaterialId="
          << first.materialId
          << " effectiveMaterialId=" << effectiveMaterialId << '\n';
      }
    }
  }
  vertices.push_back(gpuVertex3D(first, u0, v0));
  vertices.push_back(gpuVertex3D(second, u1, v1));
  vertices.push_back(gpuVertex3D(third, u1, v1));
}

void appendVertices3D(
  std::vector<GpuVertex>& vertices,
  const std::vector<Vertex3D>& source,
  const TextureAtlas* atlas
) {
  for (std::size_t index = 0; index + 2 < source.size(); index += 3) {
    appendGpuTriangle3D(
      vertices,
      source[index],
      source[index + 1U],
      source[index + 2U],
      atlas
    );
  }
}

void appendScene3D(
  std::vector<GpuVertex>& vertices,
  const Scene3D& scene,
  const TextureAtlas* atlas
) {
  appendVertices3D(vertices, scene.vertices, atlas);
}

[[nodiscard]] std::vector<std::uint32_t> referencedWorldMaterials(
  const std::vector<Vertex3D>& vertices
) {
  std::vector<std::uint32_t> materials;
  materials.reserve(vertices.size() / 3U);
  for (const Vertex3D& vertex : vertices) {
    if (vertex.materialId != 0U) {
      materials.push_back(vertex.materialId);
    }
  }
  std::sort(materials.begin(), materials.end());
  materials.erase(std::unique(materials.begin(), materials.end()), materials.end());
  return materials;
}

[[nodiscard]] StaticWorldMesh* buildStaticWorldMesh(
  SDL_GPUDevice* device,
  const Arena& arena,
  const RenderSettings& settings
) {
  const auto buildStart = RenderClock::now();
  StaticAmbientBakeStats ambientBakeStats;
  Scene3D worldScene = buildStaticWorldScene(
    arena,
    settings.ambientGroundingQuality,
    settings.ambientDebugMode,
    &ambientBakeStats
  );
  const std::uint32_t sourceTriangles =
    static_cast<std::uint32_t>(worldScene.vertices.size() / 3U);
  const std::uint32_t duplicateTrianglesCulled =
    cullDuplicateStaticWorldTriangles(worldScene);
  auto mesh = new StaticWorldMesh();
  mesh->arenaFingerprint = arenaStaticWorldFingerprint(arena);
  mesh->arenaRevision = settings.mapRevision;
  mesh->ambientQuality = settings.ambientGroundingQuality;
  mesh->ambientDebugMode = settings.ambientDebugMode;
  mesh->ambientBakeStats = ambientBakeStats;
  mesh->ambientProbeGrid = bakeStaticAmbientProbeGrid(
    arena,
    settings.ambientGroundingQuality
  );
  mesh->sourceTriangles = sourceTriangles;
  mesh->duplicateTrianglesCulled = duplicateTrianglesCulled;
  mesh->vertexCount = static_cast<std::uint32_t>(worldScene.vertices.size());
  mesh->opaqueVertices = std::all_of(
    worldScene.vertices.begin(),
    worldScene.vertices.end(),
    [](const Vertex3D& vertex) { return vertex.color.alpha == 255U; }
  );
  if (duplicateTrianglesCulled > 0U) {
    std::cerr
      << "SDL_GPU static world duplicate triangles culled="
      << duplicateTrianglesCulled
      << " sourceTriangles=" << sourceTriangles
      << " renderedTriangles=" << (sourceTriangles - duplicateTrianglesCulled)
      << '\n';
  }

  const std::vector<std::uint32_t> referenced =
    referencedWorldMaterials(worldScene.vertices);
  mesh->referencedMaterials = static_cast<std::uint32_t>(referenced.size());
  mesh->textures.reserve(referenced.size() + 2U);
  mesh->textures.push_back(createFallbackWorldTexture(device, false));
  mesh->textures.push_back(createFallbackWorldTexture(device, true));
  WorldTexture* whiteTexture = &mesh->textures[0];
  WorldTexture* missingTexture = &mesh->textures[1];

  std::unordered_map<std::uint32_t, WorldTexture*> textureByMaterial;
  textureByMaterial.reserve(referenced.size() + 1U);
  const bool debugChecker = textureDebugCheckerEnabled();
  const std::filesystem::path root = basePath();
  std::vector<TextureMaterialFile> materialFiles;
  collectTextureMaterialFiles(root / "textures", materialFiles);
  for (std::uint32_t materialId : referenced) {
    const auto file = std::find_if(
      materialFiles.begin(),
      materialFiles.end(),
      [materialId](const TextureMaterialFile& candidate) {
        return arenaMaterialId(candidate.aliases[0]) == materialId ||
          arenaMaterialId(candidate.aliases[1]) == materialId;
      }
    );
    if (file == materialFiles.end() || debugChecker) {
      ++mesh->missingTextures;
      textureByMaterial[materialId] = missingTexture;
      continue;
    }
    WorldTexture texture = loadWorldTexture(device, *file);
    if (texture.texture == nullptr) {
      ++mesh->missingTextures;
      textureByMaterial[materialId] = missingTexture;
      continue;
    }
    mesh->textures.push_back(std::move(texture));
    WorldTexture* stored = &mesh->textures.back();
    textureByMaterial[materialId] = stored;
    ++mesh->loadedTextures;
    std::cerr
      << "SDL_GPU world texture material="
      << stored->material
      << " size=" << stored->width << 'x' << stored->height
      << " mipLevels=" << stored->mipLevels
      << " mipmaps=generated\n";
  }

  std::vector<WorldVisibilityTriangle> visibilityTriangles;
  visibilityTriangles.reserve(worldScene.vertices.size() / 3U);
  for (std::size_t index = 0; index + 2U < worldScene.vertices.size(); index += 3U) {
    visibilityTriangles.push_back({
      worldScene.vertices[index].position,
      worldScene.vertices[index + 1U].position,
      worldScene.vertices[index + 2U].position,
      worldScene.vertices[index].materialId,
    });
  }
  mesh->visibility = buildWorldVisibility(visibilityTriangles);

  const auto textureForMaterial = [&](std::uint32_t materialId) {
    if (materialId == 0U) {
      return whiteTexture;
    }
    WorldTexture* texture = missingTexture;
    if (const auto found = textureByMaterial.find(materialId); found != textureByMaterial.end()) {
      texture = found->second;
    }
    return texture;
  };

  std::vector<GpuVertex> gpuVertices;
  gpuVertices.reserve(worldScene.vertices.size());
  mesh->batches.reserve(referenced.size() + 1U);
  mesh->chunkBatches.reserve(mesh->visibility.chunks.size());
  mesh->visibleBatches.reserve(mesh->visibility.chunks.size());
  for (const WorldVisibilityChunk& chunk : mesh->visibility.chunks) {
    WorldTexture* texture = textureForMaterial(chunk.materialId);
    if (mesh->batches.empty() || mesh->batches.back().materialId != chunk.materialId) {
      mesh->batches.push_back({
        chunk.materialId,
        static_cast<Uint32>(gpuVertices.size()),
        0U,
        texture,
        texture->traits,
      });
    }
    const Uint32 firstVertex = static_cast<Uint32>(gpuVertices.size());
    const float width = static_cast<float>(std::max(1, texture->width));
    const float height = static_cast<float>(std::max(1, texture->height));
    for (std::uint32_t offset = 0; offset < chunk.triangleCount; ++offset) {
      const std::uint32_t triangleIndex =
        mesh->visibility.orderedTriangleIndices[chunk.firstTriangle + offset];
      const std::size_t firstSourceVertex = static_cast<std::size_t>(triangleIndex) * 3U;
      for (std::size_t vertexOffset = 0; vertexOffset < 3U; ++vertexOffset) {
        const Vertex3D& source = worldScene.vertices[firstSourceVertex + vertexOffset];
        gpuVertices.push_back(gpuVertex3D(source, source.u / width, source.v / height));
      }
    }
    const Uint32 vertexCount = static_cast<Uint32>(gpuVertices.size()) - firstVertex;
    mesh->chunkBatches.push_back({
      chunk.materialId,
      firstVertex,
      vertexCount,
      texture,
      texture->traits,
    });
    mesh->batches.back().vertexCount += vertexCount;
  }

  if (!updateStaticWorldSampler(device, mesh, settings)) {
    destroyStaticWorldMesh(device, mesh);
    return nullptr;
  }
  const Uint32 uploadSize =
    static_cast<Uint32>(gpuVertices.size() * sizeof(GpuVertex));
  const SDL_GPUBufferCreateInfo vertexBufferInfo = {
    SDL_GPU_BUFFERUSAGE_VERTEX,
    std::max<Uint32>(uploadSize, 1U),
    0,
  };
  mesh->vertexBuffer = SDL_CreateGPUBuffer(device, &vertexBufferInfo);
  if (mesh->vertexBuffer == nullptr) {
    destroyStaticWorldMesh(device, mesh);
    return nullptr;
  }
  if (!gpuVertices.empty()) {
    const SDL_GPUTransferBufferCreateInfo transferInfo = {
      SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
      uploadSize,
      0,
    };
    SDL_GPUTransferBuffer* transferBuffer =
      SDL_CreateGPUTransferBuffer(device, &transferInfo);
    void* mapped = transferBuffer != nullptr
      ? SDL_MapGPUTransferBuffer(device, transferBuffer, false)
      : nullptr;
    if (mapped == nullptr) {
      if (transferBuffer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
      }
      destroyStaticWorldMesh(device, mesh);
      return nullptr;
    }
    std::memcpy(mapped, gpuVertices.data(), uploadSize);
    SDL_UnmapGPUTransferBuffer(device, transferBuffer);
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
      ? SDL_BeginGPUCopyPass(commandBuffer)
      : nullptr;
    if (copyPass == nullptr) {
      if (commandBuffer != nullptr) {
        (void)SDL_CancelGPUCommandBuffer(commandBuffer);
      }
      SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
      destroyStaticWorldMesh(device, mesh);
      return nullptr;
    }
    const SDL_GPUTransferBufferLocation source = {transferBuffer, 0};
    const SDL_GPUBufferRegion destination = {mesh->vertexBuffer, 0, uploadSize};
    SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    SDL_EndGPUCopyPass(copyPass);
    const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    if (!submitted) {
      destroyStaticWorldMesh(device, mesh);
      return nullptr;
    }
  }

  mesh->buildCount = 1U;
  mesh->buildMilliseconds = millisecondsBetween(buildStart, RenderClock::now());
  if (textureDebugEnabled()) {
    std::cerr
      << "LG_DUEL_TEXTURE_PIPELINE_V2 static world summary"
      << " walls=" << arena.wallCount
      << " brushes=" << arena.brushCount
      << " sourceTriangles=" << mesh->sourceTriangles
      << " duplicateTrianglesCulled=" << mesh->duplicateTrianglesCulled
      << " staticVertices=" << mesh->vertexCount
      << " staticBatches=" << mesh->batches.size()
      << " referencedMaterials=" << mesh->referencedMaterials
      << " loadedTextures=" << mesh->loadedTextures
      << " missingTextures=" << mesh->missingTextures
      << " filter=" << textureFilterName(mesh->samplerTextureFilter)
      << " anisotropy=" << mesh->samplerAppliedTextureAnisotropy
      << " lodBias=" << mesh->samplerTextureLodBias
      << " buildMs=" << mesh->buildMilliseconds
      << '\n';
  }
  return mesh;
}

[[nodiscard]] StaticWorldMesh* ensureStaticWorldMesh(
  SDL_GPUDevice* device,
  StaticWorldMesh*& mesh,
  const Arena& arena,
  const RenderSettings& settings
) {
  const bool revisionCacheHit =
    mesh != nullptr &&
    settings.mapRevision != 0U &&
    mesh->arenaRevision == settings.mapRevision &&
    mesh->ambientQuality == settings.ambientGroundingQuality &&
    mesh->ambientDebugMode == settings.ambientDebugMode;
  const std::uint64_t fingerprint = revisionCacheHit
    ? mesh->arenaFingerprint
    : arenaStaticWorldFingerprint(arena);
  if (
    mesh != nullptr &&
    mesh->arenaFingerprint == fingerprint &&
    mesh->ambientQuality == settings.ambientGroundingQuality &&
    mesh->ambientDebugMode == settings.ambientDebugMode
  ) {
    mesh->arenaRevision = settings.mapRevision;
    (void)updateStaticWorldSampler(device, mesh, settings);
    return mesh;
  }
  destroyStaticWorldMesh(device, mesh);
  mesh = buildStaticWorldMesh(device, arena, settings);
  return mesh;
}

void updateStaticWorldVisibility(
  StaticWorldMesh& mesh,
  const PerspectiveCamera& camera,
  bool cullingEnabled
) {
  mesh.visibleBatches.clear();
  mesh.useCulledBatches = false;
  if (!cullingEnabled) {
    mesh.visibilityScratch.testedNodes = 0U;
    mesh.visibilityScratch.visibleChunkCount =
      static_cast<std::uint32_t>(mesh.chunkBatches.size());
    return;
  }

  queryWorldVisibility(mesh.visibility, camera, mesh.visibilityScratch);
  if (mesh.visibilityScratch.visibleChunkCount == mesh.chunkBatches.size()) {
    // The root-inside fast path commonly lands here. Aggregate material draws
    // are already optimal, so avoid rebuilding equivalent visible ranges.
    return;
  }
  // Chunk GPU ranges are already material-major. A linear scan both restores
  // draw order and merges spatial neighbors without allocating after warmup.
  for (std::size_t index = 0; index < mesh.chunkBatches.size(); ++index) {
    if (mesh.visibilityScratch.visibleChunks[index] == 0U) {
      continue;
    }
    const StaticWorldBatch& chunk = mesh.chunkBatches[index];
    if (
      !mesh.visibleBatches.empty() &&
      mesh.visibleBatches.back().materialId == chunk.materialId &&
      mesh.visibleBatches.back().texture == chunk.texture &&
      mesh.visibleBatches.back().firstVertex + mesh.visibleBatches.back().vertexCount ==
        chunk.firstVertex
    ) {
      mesh.visibleBatches.back().vertexCount += chunk.vertexCount;
    } else {
      mesh.visibleBatches.push_back(chunk);
    }
  }

  std::uint64_t visibleTriangles = 0U;
  for (const StaticWorldBatch& batch : mesh.visibleBatches) {
    visibleTriangles += batch.vertexCount / 3U;
  }
  std::uint64_t fullTriangles = 0U;
  for (const StaticWorldBatch& batch : mesh.batches) {
    fullTriangles += batch.vertexCount / 3U;
  }
  const std::uint64_t savedTriangles = fullTriangles - visibleTriangles;
  const std::uint64_t rangeInflation = mesh.visibleBatches.size() > mesh.batches.size()
    ? mesh.visibleBatches.size() - mesh.batches.size()
    : 0U;
  // Direct range submission is worthwhile only when it removes at least ten
  // percent of the world and repays each draw beyond the material baseline
  // with 800 culled triangles. Otherwise the five-ish aggregate draws win.
  mesh.useCulledBatches = fullTriangles > 0U &&
    savedTriangles * 10U >= fullTriangles &&
    savedTriangles >= 800U * rangeInflation;
}

[[nodiscard]] std::span<const StaticWorldBatch> staticWorldDrawBatches(
  const StaticWorldMesh& mesh,
  bool cullingEnabled
) {
  return cullingEnabled && mesh.useCulledBatches
    ? std::span<const StaticWorldBatch>(mesh.visibleBatches)
    : std::span<const StaticWorldBatch>(mesh.batches);
}

void drawStaticWorldGeometry(
  SDL_GPUCommandBuffer* commandBuffer,
  SDL_GPURenderPass* renderPass,
  StaticWorldMesh& mesh,
  bool cullingEnabled,
  RendererFrameDiagnostics* diagnostics,
  bool pushMaterialTraits = true
) {
  for (const StaticWorldBatch& batch : staticWorldDrawBatches(mesh, cullingEnabled)) {
    if (
      batch.vertexCount == 0U ||
      batch.texture == nullptr ||
      batch.texture->texture == nullptr
    ) {
      continue;
    }
    const SDL_GPUTextureSamplerBinding textureBinding = {
      batch.texture->texture,
      mesh.sampler,
    };
    const struct alignas(16) MaterialUniform {
      float traits[4];
    } materialUniform = {{
      batch.traits.roughness,
      batch.traits.metallic,
      batch.traits.specular,
      batch.traits.emissive,
    }};
    if (pushMaterialTraits) {
      SDL_PushGPUFragmentUniformData(
        commandBuffer,
        1,
        &materialUniform,
        sizeof(materialUniform)
      );
    }
    SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);
    SDL_DrawGPUPrimitives(
      renderPass,
      batch.vertexCount,
      1,
      batch.firstVertex,
      0
    );
    if (diagnostics != nullptr) {
      ++diagnostics->worldDrawCalls;
      ++diagnostics->worldSubmittedRanges;
      diagnostics->worldSubmittedTriangles += batch.vertexCount / 3U;
    }
  }
}

template <typename Vertex>
[[nodiscard]] bool uploadStaticVertices(
  SDL_GPUDevice* device,
  std::span<const Vertex> vertices,
  SDL_GPUBuffer*& buffer
) {
  const Uint32 uploadSize =
    static_cast<Uint32>(vertices.size() * sizeof(Vertex));
  const SDL_GPUBufferCreateInfo vertexBufferInfo = {
    SDL_GPU_BUFFERUSAGE_VERTEX,
    std::max<Uint32>(uploadSize, 1U),
    0,
  };
  buffer = SDL_CreateGPUBuffer(device, &vertexBufferInfo);
  if (buffer == nullptr) {
    return false;
  }
  if (vertices.empty()) {
    return true;
  }

  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    uploadSize,
    0,
  };
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
  void* mapped = transfer != nullptr
    ? SDL_MapGPUTransferBuffer(device, transfer, false)
    : nullptr;
  if (mapped == nullptr) {
    if (transfer != nullptr) {
      SDL_ReleaseGPUTransferBuffer(device, transfer);
    }
    SDL_ReleaseGPUBuffer(device, buffer);
    buffer = nullptr;
    return false;
  }
  std::memcpy(mapped, vertices.data(), uploadSize);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
    ? SDL_BeginGPUCopyPass(commandBuffer)
    : nullptr;
  if (copyPass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUBuffer(device, buffer);
    buffer = nullptr;
    return false;
  }
  const SDL_GPUTransferBufferLocation source = {transfer, 0};
  const SDL_GPUBufferRegion destination = {buffer, 0, uploadSize};
  SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
  SDL_EndGPUCopyPass(copyPass);
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  if (!submitted) {
    SDL_ReleaseGPUBuffer(device, buffer);
    buffer = nullptr;
  }
  return submitted;
}

[[nodiscard]] bool uploadStaticBytes(
  SDL_GPUDevice* device,
  const void* data,
  Uint32 uploadSize,
  SDL_GPUBufferUsageFlags usage,
  SDL_GPUBuffer*& buffer
) {
  if (data == nullptr || uploadSize == 0U) {
    return false;
  }
  const SDL_GPUBufferCreateInfo bufferInfo = {
    usage,
    uploadSize,
    0,
  };
  buffer = SDL_CreateGPUBuffer(device, &bufferInfo);
  if (buffer == nullptr) {
    return false;
  }
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    uploadSize,
    0,
  };
  SDL_GPUTransferBuffer* transfer =
    SDL_CreateGPUTransferBuffer(device, &transferInfo);
  void* mapped = transfer != nullptr
    ? SDL_MapGPUTransferBuffer(device, transfer, false)
    : nullptr;
  if (mapped == nullptr) {
    if (transfer != nullptr) {
      SDL_ReleaseGPUTransferBuffer(device, transfer);
    }
    SDL_ReleaseGPUBuffer(device, buffer);
    buffer = nullptr;
    return false;
  }
  std::memcpy(mapped, data, uploadSize);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copyPass = commandBuffer != nullptr
    ? SDL_BeginGPUCopyPass(commandBuffer)
    : nullptr;
  if (copyPass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUBuffer(device, buffer);
    buffer = nullptr;
    return false;
  }
  const SDL_GPUTransferBufferLocation source = {transfer, 0};
  const SDL_GPUBufferRegion destination = {buffer, 0, uploadSize};
  SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
  SDL_EndGPUCopyPass(copyPass);
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  if (!submitted) {
    SDL_ReleaseGPUBuffer(device, buffer);
    buffer = nullptr;
  }
  return submitted;
}

[[nodiscard]] GpuSimpleResources* createGpuSimpleResources(SDL_GPUDevice* device) {
  auto* resources = new GpuSimpleResources();
  const std::array<MeshHandle, 6> projectileMeshHandles = {{
    MeshHandle::PlasmaCore,
    MeshHandle::RocketProjectile,
    MeshHandle::GrenadeProjectile,
    MeshHandle::ExplosionCore,
    MeshHandle::MachineGunTracer,
    MeshHandle::ShotgunTracer,
  }};
  resources->projectileMeshes.reserve(projectileMeshHandles.size());
  for (MeshHandle handle : projectileMeshHandles) {
    const StaticMeshAsset* asset = staticMeshAsset(handle);
    if (asset == nullptr || asset->vertices.empty()) {
      destroyGpuSimpleResources(device, resources);
      return nullptr;
    }
    std::vector<GpuVertex> meshVertices;
    meshVertices.reserve(asset->vertices.size());
    for (const Vertex3D& vertex : asset->vertices) {
      meshVertices.push_back({
        vertex.position.x,
        vertex.position.y,
        vertex.position.z,
        vertex.color.red,
        vertex.color.green,
        vertex.color.blue,
        vertex.color.alpha,
        0.0F,
        0.0F,
      });
    }
    GpuStaticMesh mesh;
    mesh.opaque = std::all_of(
      asset->vertices.begin(),
      asset->vertices.end(),
      [](const Vertex3D& vertex) { return vertex.color.alpha == 255U; }
    );
    if (!uploadStaticVertices(device, std::span<const GpuVertex>(meshVertices), mesh.vertexBuffer)) {
      destroyGpuSimpleResources(device, resources);
      return nullptr;
    }
    mesh.vertexCount = static_cast<Uint32>(meshVertices.size());
    mesh.handle = handle;
    resources->projectileMeshes.push_back(mesh);
  }

  const std::array<GpuVertex, 6> glowQuad = {{
    {-1.0F, -1.0F, 0.0F, 255, 255, 255, 255, 0.0F, 0.0F},
    { 1.0F, -1.0F, 0.0F, 255, 255, 255, 255, 1.0F, 0.0F},
    { 1.0F,  1.0F, 0.0F, 255, 255, 255, 255, 1.0F, 1.0F},
    {-1.0F, -1.0F, 0.0F, 255, 255, 255, 255, 0.0F, 0.0F},
    { 1.0F,  1.0F, 0.0F, 255, 255, 255, 255, 1.0F, 1.0F},
    {-1.0F,  1.0F, 0.0F, 255, 255, 255, 255, 0.0F, 1.0F},
  }};
  const std::array<BillboardHandle, 5> projectileBillboardHandles = {{
    BillboardHandle::PlasmaGlow,
    BillboardHandle::RocketFlame,
    BillboardHandle::ExplosionFlash,
    BillboardHandle::ExplosionHalo,
    BillboardHandle::LightSource,
  }};
  resources->projectileBillboards.reserve(projectileBillboardHandles.size());
  for (BillboardHandle handle : projectileBillboardHandles) {
    GpuBillboardMesh billboard;
    if (!uploadStaticVertices(device, std::span<const GpuVertex>(glowQuad), billboard.vertexBuffer)) {
      destroyGpuSimpleResources(device, resources);
      return nullptr;
    }
    billboard.vertexCount = static_cast<Uint32>(glowQuad.size());
    billboard.handle = handle;
    resources->projectileBillboards.push_back(billboard);
  }
  const std::array<MeshHandle, 21> staticMeshHandles = {{
    MeshHandle::PlayerBoxCube,
    MeshHandle::RemoteMachineGunBody,
    MeshHandle::RemoteMachineGunBarrels,
    MeshHandle::RemoteShotgun,
    MeshHandle::RemoteGrenadeLauncher,
    MeshHandle::RemoteRocketLauncherBody,
    MeshHandle::RemoteRocketLauncherRecoil,
    MeshHandle::RemoteRocketLauncherLatch,
    MeshHandle::RemoteLightningGun,
    MeshHandle::RemoteFreezeGunBody,
    MeshHandle::RemoteFreezeGunFocus,
    MeshHandle::RemoteFreezeGunCoolant,
    MeshHandle::RemoteRailgun,
    MeshHandle::RemotePlasmaGunBody,
    MeshHandle::RemotePlasmaGunProngs,
    MeshHandle::RemotePlasmaGunCore,
    MeshHandle::RemoteRevolverBody,
    MeshHandle::RemoteRevolverCylinder,
    MeshHandle::ViewModelRightTriggerGrip,
    MeshHandle::ViewModelLeftClosedSupport,
    MeshHandle::ViewModelLeftOpenSupport,
  }};
  resources->staticMeshes.reserve(staticMeshHandles.size());
  for (MeshHandle handle : staticMeshHandles) {
    const StaticMeshAsset* asset = staticMeshAsset(handle);
    const MaterialMeshAsset* materialAsset = materialMeshAsset(handle);
    if (
      (asset == nullptr || asset->vertices.empty()) &&
      (materialAsset == nullptr || materialAsset->vertices.empty())
    ) {
      destroyGpuSimpleResources(device, resources);
      return nullptr;
    }
    GpuStaticMesh mesh;
    if (materialAsset != nullptr) {
      mesh.opaque = std::all_of(
        materialAsset->vertices.begin(),
        materialAsset->vertices.end(),
        [](const WeaponMaterialVertex3D& vertex) {
          return vertex.baseColor.alpha == 255U;
        }
      );
      std::vector<GpuMaterialVertex> vertices;
      vertices.reserve(materialAsset->vertices.size());
      for (const WeaponMaterialVertex3D& vertex : materialAsset->vertices) {
        vertices.push_back({
          {vertex.position.x, vertex.position.y, vertex.position.z},
          {vertex.normal.x, vertex.normal.y, vertex.normal.z},
          vertex.baseColor.red,
          vertex.baseColor.green,
          vertex.baseColor.blue,
          vertex.baseColor.alpha,
          vertex.metallic,
          vertex.roughness,
        });
      }
      if (!uploadStaticVertices(
        device,
        std::span<const GpuMaterialVertex>(vertices),
        mesh.vertexBuffer
      )) {
        destroyGpuSimpleResources(device, resources);
        return nullptr;
      }
      mesh.vertexCount = static_cast<Uint32>(vertices.size());
      mesh.materialLit = true;
    } else {
      mesh.opaque = std::all_of(
        asset->vertices.begin(),
        asset->vertices.end(),
        [](const Vertex3D& vertex) {
          return vertex.color.alpha == 255U;
        }
      );
      std::vector<GpuVertex> vertices;
      vertices.reserve(asset->vertices.size());
      for (const Vertex3D& vertex : asset->vertices) {
        vertices.push_back({
          vertex.position.x, vertex.position.y, vertex.position.z,
          vertex.color.red, vertex.color.green, vertex.color.blue, vertex.color.alpha,
          0.0F, 0.0F,
        });
      }
      if (!uploadStaticVertices(
        device,
        std::span<const GpuVertex>(vertices),
        mesh.vertexBuffer
      )) {
        destroyGpuSimpleResources(device, resources);
        return nullptr;
      }
      mesh.vertexCount = static_cast<Uint32>(vertices.size());
    }
    mesh.handle = handle;
    resources->staticMeshes.push_back(mesh);
  }
  resources->instances.staging.reserve(kMaxRocketProjectiles * 2U);
  resources->staticInstances.staging.reserve(kDuelPlayerCount * 8U + 1U);
  if (!createWeaponEnvironment(
    device,
    resources->weaponEnvironment,
    resources->weaponEnvironmentSampler
  )) {
    destroyGpuSimpleResources(device, resources);
    return nullptr;
  }
  return resources;
}

[[nodiscard]] SDL_GPUSampler* createGltfMaterialSampler(
  SDL_GPUDevice* device,
  std::uint32_t mipLevels
) {
  const float maxLod = mipLevels > 0U
    ? static_cast<float>(mipLevels - 1U)
    : 0.0F;
  const SDL_GPUSamplerCreateInfo samplerInfo = {
    SDL_GPU_FILTER_LINEAR,
    SDL_GPU_FILTER_LINEAR,
    SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    0.0F,
    maxLod,
    SDL_GPU_COMPAREOP_ALWAYS,
    0.0F,
    maxLod,
    false,
    false,
    0,
    0,
    0,
  };
  return SDL_CreateGPUSampler(device, &samplerInfo);
}

void createGltfMaterialFallbackTextures(
  SDL_GPUDevice* device,
  GpuGltfPlayerResources& resources
) {
  // These are deliberately neutral. The texture-free flat pipeline remains
  // the visible fallback, while the resources ensure a later descriptor-only
  // fallback cannot turn a missing material asset into black or emissive data.
  constexpr std::array<std::uint8_t, 4> kWhiteAlbedo = {{255U, 255U, 255U, 255U}};
  constexpr std::array<std::uint8_t, 4> kNeutralMask = {{0U, 217U, 0U, 0U}};
  SDL_GPUTexture* albedo = uploadRgbaTexture(
    device,
    kWhiteAlbedo.data(),
    1,
    1,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
  );
  SDL_GPUTexture* mask = uploadRgbaTexture(
    device,
    kNeutralMask.data(),
    1,
    1,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
  );
  SDL_GPUSampler* sampler = albedo != nullptr && mask != nullptr
    ? createGltfMaterialSampler(device, 1U)
    : nullptr;
  if (albedo == nullptr || mask == nullptr || sampler == nullptr) {
    if (sampler != nullptr) {
      SDL_ReleaseGPUSampler(device, sampler);
    }
    if (mask != nullptr) {
      SDL_ReleaseGPUTexture(device, mask);
    }
    if (albedo != nullptr) {
      SDL_ReleaseGPUTexture(device, albedo);
    }
    return;
  }
  resources.fallbackAlbedoTexture = albedo;
  resources.fallbackPackedMaskTexture = mask;
  resources.fallbackMaterialSampler = sampler;
  resources.materialFallbackTexturesReady = true;
  resources.materialTextureMipLevels = 1U;
  resources.materialTextureBytes = 8U;
}

[[nodiscard]] SDL_GPUTexture* loadGltfMaterialTexture(
  SDL_GPUDevice* device,
  const GltfMaterialTexture& description,
  SDL_GPUTextureFormat format,
  std::string& failure
) {
  if (
    description.path.empty() ||
    std::filesystem::path(description.path).extension() != ".png" ||
    !std::filesystem::is_regular_file(description.path)
  ) {
    failure = "texture file is missing or is not a PNG";
    return nullptr;
  }
  SDL_Surface* loaded = SDL_LoadPNG(description.path.c_str());
  if (loaded == nullptr) {
    failure = "SDL could not decode the PNG";
    return nullptr;
  }
  SDL_Surface* converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  if (converted == nullptr) {
    failure = "SDL could not convert the PNG to RGBA8";
    return nullptr;
  }
  if (
    converted->w != static_cast<int>(description.width) ||
    converted->h != static_cast<int>(description.height)
  ) {
    SDL_DestroySurface(converted);
    failure = "texture dimensions do not match the material manifest";
    return nullptr;
  }
  SDL_GPUTexture* texture = uploadRgbaTexture(
    device,
    static_cast<const std::uint8_t*>(converted->pixels),
    converted->w,
    converted->h,
    format
  );
  SDL_DestroySurface(converted);
  if (texture == nullptr) {
    failure = "the GPU rejected the material texture format or upload";
  }
  return texture;
}

void initializeGltfMaterialTextures(
  SDL_GPUDevice* device,
  const GltfSkinnedModel& model,
  GpuGltfPlayerResources& resources
) {
  const GltfMaterialMetadata& metadata = model.materialMetadata();
  resources.materialManifestHasAuthoredTextures = metadata.hasAuthoredTextures();
  resources.materialDiagnostic = metadata.diagnostic;
  if (!metadata.hasAuthoredTextures()) {
    return;
  }
  std::string albedoFailure;
  SDL_GPUTexture* albedo = loadGltfMaterialTexture(
    device,
    metadata.albedo,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
    albedoFailure
  );
  std::string maskFailure;
  SDL_GPUTexture* packedMask = loadGltfMaterialTexture(
    device,
    metadata.packedMask,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    maskFailure
  );
  const std::uint32_t mipLevels = std::max(
    gltfTextureMipLevels(metadata.albedo.width, metadata.albedo.height),
    gltfTextureMipLevels(metadata.packedMask.width, metadata.packedMask.height)
  );
  SDL_GPUSampler* sampler = albedo != nullptr && packedMask != nullptr
    ? createGltfMaterialSampler(device, mipLevels)
    : nullptr;
  if (albedo == nullptr || packedMask == nullptr || sampler == nullptr) {
    if (sampler != nullptr) {
      SDL_ReleaseGPUSampler(device, sampler);
    }
    if (packedMask != nullptr) {
      SDL_ReleaseGPUTexture(device, packedMask);
    }
    if (albedo != nullptr) {
      SDL_ReleaseGPUTexture(device, albedo);
    }
    resources.materialDiagnostic = albedoFailure.empty()
      ? (maskFailure.empty() ? "could not create the shared material sampler" : maskFailure)
      : albedoFailure;
    createGltfMaterialFallbackTextures(device, resources);
    if (resources.materialFallbackTexturesReady) {
      resources.materialDiagnostic += "; using neutral material fallbacks";
    }
    return;
  }
  resources.albedoTexture = albedo;
  resources.packedMaskTexture = packedMask;
  resources.materialSampler = sampler;
  resources.authoredMaterialTexturesReady = true;
  resources.materialTextureMipLevels = mipLevels;
  resources.materialTextureBytes = gltfMaterialResourcePlan(metadata).textureBytes;
  resources.materialDiagnostic = "loaded shared sRGB albedo and linear packed mask";
}

[[nodiscard]] GpuGltfPlayerResources* createGpuGltfPlayerResources(
  SDL_GPUDevice* device,
  const GltfSkinnedModel& model
) {
  if (!model.loaded() || model.primitives().empty()) {
    return new GpuGltfPlayerResources();
  }

  auto* resources = new GpuGltfPlayerResources();
  resources->sourcePath = std::string(model.sourcePath());
  initializeGltfMaterialTextures(device, model, *resources);
  std::cerr
    << "SDL_GPU GLTF material model=" << resources->sourcePath
    << " authoredTextures=" << (resources->authoredMaterialTexturesReady ? 1 : 0)
    << " mipLevels=" << resources->materialTextureMipLevels
    << " bytes=" << resources->materialTextureBytes
    << " detail=" << resources->materialDiagnostic << '\n';
  resources->primitives.reserve(model.primitives().size());
  for (const GltfSkinnedModel::Primitive& primitive : model.primitives()) {
    if (primitive.vertices.empty() || primitive.indices.empty()) {
      continue;
    }
    std::vector<GpuModelVertex> vertices;
    vertices.reserve(primitive.vertices.size());
    for (const GltfSkinnedModel::GpuVertex& source : primitive.vertices) {
      resources->opaque =
        resources->opaque && source.color.alpha == 255U;
      GpuModelVertex vertex;
      vertex.position[0] = source.position.x;
      vertex.position[1] = source.position.y;
      vertex.position[2] = source.position.z;
      vertex.normal[0] = source.normal.x;
      vertex.normal[1] = source.normal.y;
      vertex.normal[2] = source.normal.z;
      vertex.texCoord[0] = source.u;
      vertex.texCoord[1] = source.v;
      vertex.red = source.color.red;
      vertex.green = source.color.green;
      vertex.blue = source.color.blue;
      vertex.alpha = source.color.alpha;
      vertex.tintWeight = source.tintWeight;
      vertex.albedoTextureMode = source.albedoTextureMode;
      for (std::size_t index = 0; index < 4U; ++index) {
        vertex.joints[index] = source.joints[index];
        vertex.weights[index] = source.weights[index];
      }
      vertices.push_back(vertex);
    }

    GpuGltfPrimitive gpuPrimitive;
    gpuPrimitive.vertexBytes =
      static_cast<Uint32>(vertices.size() * sizeof(GpuModelVertex));
    gpuPrimitive.indexBytes =
      static_cast<Uint32>(primitive.indices.size() * sizeof(std::uint32_t));
    gpuPrimitive.indexCount = static_cast<Uint32>(primitive.indices.size());
    if (
      !uploadStaticBytes(
        device,
        vertices.data(),
        gpuPrimitive.vertexBytes,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        gpuPrimitive.vertexBuffer
      ) ||
      !uploadStaticBytes(
        device,
        primitive.indices.data(),
        gpuPrimitive.indexBytes,
        SDL_GPU_BUFFERUSAGE_INDEX,
        gpuPrimitive.indexBuffer
      )
    ) {
      destroyGpuGltfPlayerResources(device, resources);
      return nullptr;
    }
    resources->staticVertexBytes += gpuPrimitive.vertexBytes;
    resources->staticIndexBytes += gpuPrimitive.indexBytes;
    resources->primitives.push_back(gpuPrimitive);
  }
  resources->instanceStaging.reserve(kDuelPlayerCount);
  resources->boneStaging.reserve(
    kDuelPlayerCount * std::max<std::uint32_t>(1U, model.jointCount())
  );
  return resources;
}

[[nodiscard]] bool ensureGpuInstanceCapacity(
  SDL_GPUDevice* device,
  GpuInstanceBuffer& buffer,
  Uint32 required
) {
  if (required <= buffer.capacity) {
    return true;
  }
  Uint32 capacity = std::max<Uint32>(8U, buffer.capacity);
  while (capacity < required) {
    capacity *= 2U;
  }
  destroyGpuInstanceBuffer(device, buffer);
  const Uint32 byteSize = capacity * static_cast<Uint32>(sizeof(GpuSimpleInstance));
  const SDL_GPUBufferCreateInfo bufferInfo = {
    SDL_GPU_BUFFERUSAGE_VERTEX,
    byteSize,
    0,
  };
  buffer.buffer = SDL_CreateGPUBuffer(device, &bufferInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    byteSize,
    0,
  };
  buffer.transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
  if (buffer.buffer == nullptr || buffer.transfer == nullptr) {
    destroyGpuInstanceBuffer(device, buffer);
    return false;
  }
  buffer.capacity = capacity;
  buffer.staging.reserve(capacity);
  return true;
}

[[nodiscard]] bool uploadSimpleInstances(
  SDL_GPUDevice* device,
  SDL_GPUCommandBuffer* commandBuffer,
  GpuInstanceBuffer& buffer,
  const Scene3D& scene
) {
  buffer.staging.clear();
  buffer.staging.reserve(scene.simpleInstances.size());
  for (const SimpleRenderInstance& instance : scene.simpleInstances) {
    buffer.staging.push_back({
      {instance.position.x, instance.position.y, instance.position.z},
      {instance.scale.x, instance.scale.y, instance.scale.z},
      instance.rotationRadians,
      instance.pitchRadians,
      instance.color.red,
      instance.color.green,
      instance.color.blue,
      instance.color.alpha,
      instance.visualPhase,
    });
  }
  if (buffer.staging.empty()) {
    return true;
  }
  const Uint32 instanceCount = static_cast<Uint32>(buffer.staging.size());
  if (!ensureGpuInstanceCapacity(device, buffer, instanceCount)) {
    return false;
  }
  const Uint32 uploadSize =
    instanceCount * static_cast<Uint32>(sizeof(GpuSimpleInstance));
  void* mapped = SDL_MapGPUTransferBuffer(device, buffer.transfer, true);
  if (mapped == nullptr) {
    return false;
  }
  std::memcpy(mapped, buffer.staging.data(), uploadSize);
  SDL_UnmapGPUTransferBuffer(device, buffer.transfer);
  SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
  if (copyPass == nullptr) {
    return false;
  }
  const SDL_GPUTransferBufferLocation source = {buffer.transfer, 0};
  const SDL_GPUBufferRegion destination = {buffer.buffer, 0, uploadSize};
  SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
  SDL_EndGPUCopyPass(copyPass);
  return true;
}

[[nodiscard]] bool ensureGpuStaticInstanceCapacity(
  SDL_GPUDevice* device,
  GpuStaticInstanceBuffer& buffer,
  Uint32 required
) {
  if (required <= buffer.capacity) {
    return true;
  }
  Uint32 capacity = std::max<Uint32>(8U, buffer.capacity);
  while (capacity < required) {
    capacity *= 2U;
  }
  destroyGpuStaticInstanceBuffer(device, buffer);
  const Uint32 byteSize = capacity * static_cast<Uint32>(sizeof(GpuStaticInstance));
  const SDL_GPUBufferCreateInfo bufferInfo = {
    SDL_GPU_BUFFERUSAGE_VERTEX,
    byteSize,
    0,
  };
  buffer.buffer = SDL_CreateGPUBuffer(device, &bufferInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    byteSize,
    0,
  };
  buffer.transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
  if (buffer.buffer == nullptr || buffer.transfer == nullptr) {
    destroyGpuStaticInstanceBuffer(device, buffer);
    return false;
  }
  buffer.capacity = capacity;
  buffer.staging.reserve(capacity);
  return true;
}

[[nodiscard]] bool uploadStaticMeshInstances(
  SDL_GPUDevice* device,
  SDL_GPUCommandBuffer* commandBuffer,
  GpuStaticInstanceBuffer& buffer,
  const Scene3D& scene,
  const StaticAmbientProbeGrid* ambientProbeGrid,
  int ambientDebugMode,
  std::uint32_t* sampledInstances
) {
  buffer.staging.clear();
  buffer.staging.reserve(scene.staticMeshInstances.size());
  for (const StaticMeshInstance& instance : scene.staticMeshInstances) {
    const bool grounded =
      instance.pass == RenderPass::OpaqueWorld &&
      ambientProbeGrid != nullptr &&
      ambientProbeGrid->enabled();
    const std::uint8_t ambientVisibility = grounded
      ? sampleStaticAmbientProbe(*ambientProbeGrid, instance.modelTranslation)
      : 255U;
    if (grounded && sampledInstances != nullptr) {
      ++*sampledInstances;
    }
    buffer.staging.push_back({
      {
        instance.modelRow0.x,
        instance.modelRow0.y,
        instance.modelRow0.z,
        instance.modelTranslation.x,
      },
      {
        instance.modelRow1.x,
        instance.modelRow1.y,
        instance.modelRow1.z,
        instance.modelTranslation.y,
      },
      {
        instance.modelRow2.x,
        instance.modelRow2.y,
        instance.modelRow2.z,
        instance.modelTranslation.z,
      },
      instance.color.red,
      instance.color.green,
      instance.color.blue,
      instance.color.alpha,
      ambientVisibility,
      static_cast<std::uint8_t>(
        ambientDebugMode >= 2 && grounded ? 255U : 0U
      ),
      {0U, 0U},
    });
  }
  if (buffer.staging.empty()) {
    return true;
  }
  const Uint32 instanceCount = static_cast<Uint32>(buffer.staging.size());
  if (!ensureGpuStaticInstanceCapacity(device, buffer, instanceCount)) {
    return false;
  }
  const Uint32 uploadSize =
    instanceCount * static_cast<Uint32>(sizeof(GpuStaticInstance));
  void* mapped = SDL_MapGPUTransferBuffer(device, buffer.transfer, true);
  if (mapped == nullptr) {
    return false;
  }
  std::memcpy(mapped, buffer.staging.data(), uploadSize);
  SDL_UnmapGPUTransferBuffer(device, buffer.transfer);
  SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
  if (copyPass == nullptr) {
    return false;
  }
  const SDL_GPUTransferBufferLocation source = {buffer.transfer, 0};
  const SDL_GPUBufferRegion destination = {buffer.buffer, 0, uploadSize};
  SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
  SDL_EndGPUCopyPass(copyPass);
  return true;
}

[[nodiscard]] bool ensureGpuGltfInstanceCapacity(
  SDL_GPUDevice* device,
  GpuGltfPlayerResources& resources,
  Uint32 required
) {
  if (required <= resources.instanceCapacity) {
    return true;
  }
  Uint32 capacity = std::max<Uint32>(8U, resources.instanceCapacity);
  while (capacity < required) {
    capacity *= 2U;
  }
  if (resources.instanceTransfer != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, resources.instanceTransfer);
    resources.instanceTransfer = nullptr;
  }
  if (resources.instanceBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, resources.instanceBuffer);
    resources.instanceBuffer = nullptr;
  }
  const Uint32 byteSize =
    capacity * static_cast<Uint32>(sizeof(GpuGltfPlayerInstance));
  const SDL_GPUBufferCreateInfo bufferInfo = {
    SDL_GPU_BUFFERUSAGE_VERTEX,
    byteSize,
    0,
  };
  resources.instanceBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    byteSize,
    0,
  };
  resources.instanceTransfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
  if (resources.instanceBuffer == nullptr || resources.instanceTransfer == nullptr) {
    return false;
  }
  resources.instanceCapacity = capacity;
  resources.instanceStaging.reserve(capacity);
  return true;
}

[[nodiscard]] bool ensureGpuGltfBoneCapacity(
  SDL_GPUDevice* device,
  GpuGltfPlayerResources& resources,
  Uint32 requiredRows
) {
  if (requiredRows <= resources.boneCapacityRows) {
    return true;
  }
  Uint32 capacity = std::max<Uint32>(64U, resources.boneCapacityRows);
  while (capacity < requiredRows) {
    capacity *= 2U;
  }
  if (resources.boneTransfer != nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, resources.boneTransfer);
    resources.boneTransfer = nullptr;
  }
  if (resources.boneBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, resources.boneBuffer);
    resources.boneBuffer = nullptr;
  }
  const Uint32 byteSize = capacity * static_cast<Uint32>(sizeof(float) * 4U);
  const SDL_GPUBufferCreateInfo bufferInfo = {
    SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
    byteSize,
    0,
  };
  resources.boneBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);
  const SDL_GPUTransferBufferCreateInfo transferInfo = {
    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    byteSize,
    0,
  };
  resources.boneTransfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
  if (resources.boneBuffer == nullptr || resources.boneTransfer == nullptr) {
    return false;
  }
  resources.boneCapacityRows = capacity;
  return true;
}

[[nodiscard]] bool uploadGltfPlayerFrameData(
  SDL_GPUDevice* device,
  SDL_GPUCommandBuffer* commandBuffer,
  GpuGltfPlayerResources& resources,
  const Scene3D& scene,
  const StaticAmbientProbeGrid* ambientProbeGrid,
  int ambientDebugMode,
  std::uint32_t* sampledInstances
) {
  resources.instanceStaging.clear();
  resources.instanceStaging.reserve(scene.gltfPlayerModelInstances.size());
  for (const GltfPlayerModelInstance& instance : scene.gltfPlayerModelInstances) {
    const bool grounded =
      ambientProbeGrid != nullptr && ambientProbeGrid->enabled();
    const std::uint8_t ambientVisibility = grounded
      ? sampleStaticAmbientProbe(*ambientProbeGrid, instance.modelTranslation)
      : 255U;
    if (grounded && sampledInstances != nullptr) {
      ++*sampledInstances;
    }
    resources.instanceStaging.push_back({
      {
        instance.modelRow0.x,
        instance.modelRow0.y,
        instance.modelRow0.z,
        instance.modelTranslation.x,
      },
      {
        instance.modelRow1.x,
        instance.modelRow1.y,
        instance.modelRow1.z,
        instance.modelTranslation.y,
      },
      {
        instance.modelRow2.x,
        instance.modelRow2.y,
        instance.modelRow2.z,
        instance.modelTranslation.z,
      },
      instance.color.red,
      instance.color.green,
      instance.color.blue,
      instance.color.alpha,
      instance.firstBone,
      instance.boneCount,
      instance.skinned ? 1U : 0U,
      ambientVisibility,
      static_cast<std::uint8_t>(
        ambientDebugMode >= 2 && grounded ? 255U : 0U
      ),
      {0U, 0U, 0U, 0U, 0U, 0U},
    });
  }
  if (!resources.instanceStaging.empty()) {
    const Uint32 instanceCount =
      static_cast<Uint32>(resources.instanceStaging.size());
    if (!ensureGpuGltfInstanceCapacity(device, resources, instanceCount)) {
      return false;
    }
    const Uint32 uploadSize =
      instanceCount * static_cast<Uint32>(sizeof(GpuGltfPlayerInstance));
    void* mapped = SDL_MapGPUTransferBuffer(device, resources.instanceTransfer, true);
    if (mapped == nullptr) {
      return false;
    }
    std::memcpy(mapped, resources.instanceStaging.data(), uploadSize);
    SDL_UnmapGPUTransferBuffer(device, resources.instanceTransfer);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr) {
      return false;
    }
    const SDL_GPUTransferBufferLocation source = {resources.instanceTransfer, 0};
    const SDL_GPUBufferRegion destination = {resources.instanceBuffer, 0, uploadSize};
    SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
    SDL_EndGPUCopyPass(copyPass);
  }

  if (!scene.gltfBonePalette.empty()) {
    const Uint32 matrixCount = static_cast<Uint32>(scene.gltfBonePalette.size());
    const Uint32 rowCount = matrixCount * 4U;
    if (!ensureGpuGltfBoneCapacity(device, resources, rowCount)) {
      return false;
    }
    const Uint32 uploadSize =
      matrixCount * static_cast<Uint32>(sizeof(std::array<float, 16>));
    void* mapped = SDL_MapGPUTransferBuffer(device, resources.boneTransfer, true);
    if (mapped == nullptr) {
      return false;
    }
    std::memcpy(mapped, scene.gltfBonePalette.data(), uploadSize);
    SDL_UnmapGPUTransferBuffer(device, resources.boneTransfer);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr) {
      return false;
    }
    const SDL_GPUTransferBufferLocation source = {resources.boneTransfer, 0};
    const SDL_GPUBufferRegion destination = {resources.boneBuffer, 0, uploadSize};
    SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
    SDL_EndGPUCopyPass(copyPass);
  }
  return true;
}

[[nodiscard]] const GpuStaticMesh* findStaticMesh(
  const GpuSimpleResources& resources,
  MeshHandle handle
) {
  for (const GpuStaticMesh& mesh : resources.staticMeshes) {
    if (mesh.handle == handle) {
      return &mesh;
    }
  }
  return nullptr;
}

[[nodiscard]] const GpuStaticMesh* findProjectileMesh(
  const GpuSimpleResources& resources,
  MeshHandle handle
) {
  for (const GpuStaticMesh& mesh : resources.projectileMeshes) {
    if (mesh.handle == handle) {
      return &mesh;
    }
  }
  return nullptr;
}

[[nodiscard]] const char* directPresentFallbackReasonName(
  DirectPresentFallbackReason reason
) {
  switch (reason) {
  case DirectPresentFallbackReason::None:
    return "none";
  case DirectPresentFallbackReason::ColorGrade:
    return "color-grade";
  case DirectPresentFallbackReason::Exposure:
    return "exposure";
  case DirectPresentFallbackReason::DisplayGamma:
    return "display-gamma";
  case DirectPresentFallbackReason::AntiAliasing:
    return "anti-aliasing";
  case DirectPresentFallbackReason::Bloom:
    return "bloom";
  case DirectPresentFallbackReason::SunShadow:
    return "sun-shadow";
  case DirectPresentFallbackReason::QualityContract:
    return "quality-contract";
  case DirectPresentFallbackReason::LivePointLights:
    return "live-point-lights";
  case DirectPresentFallbackReason::OutlineMode:
    return "outline-mode";
  case DirectPresentFallbackReason::ContactShadows:
    return "contact-shadows";
  case DirectPresentFallbackReason::TranslucentVertices:
    return "translucent-vertices";
  case DirectPresentFallbackReason::TranslucentEffects:
    return "translucent-effects";
  case DirectPresentFallbackReason::SimpleBatchPass:
    return "simple-batch-pass";
  case DirectPresentFallbackReason::ActiveTextureAlpha:
    return "active-texture-alpha";
  case DirectPresentFallbackReason::ActiveVertexAlpha:
    return "active-vertex-alpha";
  case DirectPresentFallbackReason::ActiveInstanceAlpha:
    return "active-instance-alpha";
  case DirectPresentFallbackReason::PlayerAlpha:
    return "player-alpha";
  case DirectPresentFallbackReason::ViewModelAlpha:
    return "viewmodel-alpha";
  case DirectPresentFallbackReason::SwapchainFormat:
    return "swapchain-format";
  case DirectPresentFallbackReason::Pipelines:
    return "pipelines";
  }
  return "unknown";
}

[[nodiscard]] bool directPresentSwapchainFormatSupported(
  SDL_GPUTextureFormat format
) {
  return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
    format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
}

[[nodiscard]] bool verticesOpaque(std::span<const Vertex3D> vertices) {
  return std::all_of(
    vertices.begin(),
    vertices.end(),
    [](const Vertex3D& vertex) {
      return vertex.color.alpha == 255U;
    }
  );
}

[[nodiscard]] bool simpleBatchesOpaque(const Scene3D& scene) {
  return std::all_of(
    scene.simpleBatches.begin(),
    scene.simpleBatches.end(),
    [](const SimpleRenderBatch& batch) {
      return batch.instanceCount == 0U ||
        batch.pass == RenderPass::OpaqueWorld;
    }
  );
}

[[nodiscard]] bool activeWorldTexturesOpaque(
  const StaticWorldMesh& mesh,
  bool cullingEnabled
) {
  const std::span<const StaticWorldBatch> batches =
    staticWorldDrawBatches(mesh, cullingEnabled);
  return std::all_of(
    batches.begin(),
    batches.end(),
    [](const StaticWorldBatch& batch) {
      return batch.vertexCount == 0U ||
        (batch.texture != nullptr && batch.texture->opaque);
    }
  );
}

[[nodiscard]] bool activeStaticMeshAssetsOpaque(
  const GpuSimpleResources& resources,
  const Scene3D& scene
) {
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    if (batch.instanceCount == 0U) {
      continue;
    }
    if (
      batch.pass != RenderPass::OpaqueWorld &&
      batch.pass != RenderPass::ViewModel
    ) {
      return false;
    }
    const GpuStaticMesh* mesh = findStaticMesh(resources, batch.mesh);
    if (mesh == nullptr || !mesh->opaque) {
      return false;
    }
  }
  for (const SimpleRenderBatch& batch : scene.simpleBatches) {
    if (batch.instanceCount == 0U) {
      continue;
    }
    const GpuStaticMesh* mesh = findProjectileMesh(resources, batch.mesh);
    if (mesh == nullptr || !mesh->opaque) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool activeInstanceColorsOpaque(const Scene3D& scene) {
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    const std::size_t end = static_cast<std::size_t>(batch.firstInstance) +
      batch.instanceCount;
    if (end > scene.staticMeshInstances.size()) {
      return false;
    }
    for (std::size_t index = batch.firstInstance; index < end; ++index) {
      if (scene.staticMeshInstances[index].color.alpha != 255U) {
        return false;
      }
    }
  }
  for (const SimpleRenderBatch& batch : scene.simpleBatches) {
    const std::size_t end = static_cast<std::size_t>(batch.firstInstance) +
      batch.instanceCount;
    if (end > scene.simpleInstances.size()) {
      return false;
    }
    for (std::size_t index = batch.firstInstance; index < end; ++index) {
      if (scene.simpleInstances[index].color.alpha != 255U) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool activePlayersOpaque(
  const GpuGltfPlayerResources& resources,
  const Scene3D& scene
) {
  if (!scene.gltfPlayerModelBatches.empty() && !resources.opaque) {
    return false;
  }
  for (const GltfPlayerModelBatch& batch : scene.gltfPlayerModelBatches) {
    const std::size_t end = static_cast<std::size_t>(batch.firstInstance) +
      batch.instanceCount;
    if (end > scene.gltfPlayerModelInstances.size()) {
      return false;
    }
    for (std::size_t index = batch.firstInstance; index < end; ++index) {
      if (scene.gltfPlayerModelInstances[index].color.alpha != 255U) {
        return false;
      }
    }
  }
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    const std::size_t end = static_cast<std::size_t>(batch.firstInstance) +
      batch.instanceCount;
    if (end > scene.staticMeshInstances.size()) {
      return false;
    }
    for (std::size_t index = batch.firstInstance; index < end; ++index) {
      const StaticMeshInstance& instance = scene.staticMeshInstances[index];
      if (
        (
          instance.playerBodyPart != PlayerBodyPartType::None ||
          instance.playerBoxBody
        ) &&
        instance.color.alpha != 255U
      ) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool activeViewModelOpaque(
  const GpuSimpleResources& resources,
  const Scene3D& scene
) {
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    if (
      batch.instanceCount == 0U ||
      batch.pass != RenderPass::ViewModel
    ) {
      continue;
    }
    const GpuStaticMesh* mesh = findStaticMesh(resources, batch.mesh);
    if (mesh == nullptr || !mesh->opaque) {
      return false;
    }
    const std::size_t end = static_cast<std::size_t>(batch.firstInstance) +
      batch.instanceCount;
    if (end > scene.staticMeshInstances.size()) {
      return false;
    }
    for (std::size_t index = batch.firstInstance; index < end; ++index) {
      if (scene.staticMeshInstances[index].color.alpha != 255U) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] const GpuBillboardMesh* findProjectileBillboard(
  const GpuSimpleResources& resources,
  BillboardHandle handle
) {
  for (const GpuBillboardMesh& billboard : resources.projectileBillboards) {
    if (billboard.handle == handle) {
      return &billboard;
    }
  }
  return nullptr;
}

void drawStaticMeshBatches(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* pipeline,
  SDL_GPUGraphicsPipeline* materialPipeline,
  GpuSimpleResources* resources,
  const Scene3D& scene,
  RenderPass passFilter,
  bool bindMaterialEnvironment = true,
  const SDL_GPUTextureSamplerBinding* simplePointShadowBinding = nullptr,
  bool shadowCastersOnly = false
) {
  if (
    resources == nullptr ||
    resources->staticInstances.buffer == nullptr ||
    pipeline == nullptr || materialPipeline == nullptr ||
    scene.staticMeshBatches.empty()
  ) {
    return;
  }
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    if (batch.instanceCount == 0U || batch.pass != passFilter) {
      continue;
    }
    const GpuStaticMesh* mesh = findStaticMesh(*resources, batch.mesh);
    if (mesh == nullptr || mesh->vertexBuffer == nullptr || mesh->vertexCount == 0U) {
      continue;
    }
    const auto drawRange = [&](std::uint32_t firstInstance,
                               std::uint32_t instanceCount) {
      if (instanceCount == 0U) {
        return;
      }
      SDL_BindGPUGraphicsPipeline(
        pass,
        mesh->materialLit ? materialPipeline : pipeline
      );
      if (mesh->materialLit && bindMaterialEnvironment) {
        const SDL_GPUTextureSamplerBinding environmentBinding = {
          resources->weaponEnvironment,
          resources->weaponEnvironmentSampler,
        };
        SDL_BindGPUFragmentSamplers(pass, 0, &environmentBinding, 1);
      } else if (!mesh->materialLit && simplePointShadowBinding != nullptr) {
        SDL_BindGPUFragmentSamplers(
          pass,
          0,
          simplePointShadowBinding,
          1
        );
      }
      const std::array<SDL_GPUBufferBinding, 2> bindings = {{
        {mesh->vertexBuffer, 0},
        {
          resources->staticInstances.buffer,
          firstInstance * static_cast<Uint32>(sizeof(GpuStaticInstance)),
        },
      }};
      SDL_BindGPUVertexBuffers(
        pass,
        0,
        bindings.data(),
        static_cast<Uint32>(bindings.size())
      );
      SDL_DrawGPUPrimitives(pass, mesh->vertexCount, instanceCount, 0, 0);
    };
    if (!shadowCastersOnly) {
      drawRange(batch.firstInstance, batch.instanceCount);
      continue;
    }
    std::uint32_t runFirst = batch.firstInstance;
    std::uint32_t runCount = 0U;
    for (
      std::uint32_t index = batch.firstInstance;
      index < batch.firstInstance + batch.instanceCount;
      ++index
    ) {
      if (scene.staticMeshInstances[index].castsSunShadow) {
        if (runCount == 0U) {
          runFirst = index;
        }
        ++runCount;
      } else {
        drawRange(runFirst, runCount);
        runCount = 0U;
      }
    }
    drawRange(runFirst, runCount);
  }
}

void drawStaticMeshInstanceRange(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* simplePipeline,
  SDL_GPUGraphicsPipeline* materialPipeline,
  GpuSimpleResources* resources,
  MeshHandle meshHandle,
  std::uint32_t firstInstance,
  std::uint32_t instanceCount
) {
  if (
    resources == nullptr ||
    resources->staticInstances.buffer == nullptr ||
    instanceCount == 0U
  ) {
    return;
  }
  const GpuStaticMesh* mesh = findStaticMesh(*resources, meshHandle);
  if (mesh == nullptr || mesh->vertexBuffer == nullptr || mesh->vertexCount == 0U) {
    return;
  }
  SDL_GPUGraphicsPipeline* pipeline =
    mesh->materialLit ? materialPipeline : simplePipeline;
  if (pipeline == nullptr) {
    return;
  }
  SDL_BindGPUGraphicsPipeline(pass, pipeline);
  const std::array<SDL_GPUBufferBinding, 2> bindings = {{
    {mesh->vertexBuffer, 0},
    {
      resources->staticInstances.buffer,
      firstInstance * static_cast<Uint32>(sizeof(GpuStaticInstance)),
    },
  }};
  SDL_BindGPUVertexBuffers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
  SDL_DrawGPUPrimitives(pass, mesh->vertexCount, instanceCount, 0, 0);
}

void drawGltfPlayerModelBatches(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* pipeline,
  GpuGltfPlayerResources* resources,
  const Scene3D& scene,
  bool bindAuthoredMaterialTextures = false,
  bool shadowCastersOnly = false
) {
  if (
    pass == nullptr ||
    pipeline == nullptr ||
    resources == nullptr ||
    resources->instanceBuffer == nullptr ||
    resources->boneBuffer == nullptr ||
    scene.gltfPlayerModelBatches.empty()
  ) {
    return;
  }
  SDL_BindGPUGraphicsPipeline(pass, pipeline);
  SDL_GPUBuffer* storageBuffers[] = {resources->boneBuffer};
  SDL_BindGPUVertexStorageBuffers(pass, 0, storageBuffers, 1);
  if (
    bindAuthoredMaterialTextures &&
    resources->authoredMaterialTexturesReady &&
    resources->albedoTexture != nullptr &&
    resources->packedMaskTexture != nullptr &&
    resources->materialSampler != nullptr
  ) {
    const std::array<SDL_GPUTextureSamplerBinding, 2> materialBindings = {{
      {resources->albedoTexture, resources->materialSampler},
      {resources->packedMaskTexture, resources->materialSampler},
    }};
    SDL_BindGPUFragmentSamplers(
      pass,
      0,
      materialBindings.data(),
      static_cast<Uint32>(materialBindings.size())
    );
  }
  for (const GltfPlayerModelBatch& batch : scene.gltfPlayerModelBatches) {
    if (
      batch.primitiveIndex >= resources->primitives.size() ||
      batch.instanceCount == 0U
    ) {
      continue;
    }
    const GpuGltfPrimitive& primitive =
      resources->primitives[batch.primitiveIndex];
    if (
      primitive.vertexBuffer == nullptr ||
      primitive.indexBuffer == nullptr ||
      primitive.indexCount == 0U
    ) {
      continue;
    }
    const auto drawRange = [&](std::uint32_t firstInstance,
                               std::uint32_t instanceCount) {
      if (instanceCount == 0U) {
        return;
      }
      const std::array<SDL_GPUBufferBinding, 2> vertexBindings = {{
        {primitive.vertexBuffer, 0},
        {
          resources->instanceBuffer,
          firstInstance * static_cast<Uint32>(sizeof(GpuGltfPlayerInstance)),
        },
      }};
      const SDL_GPUBufferBinding indexBinding = {primitive.indexBuffer, 0};
      SDL_BindGPUVertexBuffers(
        pass,
        0,
        vertexBindings.data(),
        static_cast<Uint32>(vertexBindings.size())
      );
      SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
      SDL_DrawGPUIndexedPrimitives(
        pass,
        primitive.indexCount,
        instanceCount,
        0,
        0,
        0
      );
    };
    if (!shadowCastersOnly) {
      drawRange(batch.firstInstance, batch.instanceCount);
      continue;
    }
    std::uint32_t runFirst = batch.firstInstance;
    std::uint32_t runCount = 0U;
    for (
      std::uint32_t index = batch.firstInstance;
      index < batch.firstInstance + batch.instanceCount;
      ++index
    ) {
      if (scene.gltfPlayerModelInstances[index].castsSunShadow) {
        if (runCount == 0U) {
          runFirst = index;
        }
        ++runCount;
      } else {
        drawRange(runFirst, runCount);
        runCount = 0U;
      }
    }
    drawRange(runFirst, runCount);
  }
}

void drawGltfPlayerModelInstanceRange(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* pipeline,
  GpuGltfPlayerResources* resources,
  std::uint32_t firstInstance,
  std::uint32_t instanceCount
) {
  if (
    pass == nullptr ||
    pipeline == nullptr ||
    resources == nullptr ||
    resources->instanceBuffer == nullptr ||
    resources->boneBuffer == nullptr ||
    instanceCount == 0U
  ) {
    return;
  }
  SDL_BindGPUGraphicsPipeline(pass, pipeline);
  SDL_GPUBuffer* storageBuffers[] = {resources->boneBuffer};
  SDL_BindGPUVertexStorageBuffers(pass, 0, storageBuffers, 1);
  for (const GpuGltfPrimitive& primitive : resources->primitives) {
    if (
      primitive.vertexBuffer == nullptr ||
      primitive.indexBuffer == nullptr ||
      primitive.indexCount == 0U
    ) {
      continue;
    }
    const std::array<SDL_GPUBufferBinding, 2> vertexBindings = {{
      {primitive.vertexBuffer, 0},
      {
        resources->instanceBuffer,
        firstInstance * static_cast<Uint32>(sizeof(GpuGltfPlayerInstance)),
      },
    }};
    const SDL_GPUBufferBinding indexBinding = {primitive.indexBuffer, 0};
    SDL_BindGPUVertexBuffers(
      pass,
      0,
      vertexBindings.data(),
      static_cast<Uint32>(vertexBindings.size())
    );
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(
      pass,
      primitive.indexCount,
      instanceCount,
      0,
      0,
      0
    );
  }
}

void drawSimpleInstanceBatches(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* meshPipeline,
  SDL_GPUGraphicsPipeline* glowPipeline,
  GpuSimpleResources* resources,
  const Scene3D& scene
) {
  if (
    resources == nullptr ||
    resources->instances.buffer == nullptr ||
    scene.simpleBatches.empty()
  ) {
    return;
  }

  for (const SimpleRenderBatch& batch : scene.simpleBatches) {
    if (batch.instanceCount == 0U) {
      continue;
    }
    if (
      batch.mesh != MeshHandle::Invalid &&
      (
        batch.pass == RenderPass::OpaqueWorld ||
        batch.pass == RenderPass::TranslucentWorld
      )
    ) {
      const GpuStaticMesh* mesh = findProjectileMesh(*resources, batch.mesh);
      if (
        meshPipeline == nullptr ||
        mesh == nullptr ||
        mesh->vertexBuffer == nullptr ||
        mesh->vertexCount == 0U
      ) {
        continue;
      }
      SDL_BindGPUGraphicsPipeline(pass, meshPipeline);
      const std::array<SDL_GPUBufferBinding, 2> bindings = {{
        {mesh->vertexBuffer, 0},
        {
          resources->instances.buffer,
          batch.firstInstance * static_cast<Uint32>(sizeof(GpuSimpleInstance)),
        },
      }};
      SDL_BindGPUVertexBuffers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
      SDL_DrawGPUPrimitives(
        pass,
        mesh->vertexCount,
        batch.instanceCount,
        0,
        0
      );
    } else if (
      batch.billboard != BillboardHandle::Invalid &&
      batch.pass == RenderPass::AdditiveGlow
    ) {
      const GpuBillboardMesh* billboard =
        findProjectileBillboard(*resources, batch.billboard);
      if (
        glowPipeline == nullptr ||
        billboard == nullptr ||
        billboard->vertexBuffer == nullptr ||
        billboard->vertexCount == 0U
      ) {
        continue;
      }
      SDL_BindGPUGraphicsPipeline(pass, glowPipeline);
      const std::array<SDL_GPUBufferBinding, 2> bindings = {{
        {billboard->vertexBuffer, 0},
        {
          resources->instances.buffer,
          batch.firstInstance * static_cast<Uint32>(sizeof(GpuSimpleInstance)),
        },
      }};
      SDL_BindGPUVertexBuffers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
      SDL_DrawGPUPrimitives(
        pass,
        billboard->vertexCount,
        batch.instanceCount,
        0,
        0
      );
    }
  }
}

void drawStaticMeshDepthBatches(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* staticPipeline,
  SDL_GPUGraphicsPipeline* materialPipeline,
  GpuSimpleResources* resources,
  const Scene3D& scene
) {
  if (
    pass == nullptr ||
    staticPipeline == nullptr ||
    materialPipeline == nullptr ||
    resources == nullptr ||
    resources->staticInstances.buffer == nullptr
  ) {
    return;
  }
  for (const StaticMeshBatch& batch : scene.staticMeshBatches) {
    if (
      batch.instanceCount == 0U ||
      batch.pass != RenderPass::OpaqueWorld
    ) {
      continue;
    }
    const GpuStaticMesh* mesh = findStaticMesh(*resources, batch.mesh);
    if (
      mesh == nullptr ||
      mesh->vertexBuffer == nullptr ||
      mesh->vertexCount == 0U
    ) {
      continue;
    }
    SDL_BindGPUGraphicsPipeline(
      pass,
      mesh->materialLit ? materialPipeline : staticPipeline
    );
    const std::array<SDL_GPUBufferBinding, 2> bindings = {{
      {mesh->vertexBuffer, 0},
      {
        resources->staticInstances.buffer,
        batch.firstInstance * static_cast<Uint32>(sizeof(GpuStaticInstance)),
      },
    }};
    SDL_BindGPUVertexBuffers(
      pass,
      0,
      bindings.data(),
      static_cast<Uint32>(bindings.size())
    );
    SDL_DrawGPUPrimitives(
      pass,
      mesh->vertexCount,
      batch.instanceCount,
      0,
      0
    );
  }
}

void drawSimpleGlowBatches(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* glowPipeline,
  GpuSimpleResources* resources,
  const Scene3D& scene
) {
  if (
    glowPipeline == nullptr ||
    resources == nullptr ||
    resources->instances.buffer == nullptr
  ) {
    return;
  }
  SDL_BindGPUGraphicsPipeline(pass, glowPipeline);
  for (const SimpleRenderBatch& batch : scene.simpleBatches) {
    if (
      batch.instanceCount == 0U ||
      batch.billboard == BillboardHandle::Invalid ||
      batch.pass != RenderPass::AdditiveGlow
    ) {
      continue;
    }
    const GpuBillboardMesh* billboard =
      findProjectileBillboard(*resources, batch.billboard);
    if (
      billboard == nullptr ||
      billboard->vertexBuffer == nullptr ||
      billboard->vertexCount == 0U
    ) {
      continue;
    }
    const std::array<SDL_GPUBufferBinding, 2> bindings = {{
      {billboard->vertexBuffer, 0},
      {
        resources->instances.buffer,
        batch.firstInstance * static_cast<Uint32>(sizeof(GpuSimpleInstance)),
      },
    }};
    SDL_BindGPUVertexBuffers(
      pass,
      0,
      bindings.data(),
      static_cast<Uint32>(bindings.size())
    );
    SDL_DrawGPUPrimitives(
      pass,
      billboard->vertexCount,
      batch.instanceCount,
      0,
      0
    );
  }
}

void drawSimpleOpaqueDepthBatches(
  SDL_GPURenderPass* pass,
  SDL_GPUGraphicsPipeline* depthPipeline,
  GpuSimpleResources* resources,
  const Scene3D& scene
) {
  if (
    pass == nullptr ||
    depthPipeline == nullptr ||
    resources == nullptr ||
    resources->instances.buffer == nullptr
  ) {
    return;
  }
  SDL_BindGPUGraphicsPipeline(pass, depthPipeline);
  for (const SimpleRenderBatch& batch : scene.simpleBatches) {
    if (
      batch.instanceCount == 0U ||
      batch.mesh == MeshHandle::Invalid ||
      batch.pass != RenderPass::OpaqueWorld
    ) {
      continue;
    }
    const GpuStaticMesh* mesh = findProjectileMesh(*resources, batch.mesh);
    if (
      mesh == nullptr ||
      mesh->vertexBuffer == nullptr ||
      mesh->vertexCount == 0U
    ) {
      continue;
    }
    const std::array<SDL_GPUBufferBinding, 2> bindings = {{
      {mesh->vertexBuffer, 0},
      {
        resources->instances.buffer,
        batch.firstInstance * static_cast<Uint32>(sizeof(GpuSimpleInstance)),
      },
    }};
    SDL_BindGPUVertexBuffers(
      pass,
      0,
      bindings.data(),
      static_cast<Uint32>(bindings.size())
    );
    SDL_DrawGPUPrimitives(
      pass,
      mesh->vertexCount,
      batch.instanceCount,
      0,
      0
    );
  }
}

void copyGltfPlayerModelDiagnostics(
  RendererFrameDiagnostics& diagnostics,
  const Scene3D& scene
) {
  diagnostics.gltfPlayerModelInstances =
    scene.gltfPlayerModelStats.activeInstances;
  diagnostics.gltfPlayerModelFrustumCulled =
    scene.gltfPlayerModelStats.frustumCulled;
  diagnostics.gltfStaticMeshGpuBytes =
    scene.gltfPlayerModelStats.staticMeshGpuBytes;
  diagnostics.gltfStaticIndexGpuBytes =
    scene.gltfPlayerModelStats.staticIndexGpuBytes;
  diagnostics.gltfPoseUploadBytes =
    scene.gltfPlayerModelStats.poseUploadBytes;
  diagnostics.gltfBonePaletteEntriesUploaded =
    scene.gltfPlayerModelStats.bonePaletteEntriesUploaded;
  diagnostics.gltfRigidFallbackInstances =
    scene.gltfPlayerModelStats.rigidFallbackInstances;
  diagnostics.gltfGpuSkinnedInstances =
    scene.gltfPlayerModelStats.gpuSkinnedInstances;
  diagnostics.gltfBodyBatches =
    scene.gltfPlayerModelStats.bodyBatches;
  diagnostics.gltfBodyDrawCalls =
    scene.gltfPlayerModelStats.bodyDrawCalls;
  diagnostics.gltfShadowCasterInstances =
    scene.gltfPlayerModelStats.shadowCasterInstances;
  diagnostics.gltfShadowCasterDrawCalls =
    scene.gltfPlayerModelStats.shadowCasterDrawCalls;
  diagnostics.gltfOutlineMaskBatches =
    scene.gltfPlayerModelStats.outlineMaskBatches;
  diagnostics.gltfOutlineMaskDrawCalls =
    scene.gltfPlayerModelStats.outlineMaskDrawCalls;
  diagnostics.legacyCpuSkinnedGltfVertexUploadBytes =
    scene.gltfPlayerModelStats.legacyCpuSkinnedVertexUploadBytes;
}

[[nodiscard]] SDL_GPUTexture* ensureDepthTexture(
  SDL_GPUDevice* device,
  SDL_GPUTexture* texture,
  Uint32& textureWidth,
  Uint32& textureHeight,
  Uint32 outputWidth,
  Uint32 outputHeight,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1,
  bool sampled = false
) {
  if (
    texture != nullptr &&
    textureWidth == outputWidth &&
    textureHeight == outputHeight
  ) {
    return texture;
  }
  if (texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    depthFormat,
    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
      (sampled ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0U),
    outputWidth,
    outputHeight,
    1,
    1,
    sampleCount,
    0,
  };
  textureWidth = outputWidth;
  textureHeight = outputHeight;
  return SDL_CreateGPUTexture(device, &createInfo);
}

[[nodiscard]] SDL_GPUTexture* ensureMsaaColorTexture(
  SDL_GPUDevice* device,
  SDL_GPUTexture* texture,
  Uint32& textureWidth,
  Uint32& textureHeight,
  Uint32 outputWidth,
  Uint32 outputHeight,
  SDL_GPUTextureFormat format,
  SDL_GPUSampleCount sampleCount
) {
  if (
    sampleCount == SDL_GPU_SAMPLECOUNT_1 ||
    (
      texture != nullptr &&
      textureWidth == outputWidth &&
      textureHeight == outputHeight
    )
  ) {
    return sampleCount == SDL_GPU_SAMPLECOUNT_1 ? nullptr : texture;
  }
  if (texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    format,
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
    outputWidth,
    outputHeight,
    1,
    1,
    sampleCount,
    0,
  };
  textureWidth = outputWidth;
  textureHeight = outputHeight;
  return SDL_CreateGPUTexture(device, &createInfo);
}

[[nodiscard]] SDL_GPUTexture* ensureSampledColorTexture(
  SDL_GPUDevice* device,
  SDL_GPUTexture* texture,
  Uint32& textureWidth,
  Uint32& textureHeight,
  Uint32 outputWidth,
  Uint32 outputHeight,
  SDL_GPUTextureFormat format
) {
  if (
    texture != nullptr &&
    textureWidth == outputWidth &&
    textureHeight == outputHeight
  ) {
    return texture;
  }
  if (texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    format,
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
    outputWidth,
    outputHeight,
    1,
    1,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  textureWidth = outputWidth;
  textureHeight = outputHeight;
  return SDL_CreateGPUTexture(device, &createInfo);
}

[[nodiscard]] SDL_GPUTexture* ensureSunShadowTexture(
  SDL_GPUDevice* device,
  SDL_GPUTexture* texture,
  Uint32& textureSize,
  Uint32 requestedSize,
  SDL_GPUTextureFormat depthFormat
) {
  const Uint32 size = std::max(1U, requestedSize);
  if (texture != nullptr && textureSize == size) {
    return texture;
  }
  if (texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    depthFormat,
    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
    size,
    size,
    1,
    1,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  textureSize = size;
  return SDL_CreateGPUTexture(device, &createInfo);
}

[[nodiscard]] SDL_GPUTexture* createClearedSunShadowFallbackTexture(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat
) {
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    depthFormat,
    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
    1,
    1,
    1,
    1,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &createInfo);
  SDL_GPUCommandBuffer* commandBuffer = texture != nullptr
    ? SDL_AcquireGPUCommandBuffer(device)
    : nullptr;
  SDL_GPUDepthStencilTargetInfo target = {};
  target.texture = texture;
  target.clear_depth = 1.0F;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  SDL_GPURenderPass* pass = commandBuffer != nullptr
    ? SDL_BeginGPURenderPass(commandBuffer, nullptr, 0, &target)
    : nullptr;
  if (pass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    if (texture != nullptr) {
      SDL_ReleaseGPUTexture(device, texture);
    }
    return nullptr;
  }
  SDL_EndGPURenderPass(pass);
  if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  return texture;
}

[[nodiscard]] SDL_GPUTexture* ensurePointShadowTexture(
  SDL_GPUDevice* device,
  SDL_GPUTexture* texture,
  Uint32& textureSize,
  Uint32& lightCount,
  Uint32 requestedSize,
  Uint32 requestedLightCount,
  SDL_GPUTextureFormat depthFormat
) {
  const Uint32 size = std::max(1U, requestedSize);
  const Uint32 count = std::min<Uint32>(
    static_cast<Uint32>(kMaxPointShadowLights),
    requestedLightCount
  );
  if (
    texture != nullptr &&
    textureSize == size &&
    lightCount == count
  ) {
    return texture;
  }
  if (texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D_ARRAY,
    depthFormat,
    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
    size,
    size,
    std::max(1U, count * 6U),
    1,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  textureSize = size;
  lightCount = count;
  return SDL_CreateGPUTexture(device, &createInfo);
}

[[nodiscard]] SDL_GPUTexture* createClearedPointShadowFallbackTexture(
  SDL_GPUDevice* device,
  SDL_GPUTextureFormat depthFormat
) {
  Uint32 size = 0;
  Uint32 lights = 0;
  SDL_GPUTexture* texture = ensurePointShadowTexture(
    device,
    nullptr,
    size,
    lights,
    1U,
    0U,
    depthFormat
  );
  SDL_GPUCommandBuffer* commandBuffer = texture != nullptr
    ? SDL_AcquireGPUCommandBuffer(device)
    : nullptr;
  SDL_GPUDepthStencilTargetInfo target = {};
  target.texture = texture;
  target.clear_depth = 1.0F;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  SDL_GPURenderPass* pass = commandBuffer != nullptr
    ? SDL_BeginGPURenderPass(commandBuffer, nullptr, 0, &target)
    : nullptr;
  if (pass == nullptr) {
    if (commandBuffer != nullptr) {
      (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    }
    if (texture != nullptr) {
      SDL_ReleaseGPUTexture(device, texture);
    }
    return nullptr;
  }
  SDL_EndGPURenderPass(pass);
  if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  return texture;
}

[[nodiscard]] SDL_GPUTexture* ensureOutlineMaskTexture(
  SDL_GPUDevice* device,
  SDL_GPUTexture* texture,
  Uint32& textureWidth,
  Uint32& textureHeight,
  Uint32 outputWidth,
  Uint32 outputHeight
) {
  if (
    texture != nullptr &&
    textureWidth == outputWidth &&
    textureHeight == outputHeight
  ) {
    return texture;
  }
  if (texture != nullptr) {
    SDL_ReleaseGPUTexture(device, texture);
  }
  const SDL_GPUTextureCreateInfo createInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
    outputWidth,
    outputHeight,
    1,
    1,
    SDL_GPU_SAMPLECOUNT_1,
    0,
  };
  textureWidth = outputWidth;
  textureHeight = outputHeight;
  return SDL_CreateGPUTexture(device, &createInfo);
}

struct SniperScopeMeshVertex {
  ScreenPoint position = {};
  RenderColor color = {};
};

struct CachedSniperScopeMesh {
  int outputWidth = 0;
  int outputHeight = 0;
  ScreenPoint center = {};
  float radius = 0.0F;
  std::vector<SniperScopeMeshVertex> vertices;
  std::vector<int> indices;
  std::vector<SDL_Vertex> fallbackVertices;
};

void appendSniperScopeMeshQuad(
  CachedSniperScopeMesh& mesh,
  const std::array<ScreenPoint, 4>& points,
  const std::array<RenderColor, 4>& colors
) {
  const int first = static_cast<int>(mesh.vertices.size());
  for (std::size_t index = 0; index < points.size(); ++index) {
    mesh.vertices.push_back({points[index], colors[index]});
  }
  mesh.indices.insert(
    mesh.indices.end(),
    {first, first + 1, first + 2, first, first + 2, first + 3}
  );
}

void appendSniperScopeMeshQuad(
  CachedSniperScopeMesh& mesh,
  const std::array<ScreenPoint, 4>& points,
  RenderColor color
) {
  appendSniperScopeMeshQuad(mesh, points, {color, color, color, color});
}

void rebuildSniperScopeMesh(
  CachedSniperScopeMesh& mesh,
  const SniperScopeOverlay2D& scope
) {
  constexpr int kSegments = 96;
  constexpr float kTwoPi = 6.28318530718F;
  constexpr float kOuterRingScale = 1.45F;
  constexpr int kVignetteBands = 2;

  mesh.outputWidth = scope.outputWidth;
  mesh.outputHeight = scope.outputHeight;
  mesh.center = scope.center;
  mesh.radius = scope.radius;
  mesh.vertices.clear();
  mesh.indices.clear();
  mesh.vertices.reserve((4U + 4U * kSegments) * 4U);
  mesh.indices.reserve((4U + 4U * kSegments) * 6U);

  const float width = static_cast<float>(scope.outputWidth);
  const float height = static_cast<float>(scope.outputHeight);
  const float left = scope.center.x - scope.radius;
  const float right = scope.center.x + scope.radius;
  const float top = scope.center.y - scope.radius;
  const float bottom = scope.center.y + scope.radius;
  const RenderColor outside = {0, 0, 0, 255};

  // Four large rectangles cover the area beyond the lens bounding square.
  appendSniperScopeMeshQuad(
    mesh,
    {{{0.0F, 0.0F}, {width, 0.0F}, {width, top}, {0.0F, top}}},
    outside
  );
  appendSniperScopeMeshQuad(
    mesh,
    {{{0.0F, bottom}, {width, bottom}, {width, height}, {0.0F, height}}},
    outside
  );
  appendSniperScopeMeshQuad(
    mesh,
    {{{0.0F, top}, {left, top}, {left, bottom}, {0.0F, bottom}}},
    outside
  );
  appendSniperScopeMeshQuad(
    mesh,
    {{{right, top}, {width, top}, {width, bottom}, {right, bottom}}},
    outside
  );

  const auto pointAt = [&](float angle, float radius) {
    return ScreenPoint{
      scope.center.x + std::cos(angle) * radius,
      scope.center.y + std::sin(angle) * radius,
    };
  };
  const auto appendRing = [&](
    float outerRadius,
    float innerRadius,
    RenderColor outerColor,
    RenderColor innerColor
  ) {
    for (int segment = 0; segment < kSegments; ++segment) {
      const float angle0 =
        kTwoPi * static_cast<float>(segment) / static_cast<float>(kSegments);
      const float angle1 =
        kTwoPi * static_cast<float>(segment + 1) /
        static_cast<float>(kSegments);
      appendSniperScopeMeshQuad(
        mesh,
        {{
          pointAt(angle0, outerRadius),
          pointAt(angle1, outerRadius),
          pointAt(angle1, innerRadius),
          pointAt(angle0, innerRadius),
        }},
        {outerColor, outerColor, innerColor, innerColor}
      );
    }
  };

  // This ring fills the corners between the lens and its bounding square.
  appendRing(
    scope.radius * kOuterRingScale,
    scope.radius,
    outside,
    outside
  );

  const float vignetteWidth = std::min(72.0F, scope.radius * 0.14F);
  for (int band = 0; band < kVignetteBands; ++band) {
    const float outerFraction =
      static_cast<float>(band) / static_cast<float>(kVignetteBands);
    const float innerFraction =
      static_cast<float>(band + 1) / static_cast<float>(kVignetteBands);
    const float outerFade = 1.0F - outerFraction;
    const float innerFade = 1.0F - innerFraction;
    appendRing(
      scope.radius - vignetteWidth * outerFraction,
      scope.radius - vignetteWidth * innerFraction,
      {
        0,
        0,
        0,
        static_cast<std::uint8_t>(105.0F * outerFade * outerFade),
      },
      {
        0,
        0,
        0,
        static_cast<std::uint8_t>(105.0F * innerFade * innerFade),
      }
    );
  }

  constexpr float kRimHalfWidth = 1.5F;
  const RenderColor rim = {112, 94, 66, 220};
  appendRing(
    scope.radius + kRimHalfWidth,
    scope.radius - kRimHalfWidth,
    rim,
    rim
  );
  mesh.fallbackVertices.resize(mesh.vertices.size());
}

[[nodiscard]] CachedSniperScopeMesh& sniperScopeMesh(
  const SniperScopeOverlay2D& scope
) {
  static CachedSniperScopeMesh mesh;
  if (
    mesh.outputWidth != scope.outputWidth ||
    mesh.outputHeight != scope.outputHeight ||
    mesh.center.x != scope.center.x ||
    mesh.center.y != scope.center.y ||
    mesh.radius != scope.radius
  ) {
    rebuildSniperScopeMesh(mesh, scope);
  }
  return mesh;
}

[[nodiscard]] ScreenPoint transformedSniperScopePoint(
  const SniperScopeOverlay2D& scope,
  ScreenPoint point
) {
  return {
    scope.center.x + (point.x - scope.center.x) * scope.openingScale,
    scope.center.y + (point.y - scope.center.y) * scope.openingScale,
  };
}

[[nodiscard]] RenderColor modulatedSniperScopeColor(
  RenderColor color,
  float opacity
) {
  color.alpha = static_cast<std::uint8_t>(std::clamp(
    static_cast<float>(color.alpha) * std::clamp(opacity, 0.0F, 1.0F),
    0.0F,
    255.0F
  ));
  return color;
}

void appendSniperScopeOverlay(
  std::vector<GpuVertex>& vertices,
  const SniperScopeOverlay2D& scope,
  float outputWidth,
  float outputHeight
) {
  const CachedSniperScopeMesh& mesh = sniperScopeMesh(scope);
  for (int index : mesh.indices) {
    const SniperScopeMeshVertex& vertex =
      mesh.vertices[static_cast<std::size_t>(index)];
    vertices.push_back(gpuVertex(
      transformedSniperScopePoint(scope, vertex.position),
      modulatedSniperScopeColor(vertex.color, scope.opacity),
      outputWidth,
      outputHeight
    ));
  }
}

void appendTriangle(
  std::vector<GpuVertex>& vertices,
  ScreenPoint first,
  ScreenPoint second,
  ScreenPoint third,
  RenderColor color,
  float outputWidth,
  float outputHeight,
  const std::array<ScreenPoint, 3>& texturePoints = {{
    {kSolidTextureU, kSolidTextureV},
    {kSolidTextureU, kSolidTextureV},
    {kSolidTextureU, kSolidTextureV},
  }}
) {
  vertices.push_back(gpuVertex(
    first,
    color,
    outputWidth,
    outputHeight,
    texturePoints[0].x,
    texturePoints[0].y
  ));
  vertices.push_back(gpuVertex(
    second,
    color,
    outputWidth,
    outputHeight,
    texturePoints[1].x,
    texturePoints[1].y
  ));
  vertices.push_back(gpuVertex(
    third,
    color,
    outputWidth,
    outputHeight,
    texturePoints[2].x,
    texturePoints[2].y
  ));
}

void appendQuad(
  std::vector<GpuVertex>& vertices,
  const std::array<ScreenPoint, 4>& points,
  RenderColor color,
  float outputWidth,
  float outputHeight
) {
  appendTriangle(
    vertices,
    points[0],
    points[1],
    points[2],
    color,
    outputWidth,
    outputHeight
  );
  appendTriangle(
    vertices,
    points[0],
    points[2],
    points[3],
    color,
    outputWidth,
    outputHeight
  );
}

void appendLine(
  std::vector<GpuVertex>& vertices,
  const Line2D& line,
  float outputWidth,
  float outputHeight
) {
  const float deltaX = line.end.x - line.start.x;
  const float deltaY = line.end.y - line.start.y;
  const float lineLength = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
  const float halfWidth = std::max(0.5F, line.width * 0.5F);
  const float normalX =
    lineLength > 0.0001F ? (-deltaY / lineLength) * halfWidth : halfWidth;
  const float normalY =
    lineLength > 0.0001F ? (deltaX / lineLength) * halfWidth : 0.0F;
  const std::array<ScreenPoint, 4> points = {{
    {line.start.x + normalX, line.start.y + normalY},
    {line.end.x + normalX, line.end.y + normalY},
    {line.end.x - normalX, line.end.y - normalY},
    {line.start.x - normalX, line.start.y - normalY},
  }};
  appendQuad(
    vertices,
    points,
    line.color,
    outputWidth,
    outputHeight
  );
}

void appendText(
  std::vector<GpuVertex>& vertices,
  const Text2D& text,
  const FontAtlas& fontAtlas,
  float outputWidth,
  float outputHeight
) {
  const float drawScale =
    fontAtlas.nominalPixelHeight / fontAtlas.baseScaleDenominator;
  const auto lineWidthAt = [&](std::size_t startIndex) {
    float width = 0.0F;
    for (std::size_t index = startIndex; index < text.text.size();) {
      const auto rawCharacter = static_cast<unsigned char>(text.text[index]);
      if (rawCharacter == '\n') {
        break;
      }
      std::size_t byteLength = 1U;
      const std::uint32_t character =
        fontAtlasCodepointAt(text.text, index, byteLength);
      auto glyphIt = fontAtlas.glyphs.find(character);
      if (glyphIt == fontAtlas.glyphs.end()) {
        glyphIt = fontAtlas.glyphs.find('?');
      }
      if (glyphIt != fontAtlas.glyphs.end()) {
        width += glyphIt->second.advance * drawScale;
      }
      index += byteLength;
    }
    return width;
  };
  const auto alignedLineX = [&](std::size_t startIndex) {
    switch (text.horizontalAlignment) {
    case TextHorizontalAlignment::Center:
      return text.position.x - lineWidthAt(startIndex) * 0.5F;
    case TextHorizontalAlignment::Right:
      return text.position.x - lineWidthAt(startIndex);
    case TextHorizontalAlignment::Left:
      break;
    }
    return text.position.x;
  };
  std::size_t lineStart = 0U;
  float x = alignedLineX(lineStart);
  float y = text.position.y;
  const float lineHeight = fontAtlas.lineHeight * drawScale;
  for (std::size_t index = 0; index < text.text.size();) {
    const auto rawCharacter = static_cast<unsigned char>(text.text[index]);
    if (rawCharacter == '\n') {
      y += lineHeight;
      ++index;
      lineStart = index;
      x = alignedLineX(lineStart);
      continue;
    }
    std::size_t byteLength = 1U;
    std::uint32_t character = fontAtlasCodepointAt(text.text, index, byteLength);
    auto glyphIt = fontAtlas.glyphs.find(character);
    if (glyphIt == fontAtlas.glyphs.end()) {
      glyphIt = fontAtlas.glyphs.find('?');
    }
    if (glyphIt != fontAtlas.glyphs.end()) {
      const FontGlyph& glyph = glyphIt->second;
      if (!glyph.drawable) {
        x += glyph.advance * drawScale;
        index += byteLength;
        continue;
      }
      const float x0 = std::round(x + glyph.xOffset * drawScale);
      const float y0 = std::round(y + glyph.yOffset * drawScale);
      const float x1 = x0 + glyph.width * drawScale;
      const float y1 = y0 + glyph.height * drawScale;
      const std::array<ScreenPoint, 4> points = {{
        {x0, y0},
        {x1, y0},
        {x1, y1},
        {x0, y1},
      }};
      appendTriangle(
        vertices,
        points[0],
        points[1],
        points[2],
        text.color,
        outputWidth,
        outputHeight,
        {{{glyph.u0, glyph.v0}, {glyph.u1, glyph.v0}, {glyph.u1, glyph.v1}}}
      );
      appendTriangle(
        vertices,
        points[0],
        points[2],
        points[3],
        text.color,
        outputWidth,
        outputHeight,
        {{{glyph.u0, glyph.v0}, {glyph.u1, glyph.v1}, {glyph.u0, glyph.v1}}}
      );
      x += glyph.advance * drawScale;
    }
    index += byteLength;
  }
}

void closeOverlayDrawBatch(
  std::vector<OverlayDrawBatch>& batches,
  FontAtlas* fontAtlas,
  Uint32 firstVertex,
  Uint32 endVertex
) {
  if (fontAtlas == nullptr || endVertex <= firstVertex) {
    return;
  }
  batches.push_back(OverlayDrawBatch{
    fontAtlas,
    firstVertex,
    endVertex - firstVertex,
  });
}

void appendCommandBatches(
  std::vector<GpuVertex>& vertices,
  std::vector<OverlayDrawBatch>& batches,
  const std::vector<DrawCommand2D>& commands,
  FontAtlasSet& fontAtlasSet,
  float outputWidth,
  float outputHeight
) {
  FontAtlas* activeFontAtlas = nullptr;
  Uint32 batchFirstVertex = static_cast<Uint32>(vertices.size());
  const auto switchBatch = [&](FontAtlas* nextFontAtlas) {
    const Uint32 currentEnd = static_cast<Uint32>(vertices.size());
    if (activeFontAtlas == nextFontAtlas) {
      return;
    }
    closeOverlayDrawBatch(
      batches,
      activeFontAtlas,
      batchFirstVertex,
      currentEnd
    );
    activeFontAtlas = nextFontAtlas;
    batchFirstVertex = currentEnd;
  };

  FontAtlas* defaultFontAtlas =
    fontAtlasSet.atlases[kDefaultUiFontPixelHeightIndex];
  for (const DrawCommand2D& command : commands) {
    FontAtlas* commandFontAtlas =
      activeFontAtlas != nullptr ? activeFontAtlas : defaultFontAtlas;
    if (const auto* text = std::get_if<Text2D>(&command)) {
      commandFontAtlas = fontAtlasForTextScale(fontAtlasSet, text->scale);
    }
    if (commandFontAtlas == nullptr) {
      continue;
    }
    switchBatch(commandFontAtlas);
    std::visit(
      [&](const auto& primitive) {
        using Primitive = std::decay_t<decltype(primitive)>;
        if constexpr (std::is_same_v<Primitive, FilledQuad2D>) {
          appendQuad(
            vertices,
            primitive.points,
            primitive.color,
            outputWidth,
            outputHeight
          );
        } else if constexpr (std::is_same_v<Primitive, Line2D>) {
          appendLine(vertices, primitive, outputWidth, outputHeight);
        } else if constexpr (
          std::is_same_v<Primitive, SniperScopeOverlay2D>
        ) {
          appendSniperScopeOverlay(
            vertices,
            primitive,
            outputWidth,
            outputHeight
          );
        } else if constexpr (std::is_same_v<Primitive, Text2D>) {
          appendText(
            vertices,
            primitive,
            *commandFontAtlas,
            outputWidth,
            outputHeight
          );
        }
      },
      command
    );
  }
  closeOverlayDrawBatch(
    batches,
    activeFontAtlas,
    batchFirstVertex,
    static_cast<Uint32>(vertices.size())
  );
}

[[nodiscard]] PerspectiveCamera playerPerspectiveCamera(
  const PlayerState& player,
  float aspectRatio,
  float fieldOfView
) {
  constexpr CollisionBounds kDefaultPlayerBounds = {};
  const float eyeHeight =
    0.65F *
    (player.bounds.halfHeight / kDefaultPlayerBounds.halfHeight);
  return makePerspectiveCamera(
    player.position + Vec3{0.0F, 0.0F, eyeHeight},
    player.viewYawRadians,
    player.viewPitchRadians,
    fieldOfView,
    aspectRatio
  );
}

[[nodiscard]] bool renderGpuFrame(
  SDL_GPUDevice* device,
  SDL_GPUGraphicsPipeline* pipeline2D,
  SDL_GPUGraphicsPipeline* pipelineWorldSurface,
  SDL_GPUGraphicsPipeline* pipeline3D,
  SDL_GPUGraphicsPipeline* pipeline3DTranslucent,
  SDL_GPUGraphicsPipeline* instancedMeshPipeline,
  SDL_GPUGraphicsPipeline* staticMeshPipeline,
  SDL_GPUGraphicsPipeline* materialMeshPipeline,
  SDL_GPUGraphicsPipeline* staticMeshViewModelPipeline,
  SDL_GPUGraphicsPipeline* materialMeshViewModelPipeline,
  SDL_GPUGraphicsPipeline* gltfPlayerModelPipeline,
  SDL_GPUGraphicsPipeline* gltfPlayerModelFlatPipeline,
  SDL_GPUGraphicsPipeline* directWorldSurfacePipeline,
  SDL_GPUGraphicsPipeline* directWorldPipeline,
  SDL_GPUGraphicsPipeline* directInstancedMeshPipeline,
  SDL_GPUGraphicsPipeline* directStaticMeshPipeline,
  SDL_GPUGraphicsPipeline* directMaterialMeshPipeline,
  SDL_GPUGraphicsPipeline* directGltfPlayerPipeline,
  SDL_GPUGraphicsPipeline* depthWorldPipeline,
  SDL_GPUGraphicsPipeline* depthInstancedPipeline,
  SDL_GPUGraphicsPipeline* depthStaticPipeline,
  SDL_GPUGraphicsPipeline* depthMaterialPipeline,
  SDL_GPUGraphicsPipeline* depthGltfPipeline,
  SDL_GPUGraphicsPipeline* instancedGlowPipeline,
  SDL_GPUGraphicsPipeline* pipelineOutlineClear,
  SDL_GPUGraphicsPipeline* pipelineOutlineColorClear,
  SDL_GPUGraphicsPipeline* pipelineOutlineMask,
  SDL_GPUGraphicsPipeline* staticMeshOutlineMaskPipeline,
  SDL_GPUGraphicsPipeline* materialMeshOutlineMaskPipeline,
  SDL_GPUGraphicsPipeline* gltfPlayerModelOutlineMaskPipeline,
  SDL_GPUGraphicsPipeline* pipelineOutlineDilation,
  SDL_GPUGraphicsPipeline* pipelineOutlineComposite,
  SDL_GPUGraphicsPipeline* pipelineOutlineNativeDilation,
  SDL_GPUGraphicsPipeline* pipelineOutlineNativeComposite,
  SDL_GPUGraphicsPipeline* sunShadowWorldPipeline,
  SDL_GPUGraphicsPipeline* sunShadowStaticPipeline,
  SDL_GPUGraphicsPipeline* sunShadowMaterialPipeline,
  SDL_GPUGraphicsPipeline* sunShadowGltfPipeline,
  SDL_GPUGraphicsPipeline* pointShadowWorldPipeline,
  SDL_GPUGraphicsPipeline* bloomSourcePipeline,
  SDL_GPUGraphicsPipeline* bloomBlurPipeline,
  SDL_GPUGraphicsPipeline* sceneCompositePipeline,
  SDL_GPUGraphicsPipeline* sceneCompositeNoBloomPipeline,
  SDL_GPUGraphicsPipeline* skyPipeline,
  SDL_GPUGraphicsPipeline* directSkyPipeline,
  SDL_GPUTexture* skyTexture,
  SDL_GPUSampler* skySampler,
  SDL_GPUBuffer* vertexBuffer,
  SDL_GPUTransferBuffer* transferBuffer,
  GpuSimpleResources* simpleResources,
  GpuGltfPlayerResources* gltfPlayerResources,
  FontAtlasSet* fontAtlasSet,
  SDL_GPUSampler* fontSampler,
  TextureAtlas* worldAtlas,
  StaticWorldMesh*& staticWorld,
  SDL_GPUTexture*& depthTexture,
  SDL_GPUTexture*& viewModelDepthTexture,
  SDL_GPUTexture*& msaaColorTexture,
  SDL_GPUTexture*& sceneColorTexture,
  SDL_GPUTexture*& bloomTextureA,
  SDL_GPUTexture*& bloomTextureB,
  SDL_GPUTexture*& bloomDepthTexture,
  SDL_GPUTexture*& outlineMaskTexture,
  SDL_GPUTexture*& outlineDilationTexture,
  SDL_GPUTexture*& outlineDepthTexture,
  SDL_GPUTexture*& sunShadowTexture,
  SDL_GPUTexture* sunShadowFallbackTexture,
  SDL_GPUTexture*& pointShadowTexture,
  SDL_GPUTexture* pointShadowFallbackTexture,
  SDL_GPUSampler* outlineMaskSampler,
  SDL_GPUSampler* sunShadowSampler,
  SDL_GPUSampler* pointShadowSampler,
  SDL_GPUSampler* postProcessSampler,
  Uint32& depthWidth,
  Uint32& depthHeight,
  Uint32& viewModelDepthWidth,
  Uint32& viewModelDepthHeight,
  Uint32& msaaColorWidth,
  Uint32& msaaColorHeight,
  Uint32& sceneColorWidth,
  Uint32& sceneColorHeight,
  Uint32& bloomWidth,
  Uint32& bloomHeight,
  Uint32& bloomDepthWidth,
  Uint32& bloomDepthHeight,
  Uint32& outlineMaskWidth,
  Uint32& outlineMaskHeight,
  Uint32& outlineDilationWidth,
  Uint32& outlineDilationHeight,
  Uint32& outlineDepthWidth,
  Uint32& outlineDepthHeight,
  Uint32& sunShadowSize,
  std::uint64_t& sunShadowCacheKey,
  Uint32& pointShadowSize,
  Uint32& pointShadowLightCount,
  std::uint64_t& pointShadowCacheKey,
  SDL_GPUTextureFormat depthFormat,
  SDL_GPUTextureFormat sceneColorFormat,
  SDL_GPUSampleCount sampleCount,
  std::vector<GpuVertex>& vertices,
  SDL_Window* window,
  const Arena& arena,
  const PlayerState& player,
  LateViewSampler lateViewSampler,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const IcePoolArray& icePools,
  const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  std::uint32_t newExplosionEventsConsumed,
  const RenderSettings& settings,
  NativeOutlineFallbackReason nativeOutlineFallbackReason,
  const HudRenderState& hud,
  const ConsoleRenderState& console,
  float cameraVerticalOffset,
  RendererFrameDiagnostics& diagnostics,
  GpuTimestampTiming& gpuTiming,
  const FrameCaptureRequest* captureRequest,
  FrameCaptureResult* captureResult
) {
  diagnostics.swapchainAcquireMilliseconds = 0.0F;
  diagnostics.lateMouseSampleMilliseconds = 0.0F;
  diagnostics.mouseSampleToSubmitMilliseconds = 0.0F;
  diagnostics.mouseSamplePhaseGainMilliseconds = 0.0F;
  diagnostics.lateMouseSampleEnabled = lateViewSampler.sample != nullptr;
  diagnostics.lateMouseSampleApplied = false;
  diagnostics.renderInstanceConstructionMilliseconds = 0.0F;
  diagnostics.worldVisibilityMilliseconds = 0.0F;
  diagnostics.worldCommandEncodingMilliseconds = 0.0F;
  diagnostics.dynamicCommandEncodingMilliseconds = 0.0F;
  diagnostics.uiMilliseconds = 0.0F;
  diagnostics.sceneBuildMilliseconds = 0.0F;
  diagnostics.gpuVertexUploadMilliseconds = 0.0F;
  diagnostics.worldDrawIssueMilliseconds = 0.0F;
  diagnostics.renderBuildUploadMilliseconds = 0.0F;
  diagnostics.submitMilliseconds = 0.0F;
  diagnostics.worldSourceTriangles = 0;
  diagnostics.worldRenderedTriangles = 0;
  diagnostics.worldSubmittedTriangles = 0;
  diagnostics.worldDuplicateTrianglesCulled = 0;
  diagnostics.worldVertexCount = 0;
  diagnostics.worldDrawCalls = 0;
  diagnostics.skyDrawCalls = 0;
  diagnostics.skyLoadedTextures = 0;
  diagnostics.worldSubmittedRanges = 0;
  diagnostics.worldTotalChunks = 0;
  diagnostics.worldVisibleChunks = 0;
  diagnostics.worldCulledChunks = 0;
  diagnostics.worldVisibilityTestedNodes = 0;
  diagnostics.worldVisibilityQueryMilliseconds = 0.0F;
  diagnostics.ambientGroundingQuality = settings.ambientGroundingQuality;
  diagnostics.ambientStaticRays = 0;
  diagnostics.ambientStaticSamples = 0;
  diagnostics.ambientStaticCacheHits = 0;
  diagnostics.ambientStaticMinimum = 255U;
  diagnostics.ambientStaticMaximum = 255U;
  diagnostics.ambientProbeCount = 0;
  diagnostics.ambientProbeRays = 0;
  diagnostics.ambientProbeBytes = 0;
  diagnostics.ambientProbeFingerprint = 0;
  diagnostics.ambientProbeBuildMilliseconds = 0.0F;
  diagnostics.ambientDynamicSamples = 0;
  diagnostics.gpuDepthBits = gpuDepthFormatBits(depthFormat);
  diagnostics.worldLoadedTextures = 0;
  diagnostics.worldMissingTextures = 0;
  diagnostics.worldReferencedMaterials = 0;
  diagnostics.worldMaxTextureMipLevels = 0;
  diagnostics.worldTextureFilter = normalizedTextureFilter(settings.textureFilter);
  diagnostics.worldRequestedTextureAnisotropy =
    normalizedTextureAnisotropy(settings.textureAnisotropy);
  diagnostics.worldAppliedTextureAnisotropy = 1;
  diagnostics.worldTextureLodBias =
    normalizedTextureLodBias(settings.textureLodBias);
  diagnostics.dynamicOpaqueVertices = 0;
  diagnostics.dynamicTranslucentVertices = 0;
  diagnostics.totalUploadedVertices = 0;
  diagnostics.dynamicTriangles = 0;
  diagnostics.normalPlayerBodyDynamicVertices = 0;
  diagnostics.geometryOutlineDynamicVertices = 0;
  diagnostics.outlinedPlayers = 0;
  diagnostics.outlineStyle = static_cast<int>(settings.playerOutlineStyle);
  diagnostics.outlineMaskWidth = 0;
  diagnostics.outlineMaskHeight = 0;
  diagnostics.outlineWorkWidth = 0;
  diagnostics.outlineWorkHeight = 0;
  diagnostics.outlineWorkScale = 0.0F;
  diagnostics.outlineWorkRectX = 0;
  diagnostics.outlineWorkRectY = 0;
  diagnostics.outlineWorkRectWidth = 0;
  diagnostics.outlineWorkRectHeight = 0;
  diagnostics.outlineWorkAreaPercent = 0.0F;
  diagnostics.outlineMaskDrawCalls = 0;
  diagnostics.outlineDilationDrawCalls = 0;
  diagnostics.outlineCompositeDrawCalls = 0;
  diagnostics.outlineUploadBytes = 0;
  diagnostics.outlineGpuTimingAvailable = false;
  diagnostics.outlineGpuMilliseconds = 0.0F;
  diagnostics.outlinePasses = 0;
  diagnostics.outlineCompositeEnabled = false;
  diagnostics.geometryOutlineFallbackUsed = false;
  diagnostics.nativeOutlineFallbackReason = nativeOutlineFallbackReason;
  diagnostics.sceneColorWidth = 0;
  diagnostics.sceneColorHeight = 0;
  diagnostics.sceneColorFormat = static_cast<std::uint32_t>(sceneColorFormat);
  diagnostics.sceneCompositePasses = 0;
  diagnostics.bloomWidth = 0;
  diagnostics.bloomHeight = 0;
  diagnostics.bloomPasses = 0;
  diagnostics.sceneCompositeEnabled = false;
  diagnostics.bloomEnabled = false;
  diagnostics.directPresentEligible = false;
  diagnostics.directPresentUsed = false;
  diagnostics.directPresentFallbackReason = "not-evaluated";
  diagnostics.directPresentFormat.clear();
  diagnostics.visibleRemotePlayers = 0;
  diagnostics.remoteBodyModelsBuilt = 0;
  diagnostics.remoteWeaponModelsBuilt = 0;
  diagnostics.playerOutlinesBuilt = 0;
  diagnostics.remoteCandidates = 0;
  diagnostics.remoteFrustumVisible = 0;
  diagnostics.remoteFrustumCulled = 0;
  diagnostics.remoteWeaponCandidates = 0;
  diagnostics.remoteWeaponsFrustumCulled = 0;
  diagnostics.remoteWeaponInstances = 0;
  diagnostics.remoteWeaponInstanceUploadBytes = 0;
  diagnostics.remoteWeaponBatches = 0;
  diagnostics.remoteWeaponDrawCalls = 0;
  diagnostics.legacyRemoteWeaponDynamicVertices = 0;
  diagnostics.gltfPlayerModelInstances = 0;
  diagnostics.gltfPlayerModelFrustumCulled = 0;
  diagnostics.gltfStaticMeshGpuBytes = 0;
  diagnostics.gltfStaticIndexGpuBytes = 0;
  diagnostics.gltfMaterialTextureGpuBytes = gltfPlayerResources != nullptr
    ? gltfPlayerResources->materialTextureBytes
    : 0U;
  diagnostics.gltfMaterialTextureMipLevels = gltfPlayerResources != nullptr
    ? gltfPlayerResources->materialTextureMipLevels
    : 0U;
  diagnostics.gltfMaterialTextureBinds = 0;
  diagnostics.gltfAuthoredMaterialTexturesReady = gltfPlayerResources != nullptr &&
    gltfPlayerResources->authoredMaterialTexturesReady;
  diagnostics.gltfMaterialFallbackUsed = gltfPlayerResources != nullptr &&
    gltfPlayerResources->materialManifestHasAuthoredTextures &&
    !gltfPlayerResources->authoredMaterialTexturesReady;
  diagnostics.gltfPoseUploadBytes = 0;
  diagnostics.gltfBonePaletteEntriesUploaded = 0;
  diagnostics.gltfRigidFallbackInstances = 0;
  diagnostics.gltfGpuSkinnedInstances = 0;
  diagnostics.gltfBodyBatches = 0;
  diagnostics.gltfBodyDrawCalls = 0;
  diagnostics.gltfShadowCasterInstances = 0;
  diagnostics.gltfShadowCasterDrawCalls = 0;
  diagnostics.gltfOutlineMaskBatches = 0;
  diagnostics.gltfOutlineMaskDrawCalls = 0;
  diagnostics.legacyCpuSkinnedGltfVertexUploadBytes = 0;
  diagnostics.firstPersonViewModelDrawCalls = 0;
  diagnostics.firstPersonViewModelDynamicVertices = 0;
  diagnostics.projectilesActive = 0;
  diagnostics.projectilesFrustumCulled = 0;
  diagnostics.projectilesRendered = 0;
  diagnostics.plasmaInstances = 0;
  diagnostics.rocketInstances = 0;
  diagnostics.grenadeInstances = 0;
  diagnostics.projectileCoreInstances = 0;
  diagnostics.projectileGlowInstances = 0;
  diagnostics.opaqueProjectileBatches = 0;
  diagnostics.additiveProjectileBatches = 0;
  diagnostics.projectileInstanceUploadBytes = 0;
  diagnostics.projectileMeshDrawCalls = 0;
  diagnostics.projectileGlowDrawCalls = 0;
  diagnostics.legacyProjectileDynamicVertices = 0;
  diagnostics.activeTransientEffects = 0;
  diagnostics.activeMachineGunTracers = 0;
  diagnostics.activeShotgunTracers = 0;
  diagnostics.activeExplosionEffects = 0;
  diagnostics.newExplosionEventsConsumed = 0;
  diagnostics.tracerCandidates = 0;
  diagnostics.tracerFrustumCulled = 0;
  diagnostics.tracerInstancesSubmitted = 0;
  diagnostics.tracerInstanceUploadBytes = 0;
  diagnostics.tracerBatches = 0;
  diagnostics.tracerDrawCalls = 0;
  diagnostics.explosionCandidates = 0;
  diagnostics.explosionFrustumCulled = 0;
  diagnostics.explosionInstancesSubmitted = 0;
  diagnostics.explosionInstanceUploadBytes = 0;
  diagnostics.explosionOpaqueBatches = 0;
  diagnostics.explosionAdditiveBatches = 0;
  diagnostics.explosionDrawCalls = 0;
  diagnostics.legacyWireframeExplosionDraws = 0;
  diagnostics.legacyMachineGunShotgunVisualDraws = 0;
  diagnostics.activeTemporaryLights = 0;
  diagnostics.authoredPointLights = 0;
  diagnostics.pointLightCandidates = 0;
  diagnostics.selectedPointLights = 0;
  diagnostics.droppedPointLights = 0;
  diagnostics.flickeringPointLights = 0;
  diagnostics.shadowedPointLights = 0;
  diagnostics.activeCasings = 0;
  diagnostics.activeImpactParticles = 0;
  diagnostics.activeBulletDecals = 0;
  diagnostics.transparentEffectsSubmitted = 0;
  bool pointShadowCacheRendered = false;
  std::uint64_t pendingPointShadowCacheKey = 0;
  std::uint64_t lateSampleCompletedNanoseconds = 0;
  SDL_GPUCommandBuffer* commandBuffer =
    SDL_AcquireGPUCommandBuffer(device);
  if (commandBuffer == nullptr) {
    return false;
  }
  bool timingActive = false;
  bool timingOwnedSubmittedFence = false;
  const auto submitCommandBuffer = [&](SDL_GPUFence** outputFence = nullptr) {
    recordMouseSampleToSubmit(
      lateSampleCompletedNanoseconds,
      diagnostics
    );
    if (timingActive) {
      gpuTiming.endFrame(commandBuffer);
      SDL_GPUFence* fence =
        SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
      if (fence == nullptr) {
        gpuTiming.submissionFailed();
        timingActive = false;
        return false;
      }
      gpuTiming.submitted(device, fence);
      timingActive = false;
      timingOwnedSubmittedFence = true;
      if (outputFence != nullptr) {
        *outputFence = fence;
      }
      return true;
    }
    if (outputFence != nullptr) {
      *outputFence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
      return *outputFence != nullptr;
    }
    return SDL_SubmitGPUCommandBuffer(commandBuffer);
  };

  SDL_GPUTexture* swapchainTexture = nullptr;
  Uint32 outputWidth = 0;
  Uint32 outputHeight = 0;
  const auto acquireStart = RenderClock::now();
  // This can block on swapchain availability and requested frame pacing.
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(
        commandBuffer,
        window,
        &swapchainTexture,
        &outputWidth,
        &outputHeight
      )) {
    (void)SDL_CancelGPUCommandBuffer(commandBuffer);
    return false;
  }
  const auto acquireEnd = RenderClock::now();
  diagnostics.swapchainAcquireMilliseconds =
    millisecondsBetween(acquireStart, acquireEnd);
  const auto buildStart = acquireEnd;
  auto uploadStart = buildStart;

  if (swapchainTexture != nullptr && outputWidth > 0 && outputHeight > 0) {
    PlayerState sampledPlayer = player;
    lateSampleCompletedNanoseconds = sampleLateView(
      lateViewSampler,
      sampledPlayer,
      diagnostics
    );
    Scene3D perspectiveScene;
    vertices.clear();
    std::vector<OverlayDrawBatch> overlayBatches;
    const auto sceneBuildStart = RenderClock::now();
    RenderClock::time_point instanceConstructionStart = {};
    if (settings.benchmarkTimingEnabled) {
      instanceConstructionStart = RenderClock::now();
    }
    perspectiveScene = buildPerspectiveScene(
      static_cast<float>(outputWidth) / static_cast<float>(outputHeight),
      arena,
      sampledPlayer,
      remotePlayers,
      localLightningGun,
      weaponFires,
      rocketExplosions,
      rockets,
      healthPickupAvailable,
      transientTracers,
      transientEffects,
      icePools,
      settings,
      cameraVerticalOffset
    );
    const bool bloomEffective = effectiveBloom(
      settings.bloomEnabled,
      perspectiveScene.simpleBatches
    );
    diagnostics.remoteCandidates = perspectiveScene.remoteCandidates;
    diagnostics.remoteFrustumVisible = perspectiveScene.remoteFrustumVisible;
    diagnostics.remoteFrustumCulled = perspectiveScene.remoteFrustumCulled;
    diagnostics.remoteWeaponCandidates =
      perspectiveScene.remoteWeaponStats.candidates;
    diagnostics.remoteWeaponsFrustumCulled =
      perspectiveScene.remoteWeaponStats.frustumCulled;
    diagnostics.remoteWeaponInstances =
      perspectiveScene.remoteWeaponStats.instancesSubmitted;
    diagnostics.remoteWeaponInstanceUploadBytes =
      perspectiveScene.remoteWeaponStats.instanceUploadBytes;
    diagnostics.remoteWeaponBatches = perspectiveScene.remoteWeaponStats.batches;
    diagnostics.remoteWeaponDrawCalls =
      perspectiveScene.remoteWeaponStats.drawCalls;
    diagnostics.legacyRemoteWeaponDynamicVertices =
      perspectiveScene.remoteWeaponStats.legacyDynamicVertices;
    diagnostics.visibleProceduralBoxPlayers =
      perspectiveScene.playerBoxStats.visiblePlayers;
    diagnostics.culledProceduralBoxPlayers =
      perspectiveScene.playerBoxStats.culledPlayers;
    diagnostics.playerBoxInstancesSubmitted =
      perspectiveScene.playerBoxStats.instancesSubmitted;
    diagnostics.playerBoxInstanceUploadBytes =
      perspectiveScene.playerBoxStats.instanceUploadBytes;
    diagnostics.sharedCubeStaticGpuBytes =
      perspectiveScene.playerBoxStats.sharedCubeStaticGpuBytes;
    diagnostics.proceduralPlayerOpaqueBatches =
      perspectiveScene.playerBoxStats.opaqueBatches;
    diagnostics.proceduralPlayerOpaqueDrawCalls =
      perspectiveScene.playerBoxStats.opaqueDrawCalls;
    diagnostics.proceduralPlayerOutlineMaskBatches =
      perspectiveScene.playerBoxStats.outlineMaskBatches;
    diagnostics.proceduralPlayerOutlineMaskDrawCalls =
      perspectiveScene.playerBoxStats.outlineMaskDrawCalls;
    diagnostics.legacyCpuGeneratedPlayerVertices =
      perspectiveScene.playerBoxStats.legacyCpuGeneratedVertices;
    diagnostics.legacyDynamicPlayerVertexUploadBytes =
      perspectiveScene.playerBoxStats.legacyDynamicVertexUploadBytes;
    copyGltfPlayerModelDiagnostics(diagnostics, perspectiveScene);
    diagnostics.firstPersonViewModelDrawCalls =
      perspectiveScene.viewModelStats.drawCalls;
    diagnostics.firstPersonViewModelDynamicVertices =
      perspectiveScene.viewModelStats.dynamicVertices;
    diagnostics.projectilesActive =
      perspectiveScene.projectileStats.projectilesActive;
    diagnostics.projectilesFrustumCulled =
      perspectiveScene.projectileStats.projectilesFrustumCulled;
    diagnostics.projectilesRendered =
      perspectiveScene.projectileStats.projectilesRendered;
    diagnostics.plasmaInstances =
      perspectiveScene.projectileStats.plasmaInstances;
    diagnostics.rocketInstances =
      perspectiveScene.projectileStats.rocketInstances;
    diagnostics.grenadeInstances =
      perspectiveScene.projectileStats.grenadeInstances;
    diagnostics.projectileCoreInstances =
      perspectiveScene.projectileStats.projectileCoreInstances;
    diagnostics.projectileGlowInstances =
      perspectiveScene.projectileStats.projectileGlowInstances;
    diagnostics.opaqueProjectileBatches =
      perspectiveScene.projectileStats.opaqueProjectileBatches;
    diagnostics.additiveProjectileBatches =
      perspectiveScene.projectileStats.additiveProjectileBatches;
    diagnostics.projectileInstanceUploadBytes =
      perspectiveScene.projectileStats.projectileInstanceUploadBytes;
    diagnostics.projectileMeshDrawCalls =
      perspectiveScene.projectileStats.projectileMeshDrawCalls;
    diagnostics.projectileGlowDrawCalls =
      perspectiveScene.projectileStats.projectileGlowDrawCalls;
    diagnostics.legacyProjectileDynamicVertices =
      perspectiveScene.projectileStats.legacyProjectileDynamicVertices;
    diagnostics.activeTransientEffects =
      perspectiveScene.transientVfxStats.activeEffects;
    diagnostics.activeMachineGunTracers =
      perspectiveScene.transientVfxStats.activeMachineGunTracers;
    diagnostics.activeShotgunTracers =
      perspectiveScene.transientVfxStats.activeShotgunTracers;
    diagnostics.activeExplosionEffects =
      perspectiveScene.transientVfxStats.activeExplosionEffects;
    diagnostics.newExplosionEventsConsumed = newExplosionEventsConsumed;
    diagnostics.tracerCandidates =
      perspectiveScene.transientVfxStats.tracerCandidates;
    diagnostics.tracerFrustumCulled =
      perspectiveScene.transientVfxStats.tracerFrustumCulled;
    diagnostics.tracerInstancesSubmitted =
      perspectiveScene.transientVfxStats.tracerInstancesSubmitted;
    diagnostics.tracerInstanceUploadBytes =
      perspectiveScene.transientVfxStats.tracerInstanceUploadBytes;
    diagnostics.tracerBatches =
      perspectiveScene.transientVfxStats.tracerBatches;
    diagnostics.tracerDrawCalls =
      perspectiveScene.transientVfxStats.tracerDrawCalls;
    diagnostics.explosionCandidates =
      perspectiveScene.transientVfxStats.explosionCandidates;
    diagnostics.explosionFrustumCulled =
      perspectiveScene.transientVfxStats.explosionFrustumCulled;
    diagnostics.explosionInstancesSubmitted =
      perspectiveScene.transientVfxStats.explosionInstancesSubmitted;
    diagnostics.explosionInstanceUploadBytes =
      perspectiveScene.transientVfxStats.explosionInstanceUploadBytes;
    diagnostics.explosionOpaqueBatches =
      perspectiveScene.transientVfxStats.explosionOpaqueBatches;
    diagnostics.explosionAdditiveBatches =
      perspectiveScene.transientVfxStats.explosionAdditiveBatches;
    diagnostics.explosionDrawCalls =
      perspectiveScene.transientVfxStats.explosionDrawCalls;
    diagnostics.legacyWireframeExplosionDraws =
      perspectiveScene.transientVfxStats.legacyWireframeExplosionDraws;
    diagnostics.legacyMachineGunShotgunVisualDraws =
      perspectiveScene.transientVfxStats.legacyMachineGunShotgunVisualDraws;
    diagnostics.activeTemporaryLights =
      perspectiveScene.transientVfxStats.activeTemporaryLights;
    diagnostics.authoredPointLights =
      perspectiveScene.pointLightStats.authored;
    diagnostics.pointLightCandidates =
      perspectiveScene.pointLightStats.candidates;
    diagnostics.selectedPointLights =
      perspectiveScene.pointLightStats.selected;
    diagnostics.droppedPointLights =
      perspectiveScene.pointLightStats.dropped;
    diagnostics.flickeringPointLights =
      perspectiveScene.pointLightStats.flickering;
    diagnostics.shadowedPointLights = 0;
    diagnostics.activeCasings =
      perspectiveScene.transientVfxStats.activeCasings;
    diagnostics.activeImpactParticles =
      perspectiveScene.transientVfxStats.activeImpactParticles;
    diagnostics.activeBulletDecals =
      perspectiveScene.transientVfxStats.activeBulletDecals;
    diagnostics.transparentEffectsSubmitted =
      perspectiveScene.transientVfxStats.transparentEffectsSubmitted;
    diagnostics.remoteBodyModelsBuilt = perspectiveScene.remoteBodyModelsBuilt;
    diagnostics.remoteWeaponModelsBuilt =
      perspectiveScene.remoteWeaponModelsBuilt;
    diagnostics.playerOutlinesBuilt = perspectiveScene.playerOutlinesBuilt;
    diagnostics.normalPlayerBodyDynamicVertices =
      perspectiveScene.normalPlayerBodyDynamicVertices;
    diagnostics.geometryOutlineDynamicVertices =
      perspectiveScene.geometryOutlineDynamicVertices;
    diagnostics.outlinedPlayers = perspectiveScene.outlinedPlayers;
    diagnostics.geometryOutlineFallbackUsed =
      perspectiveScene.geometryOutlineFallbackUsed;
    appendScene3D(vertices, perspectiveScene, worldAtlas);
    if (settings.benchmarkTimingEnabled) {
      // Instance construction ends before HUD building and draw encoding.
      diagnostics.renderInstanceConstructionMilliseconds = millisecondsBetween(
        instanceConstructionStart,
        RenderClock::now()
      );
    }
    diagnostics.sceneBuildMilliseconds =
      millisecondsBetween(sceneBuildStart, RenderClock::now());
    const Uint32 opaqueDynamicVertexCount =
      static_cast<Uint32>(vertices.size());
    appendVertices3D(vertices, perspectiveScene.contactShadowVertices, worldAtlas);
    appendVertices3D(vertices, perspectiveScene.translucentVertices, worldAtlas);
    const Uint32 dynamic3DVertexCount = static_cast<Uint32>(vertices.size());
    const Uint32 worldVertexCount = dynamic3DVertexCount;
    RenderClock::time_point uiBuildStart = {};
    if (settings.benchmarkTimingEnabled) {
      uiBuildStart = RenderClock::now();
    }
    if (captureRequest == nullptr || !captureRequest->hideHud) {
    const DrawList2D floatingHealthBars = buildFloatingHealthBars(
      static_cast<int>(outputWidth),
      static_cast<int>(outputHeight),
      perspectiveScene.camera,
      arena,
      remotePlayers,
      perspectiveScene.remoteRenderVisible,
      settings,
      hud
    );
    appendCommandBatches(
      vertices,
      overlayBatches,
      floatingHealthBars.overlayCommands,
      *fontAtlasSet,
      static_cast<float>(outputWidth),
      static_cast<float>(outputHeight)
    );
    const DrawList2D floatingDamageNumbers = buildFloatingDamageNumbers(
      static_cast<int>(outputWidth),
      static_cast<int>(outputHeight),
      perspectiveScene.camera,
      settings,
      hud
    );
    appendCommandBatches(
      vertices,
      overlayBatches,
      floatingDamageNumbers.overlayCommands,
      *fontAtlasSet,
      static_cast<float>(outputWidth),
      static_cast<float>(outputHeight)
    );
    if (
      hud.selectedWeapon != Weapon::MachineGun &&
      hud.selectedWeapon != Weapon::Shotgun &&
      hud.selectedWeapon != Weapon::RocketLauncher &&
      hud.selectedWeapon != Weapon::Revolver
    ) {
      ScreenPoint freezeGunMuzzle = {-1.0F, -1.0F};
      if (
        hud.selectedWeapon == Weapon::FreezeGun &&
        settings.showOwnWeapons
      ) {
        PerspectiveCamera muzzleCamera = perspectiveScene.camera;
        constexpr float kPi = 3.14159265359F;
        const float viewModelFov = std::max(50.0F, settings.fieldOfView - 10.0F);
        muzzleCamera.focalLength = 1.0F / std::tan(
          viewModelFov * (kPi / 180.0F) * 0.5F
        );
        ProjectedPoint projectedMuzzle;
        if (projectPerspectivePoint(
              muzzleCamera,
              firstPersonFreezeGunMuzzlePosition(sampledPlayer, settings),
              projectedMuzzle
            )) {
          freezeGunMuzzle = {
            (projectedMuzzle.x + 1.0F) * 0.5F * static_cast<float>(outputWidth),
            (1.0F - projectedMuzzle.y) * 0.5F * static_cast<float>(outputHeight),
          };
        }
      }
      const DrawList2D weaponOverlay = buildPerspectiveWeaponOverlay(
        static_cast<int>(outputWidth),
        static_cast<int>(outputHeight),
        localLightningGun,
        hud.selectedWeapon,
        hud.previousWeapon,
        hud.weaponSwitchProgress,
        settings,
        freezeGunMuzzle
      );
      appendCommandBatches(
        vertices,
        overlayBatches,
        weaponOverlay.overlayCommands,
        *fontAtlasSet,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight)
      );
    }
    const DrawList2D ui = buildScreenUi(
      static_cast<int>(outputWidth),
      static_cast<int>(outputHeight),
      sampledPlayer,
      settings,
      hud,
      console
    );
    appendCommandBatches(
      vertices,
      overlayBatches,
      ui.overlayCommands,
      *fontAtlasSet,
      static_cast<float>(outputWidth),
      static_cast<float>(outputHeight)
    );
    }
    if (settings.benchmarkTimingEnabled) {
      // UI includes HUD list construction and conversion to overlay batches.
      diagnostics.uiMilliseconds =
        millisecondsBetween(uiBuildStart, RenderClock::now());
    }
    uploadStart = RenderClock::now();
    diagnostics.sceneBuildMilliseconds =
      millisecondsBetween(buildStart, uploadStart);
    diagnostics.dynamicOpaqueVertices = opaqueDynamicVertexCount;
    diagnostics.dynamicTranslucentVertices =
      worldVertexCount - opaqueDynamicVertexCount;
    diagnostics.visibleRemotePlayers = perspectiveScene.visibleRemotePlayers;
    diagnostics.remoteBodyModelsBuilt = perspectiveScene.remoteBodyModelsBuilt;
    diagnostics.remoteWeaponModelsBuilt = perspectiveScene.remoteWeaponModelsBuilt;
    diagnostics.playerOutlinesBuilt = perspectiveScene.playerOutlinesBuilt;
    diagnostics.normalPlayerBodyDynamicVertices =
      perspectiveScene.normalPlayerBodyDynamicVertices;
    diagnostics.geometryOutlineDynamicVertices =
      perspectiveScene.geometryOutlineDynamicVertices;
    diagnostics.outlinedPlayers = perspectiveScene.outlinedPlayers;
    diagnostics.geometryOutlineFallbackUsed =
      perspectiveScene.geometryOutlineFallbackUsed;
    diagnostics.dynamicTriangles =
      (diagnostics.dynamicOpaqueVertices + diagnostics.dynamicTranslucentVertices) / 3U;
    if (vertices.size() > kMaxGpuVertices) {
      (void)submitCommandBuffer();
      SDL_SetError("SDL_GPU 2D vertex capacity exceeded");
      return false;
    }

    if (settings.benchmarkGpuFrameIndex.has_value()) {
      // Reset and TOP are the first commands in the measured primary buffer.
      // Fence polling later reads this range without stalling the render loop.
      timingActive = gpuTiming.beginFrame(
        commandBuffer,
        *settings.benchmarkGpuFrameIndex,
        false
      );
    }

    StaticWorldMesh* worldMesh =
      ensureStaticWorldMesh(device, staticWorld, arena, settings);
    const StaticAmbientProbeGrid* ambientProbeGrid =
      worldMesh != nullptr && worldMesh->ambientProbeGrid.enabled()
      ? &worldMesh->ambientProbeGrid
      : nullptr;

    if (!vertices.empty()) {
      const auto uploadStart = RenderClock::now();
      void* mapped =
        SDL_MapGPUTransferBuffer(device, transferBuffer, true);
      if (mapped == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      const Uint32 uploadSize =
        static_cast<Uint32>(vertices.size() * sizeof(GpuVertex));
      std::memcpy(mapped, vertices.data(), uploadSize);
      SDL_UnmapGPUTransferBuffer(device, transferBuffer);

      SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
      if (copyPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      const SDL_GPUTransferBufferLocation source = {transferBuffer, 0};
      const SDL_GPUBufferRegion destination = {
        vertexBuffer,
        0,
        uploadSize,
      };
      SDL_UploadToGPUBuffer(
        copyPass,
        &source,
        &destination,
        true
      );
      SDL_EndGPUCopyPass(copyPass);
      diagnostics.gpuVertexUploadMilliseconds =
        millisecondsBetween(uploadStart, RenderClock::now());
    }
    if (
      simpleResources == nullptr ||
      !uploadSimpleInstances(
          device,
          commandBuffer,
          simpleResources->instances,
          perspectiveScene
      ) ||
      !uploadStaticMeshInstances(
          device,
          commandBuffer,
          simpleResources->staticInstances,
          perspectiveScene,
          ambientProbeGrid,
          settings.ambientDebugMode,
          &diagnostics.ambientDynamicSamples
      ) ||
      gltfPlayerResources == nullptr ||
      !uploadGltfPlayerFrameData(
          device,
          commandBuffer,
          *gltfPlayerResources,
          perspectiveScene,
          ambientProbeGrid,
          settings.ambientDebugMode,
          &diagnostics.ambientDynamicSamples
      )
    ) {
      (void)submitCommandBuffer();
      return false;
    }
    if (worldMesh != nullptr) {
      RenderClock::time_point visibilityStart = {};
      if (settings.benchmarkTimingEnabled || settings.worldFrustumCull) {
        visibilityStart = RenderClock::now();
      }
      updateStaticWorldVisibility(
        *worldMesh,
        perspectiveScene.camera,
        settings.worldFrustumCull
      );
      RenderClock::time_point visibilityEnd = {};
      if (settings.benchmarkTimingEnabled || settings.worldFrustumCull) {
        visibilityEnd = RenderClock::now();
      }
      if (settings.worldFrustumCull) {
        diagnostics.worldVisibilityQueryMilliseconds =
          millisecondsBetween(visibilityStart, visibilityEnd);
      }
      if (settings.benchmarkTimingEnabled) {
        // Visibility covers only the CPU query and visible-range selection.
        diagnostics.worldVisibilityMilliseconds =
          millisecondsBetween(visibilityStart, visibilityEnd);
      }
    }
    const bool hasStaticWorld = worldMesh != nullptr &&
      worldMesh->vertexBuffer != nullptr &&
      worldMesh->sampler != nullptr &&
      !worldMesh->batches.empty();
    const SDL_GPUTextureFormat swapchainFormat =
      SDL_GetGPUSwapchainTextureFormat(device, window);
    const bool directPipelinesReady =
      directWorldSurfacePipeline != nullptr &&
      directWorldPipeline != nullptr &&
      directInstancedMeshPipeline != nullptr &&
      directStaticMeshPipeline != nullptr &&
      directMaterialMeshPipeline != nullptr &&
      directGltfPlayerPipeline != nullptr;
    const DirectPresentInputs directInputs = {
      perspectiveScene.lights.gradeQuality == 0,
      std::abs(perspectiveScene.lights.exposure - 1.0F) <= 0.000001F,
      displayGammaIsNeutral(settings.displayGamma),
      sampleCount == SDL_GPU_SAMPLECOUNT_1,
      !bloomEffective,
      perspectiveScene.lights.shadow.mapSize == 0U,
      perspectiveScene.lights.materialQuality == 0 &&
        perspectiveScene.lights.playerRimQuality == 1 &&
        settings.pointLightQuality <= 0,
      perspectiveScene.livePointLights.empty(),
      settings.playerOutlineMode == PlayerOutlineMode::Disabled ||
        settings.playerOutlineMode == PlayerOutlineMode::NativeScreenSpace,
      perspectiveScene.contactShadowVertices.empty(),
      perspectiveScene.translucentVertices.empty(),
      perspectiveScene.transientVfxStats.transparentEffectsSubmitted == 0U,
      simpleBatchesOpaque(perspectiveScene),
      worldAtlas == nullptr &&
        (perspectiveScene.vertices.empty() || hasStaticWorld) &&
        (
          !hasStaticWorld ||
          activeWorldTexturesOpaque(*worldMesh, settings.worldFrustumCull)
        ),
      (!hasStaticWorld || worldMesh->opaqueVertices) &&
        verticesOpaque(perspectiveScene.vertices),
      activeStaticMeshAssetsOpaque(*simpleResources, perspectiveScene) &&
        activeInstanceColorsOpaque(perspectiveScene),
      activePlayersOpaque(*gltfPlayerResources, perspectiveScene),
      activeViewModelOpaque(*simpleResources, perspectiveScene),
      directPresentSwapchainFormatSupported(swapchainFormat),
      directPipelinesReady,
    };
    const DirectPresentPlan directPlan =
      buildDirectPresentPlan(directInputs);
    const bool directPresent = directPlan.eligible;
    diagnostics.directPresentEligible = directPlan.eligible;
    diagnostics.directPresentUsed = directPresent;
    diagnostics.directPresentFallbackReason =
      directPresentFallbackReasonName(directPlan.fallback);
    diagnostics.directPresentFormat = gpuTextureFormatName(swapchainFormat);
    struct DirectPresentLogState {
      bool valid = false;
      bool used = false;
      std::string reason;
      std::string format;
    };
    static DirectPresentLogState directPresentLogState;
    if (!settings.benchmarkTimingEnabled) {
      directPresentLogState.valid = false;
    } else if (
      !directPresentLogState.valid ||
      directPresentLogState.used != directPresent ||
      directPresentLogState.reason !=
        diagnostics.directPresentFallbackReason ||
      directPresentLogState.format != diagnostics.directPresentFormat
    ) {
      std::cerr
        << "SDL_GPU direct present used=" << (directPresent ? 1 : 0)
        << " reason=" << diagnostics.directPresentFallbackReason
        << " format=" << diagnostics.directPresentFormat << '\n';
      directPresentLogState.valid = true;
      directPresentLogState.used = directPresent;
      directPresentLogState.reason =
        diagnostics.directPresentFallbackReason;
      directPresentLogState.format = diagnostics.directPresentFormat;
    }
    if (directPresent) {
      diagnostics.sceneColorFormat =
        static_cast<std::uint32_t>(swapchainFormat);
    }
    struct alignas(16) PerspectiveCameraUniform {
      float position[4];
      float right[4];
      float up[4];
      float forward[4];
      float projection[4];
    };
    const PerspectiveCamera& worldCamera = perspectiveScene.camera;
    const PerspectiveCameraUniform worldCameraUniform = {
      {
        worldCamera.position.x,
        worldCamera.position.y,
        worldCamera.position.z,
        static_cast<float>(perspectiveScene.lights.playerRimQuality),
      },
      {
        worldCamera.right.x,
        worldCamera.right.y,
        worldCamera.right.z,
        0.0F,
      },
      {worldCamera.up.x, worldCamera.up.y, worldCamera.up.z, 0.0F},
      {
        worldCamera.forward.x,
        worldCamera.forward.y,
        worldCamera.forward.z,
        0.0F,
      },
      {
        worldCamera.focalLength,
        worldCamera.aspectRatio,
        worldCamera.nearPlane,
        512.0F,
      },
    };
    const auto drawOpaqueDepth = [&](SDL_GPURenderPass* pass) {
      if (hasStaticWorld) {
        SDL_BindGPUGraphicsPipeline(pass, depthWorldPipeline);
        const SDL_GPUBufferBinding staticBinding = {
          worldMesh->vertexBuffer,
          0,
        };
        SDL_BindGPUVertexBuffers(pass, 0, &staticBinding, 1);
        for (const StaticWorldBatch& batch :
             staticWorldDrawBatches(*worldMesh, settings.worldFrustumCull)) {
          SDL_DrawGPUPrimitives(
            pass,
            batch.vertexCount,
            1,
            batch.firstVertex,
            0
          );
        }
      }
      if (opaqueDynamicVertexCount > 0U) {
        SDL_BindGPUGraphicsPipeline(pass, depthWorldPipeline);
        const SDL_GPUBufferBinding dynamicBinding = {vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &dynamicBinding, 1);
        SDL_DrawGPUPrimitives(
          pass,
          opaqueDynamicVertexCount,
          1,
          0,
          0
        );
      }
      drawStaticMeshDepthBatches(
        pass,
        depthStaticPipeline,
        depthMaterialPipeline,
        simpleResources,
        perspectiveScene
      );
      drawGltfPlayerModelBatches(
        pass,
        depthGltfPipeline,
        gltfPlayerResources,
        perspectiveScene
      );
      drawSimpleOpaqueDepthBatches(
        pass,
        depthInstancedPipeline,
        simpleResources,
        perspectiveScene
      );
    };
    diagnostics.projectileInstanceUploadBytes =
      perspectiveScene.projectileStats.projectileInstanceUploadBytes;
    diagnostics.tracerInstanceUploadBytes =
      perspectiveScene.transientVfxStats.tracerInstanceUploadBytes;
    diagnostics.explosionInstanceUploadBytes =
      perspectiveScene.transientVfxStats.explosionInstanceUploadBytes;
    const auto uploadEnd = RenderClock::now();
    diagnostics.gpuVertexUploadMilliseconds =
      millisecondsBetween(uploadStart, uploadEnd);
    diagnostics.totalUploadedVertices =
      static_cast<std::uint32_t>(vertices.size());

    const auto drawIssueStart = uploadEnd;
    RenderClock::time_point threeDimensionalEncodingStart = {};
    float staticWorldEncodingMilliseconds = 0.0F;
    if (settings.benchmarkTimingEnabled) {
      threeDimensionalEncodingStart = RenderClock::now();
    }
    std::vector<LivePointLight> pointShadowLights;
    if (settings.pointShadowQuality > 0) {
      pointShadowLights = selectPointShadowLights(
        perspectiveScene.livePointLights,
        perspectiveScene.camera,
        kMaxPointShadowLights
      );
    }
    const PointShadowPassPlan pointShadowBudget = buildPointShadowPassPlan(
      settings.pointShadowQuality,
      static_cast<std::uint32_t>(pointShadowLights.size()),
      false
    );
    if (pointShadowLights.size() > pointShadowBudget.lightCount) {
      pointShadowLights.resize(pointShadowBudget.lightCount);
    }
    const bool pointShadowResourcesReady =
      hasStaticWorld &&
      pointShadowWorldPipeline != nullptr &&
      pointShadowSampler != nullptr &&
      pointShadowFallbackTexture != nullptr;
    std::uint64_t desiredPointShadowCacheKey = 0;
    if (pointShadowBudget.lightCount > 0U && pointShadowResourcesReady) {
      desiredPointShadowCacheKey = pointShadowCacheFingerprint(
        worldMesh->arenaFingerprint,
        pointShadowLights,
        pointShadowBudget.textureSize
      );
    }
    const bool pointShadowCacheMatches =
      pointShadowResourcesReady &&
      pointShadowTexture != nullptr &&
      pointShadowSize == pointShadowBudget.textureSize &&
      pointShadowLightCount == pointShadowLights.size() &&
      pointShadowCacheKey == desiredPointShadowCacheKey;
    PointShadowPassPlan pointShadowPlan = buildPointShadowPassPlan(
      settings.pointShadowQuality,
      static_cast<std::uint32_t>(pointShadowLights.size()),
      pointShadowCacheMatches
    );
    SDL_GPUTexture* sampledPointShadowTexture =
      pointShadowFallbackTexture;
    if (pointShadowPlan.lightCount > 0U && pointShadowResourcesReady) {
      if (
        pointShadowSize != pointShadowPlan.textureSize ||
        pointShadowLightCount != pointShadowPlan.lightCount
      ) {
        pointShadowCacheKey = 0;
      }
      pointShadowTexture = ensurePointShadowTexture(
        device,
        pointShadowTexture,
        pointShadowSize,
        pointShadowLightCount,
        pointShadowPlan.textureSize,
        pointShadowPlan.lightCount,
        depthFormat
      );
      if (pointShadowTexture != nullptr) {
        sampledPointShadowTexture = pointShadowTexture;
      } else {
        pointShadowLights.clear();
        pointShadowPlan = buildPointShadowPassPlan(0, 0U, false);
      }
    } else {
      pointShadowLights.clear();
      pointShadowPlan = buildPointShadowPassPlan(0, 0U, false);
    }

    if (
      pointShadowPlan.renderCache &&
      sampledPointShadowTexture == pointShadowTexture
    ) {
      bool cachePassComplete = true;
      const SDL_GPUBufferBinding worldBinding = {
        worldMesh->vertexBuffer,
        0,
      };
      for (
        std::size_t shadowSlot = 0;
        shadowSlot < pointShadowLights.size() && cachePassComplete;
        ++shadowSlot
      ) {
        const LivePointLight& light = pointShadowLights[shadowSlot];
        const float nearPlane = std::clamp(
          std::max(0.025F, light.sourceRadius * 0.25F),
          0.025F,
          std::max(0.025F, light.radius * 0.25F)
        );
        for (std::size_t faceIndex = 0; faceIndex < 6U; ++faceIndex) {
          const PointShadowFace face =
            static_cast<PointShadowFace>(faceIndex);
          const PointShadowFaceProjection projection =
            pointShadowFaceProjection(face);
          SDL_GPUDepthStencilTargetInfo target = {};
          target.texture = pointShadowTexture;
          target.clear_depth = 1.0F;
          target.load_op = SDL_GPU_LOADOP_CLEAR;
          target.store_op = SDL_GPU_STOREOP_STORE;
          target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
          target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
          target.cycle = false;
          target.layer = static_cast<Uint8>(
            pointShadowLayer(shadowSlot, face)
          );
          SDL_GPURenderPass* pointShadowPass = SDL_BeginGPURenderPass(
            commandBuffer,
            nullptr,
            0,
            &target
          );
          if (pointShadowPass == nullptr) {
            cachePassComplete = false;
            break;
          }
          struct alignas(16) PointShadowCameraUniform {
            float origin[4] = {};
            float right[4] = {};
            float up[4] = {};
            float forward[4] = {};
            float parameters[4] = {};
          };
          const PointShadowCameraUniform uniform = {
            {light.position.x, light.position.y, light.position.z, 0.0F},
            {projection.right.x, projection.right.y, projection.right.z, 0.0F},
            {projection.up.x, projection.up.y, projection.up.z, 0.0F},
            {
              projection.forward.x,
              projection.forward.y,
              projection.forward.z,
              0.0F,
            },
            {nearPlane, light.radius, 0.0F, 0.0F},
          };
          SDL_PushGPUVertexUniformData(
            commandBuffer,
            0,
            &uniform,
            sizeof(uniform)
          );
          SDL_BindGPUGraphicsPipeline(
            pointShadowPass,
            pointShadowWorldPipeline
          );
          SDL_BindGPUVertexBuffers(
            pointShadowPass,
            0,
            &worldBinding,
            1
          );
          for (const StaticWorldBatch& batch : worldMesh->batches) {
            SDL_DrawGPUPrimitives(
              pointShadowPass,
              batch.vertexCount,
              1,
              batch.firstVertex,
              0
            );
          }
          SDL_EndGPURenderPass(pointShadowPass);
        }
      }
      if (cachePassComplete) {
        pointShadowCacheRendered = true;
        pendingPointShadowCacheKey = desiredPointShadowCacheKey;
      } else {
        sampledPointShadowTexture = pointShadowFallbackTexture;
        pointShadowLights.clear();
      }
    }
    diagnostics.shadowedPointLights =
      static_cast<std::uint32_t>(pointShadowLights.size());

    const SunShadowProjection& shadowProjection =
      perspectiveScene.lights.shadow;
    const SunShadowPassPlan shadowPlan =
      buildSunShadowPassPlan(shadowProjection.mapSize);
    SDL_GPUTexture* sampledSunShadowTexture =
      shadowPlan.useClearedFallback ? sunShadowFallbackTexture : nullptr;
    const bool staticSunShadowOnly = hasStaticWorld &&
      std::none_of(
        perspectiveScene.staticMeshBatches.begin(),
        perspectiveScene.staticMeshBatches.end(),
        [](const StaticMeshBatch& batch) {
          return batch.instanceCount > 0U &&
            batch.pass == RenderPass::OpaqueWorld;
        }
      ) && perspectiveScene.gltfPlayerModelBatches.empty();
    const std::uint64_t desiredSunShadowCacheKey =
      staticSunShadowOnly && shadowPlan.renderShadowPass
        ? sunShadowCacheFingerprint(
            worldMesh->arenaFingerprint,
            shadowProjection
          )
        : 0U;
    const bool sunShadowCacheMatches =
      staticSunShadowOnly &&
      shadowPlan.renderShadowPass &&
      sunShadowTexture != nullptr &&
      sunShadowSize == shadowPlan.textureSize &&
      sunShadowCacheKey == desiredSunShadowCacheKey;
    if (shadowPlan.renderShadowPass) {
      SDL_GPUTexture* previousSunShadowTexture = sunShadowTexture;
      sunShadowTexture = ensureSunShadowTexture(
        device,
        sunShadowTexture,
        sunShadowSize,
        shadowPlan.textureSize,
        depthFormat
      );
      if (sunShadowTexture != previousSunShadowTexture) {
        sunShadowCacheKey = 0U;
      }
      sampledSunShadowTexture = sunShadowTexture;
    }
    if (
      sampledSunShadowTexture == nullptr ||
      sunShadowSampler == nullptr
    ) {
      (void)submitCommandBuffer();
      return false;
    }
    if (shadowPlan.renderShadowPass && !sunShadowCacheMatches) {
      gpuTiming.beginPass(commandBuffer, GpuTimedPass::SunShadow);
      SDL_GPUDepthStencilTargetInfo shadowDepthTarget = {};
      shadowDepthTarget.texture = sampledSunShadowTexture;
      shadowDepthTarget.clear_depth = 1.0F;
      shadowDepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
      shadowDepthTarget.store_op = SDL_GPU_STOREOP_STORE;
      shadowDepthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
      shadowDepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
      shadowDepthTarget.cycle = true;
      SDL_GPURenderPass* shadowPass = SDL_BeginGPURenderPass(
        commandBuffer,
        nullptr,
        0,
        &shadowDepthTarget
      );
      if (shadowPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      struct alignas(16) ShadowCameraUniform {
        float origin[4];
        float right[4];
        float up[4];
        float forward[4];
        float parameters[4];
      };
      const ShadowCameraUniform shadowUniform = {
        {
          shadowProjection.origin.x,
          shadowProjection.origin.y,
          shadowProjection.origin.z,
          0.0F,
        },
        {
          shadowProjection.right.x,
          shadowProjection.right.y,
          shadowProjection.right.z,
          0.0F,
        },
        {
          shadowProjection.up.x,
          shadowProjection.up.y,
          shadowProjection.up.z,
          0.0F,
        },
        {
          shadowProjection.forward.x,
          shadowProjection.forward.y,
          shadowProjection.forward.z,
          0.0F,
        },
        {
          shadowProjection.halfExtent,
          shadowProjection.farPlane,
          static_cast<float>(shadowProjection.mapSize),
          0.0F,
        },
      };
      SDL_PushGPUVertexUniformData(
        commandBuffer,
        0,
        &shadowUniform,
        sizeof(shadowUniform)
      );
      if (hasStaticWorld) {
        SDL_BindGPUGraphicsPipeline(shadowPass, sunShadowWorldPipeline);
        const SDL_GPUBufferBinding worldBinding = {worldMesh->vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(shadowPass, 0, &worldBinding, 1);
        for (const StaticWorldBatch& batch : worldMesh->batches) {
          SDL_DrawGPUPrimitives(
            shadowPass,
            batch.vertexCount,
            1,
            batch.firstVertex,
            0
          );
        }
      }
      drawStaticMeshBatches(
        shadowPass,
        sunShadowStaticPipeline,
        sunShadowMaterialPipeline,
        simpleResources,
        perspectiveScene,
        RenderPass::OpaqueWorld,
        false,
        nullptr,
        true
      );
      // Shadow depth has no alpha output, so skip any remote visual that has
      // started fading instead of letting it cast a full-strength shadow.
      drawGltfPlayerModelBatches(
        shadowPass,
        sunShadowGltfPipeline,
        gltfPlayerResources,
        perspectiveScene,
        false,
        true
      );
      SDL_EndGPURenderPass(shadowPass);
      gpuTiming.endPass(commandBuffer, GpuTimedPass::SunShadow);
      sunShadowCacheKey = staticSunShadowOnly
        ? desiredSunShadowCacheKey
        : 0U;
    } else if (!staticSunShadowOnly || !shadowPlan.renderShadowPass) {
      sunShadowCacheKey = 0U;
    }

    SDL_GPUColorTargetInfo colorTarget = {};
    if (directPresent) {
      colorTarget.texture = swapchainTexture;
      colorTarget.resolve_texture = nullptr;
      colorTarget.clear_color = {
        kDirectSdrClearColor.red,
        kDirectSdrClearColor.green,
        kDirectSdrClearColor.blue,
        1.0F,
      };
      colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    } else {
      sceneColorTexture = ensureSampledColorTexture(
        device,
        sceneColorTexture,
        sceneColorWidth,
        sceneColorHeight,
        outputWidth,
        outputHeight,
        sceneColorFormat
      );
      if (sceneColorTexture == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      msaaColorTexture = ensureMsaaColorTexture(
        device,
        msaaColorTexture,
        msaaColorWidth,
        msaaColorHeight,
        outputWidth,
        outputHeight,
        sceneColorFormat,
        sampleCount
      );
      if (
        sampleCount != SDL_GPU_SAMPLECOUNT_1 &&
        msaaColorTexture == nullptr
      ) {
        (void)submitCommandBuffer();
        return false;
      }
      colorTarget.texture = sampleCount == SDL_GPU_SAMPLECOUNT_1
        ? sceneColorTexture
        : msaaColorTexture;
      colorTarget.resolve_texture = sampleCount == SDL_GPU_SAMPLECOUNT_1
        ? nullptr
        : sceneColorTexture;
      colorTarget.clear_color = {
        kNeutralHdrSceneClearColor.red,
        kNeutralHdrSceneClearColor.green,
        kNeutralHdrSceneClearColor.blue,
        1.0F,
      };
      colorTarget.store_op = sampleCount == SDL_GPU_SAMPLECOUNT_1
        ? SDL_GPU_STOREOP_STORE
        : SDL_GPU_STOREOP_RESOLVE;
    }
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTexture = ensureDepthTexture(
      device,
      depthTexture,
      depthWidth,
      depthHeight,
      outputWidth,
      outputHeight,
      depthFormat,
      sampleCount
    );
    if (depthTexture == nullptr) {
      (void)submitCommandBuffer();
      return false;
    }

    SDL_GPUDepthStencilTargetInfo depthTarget = {};
    depthTarget.texture = depthTexture;
    depthTarget.clear_depth = 1.0F;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    // Native outlines reuse the world depth so the mask stays hidden by the
    // same geometry without a second opaque-world draw.
    depthTarget.store_op =
      settings.playerOutlineMode == PlayerOutlineMode::NativeScreenSpace &&
          sampleCount == SDL_GPU_SAMPLECOUNT_1
        ? SDL_GPU_STOREOP_STORE
        : SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle = true;
    gpuTiming.beginPass(commandBuffer, GpuTimedPass::MainScene);
    SDL_GPURenderPass* worldPass = SDL_BeginGPURenderPass(
      commandBuffer,
      &colorTarget,
      1,
      &depthTarget
    );
    if (worldPass == nullptr) {
      (void)submitCommandBuffer();
      return false;
    }
      SDL_GPUGraphicsPipeline* activeSkyPipeline =
        directPresent ? directSkyPipeline : skyPipeline;
      if (
        activeSkyPipeline != nullptr &&
        skyTexture != nullptr &&
        skySampler != nullptr
      ) {
        struct alignas(16) SkyCameraUniform {
          float right[4];
          float up[4];
          float forward[4];
          float projection[4];
        };
        const SkyCameraUniform skyCameraUniform = {
          {
            worldCamera.right.x,
            worldCamera.right.y,
            worldCamera.right.z,
            0.0F,
          },
          {
            worldCamera.up.x,
            worldCamera.up.y,
            worldCamera.up.z,
            0.0F,
          },
          {
            worldCamera.forward.x,
            worldCamera.forward.y,
            worldCamera.forward.z,
            0.0F,
          },
          {
            worldCamera.focalLength,
            worldCamera.aspectRatio,
            0.0F,
            0.0F,
          },
        };
        SDL_PushGPUVertexUniformData(
          commandBuffer,
          0,
          &skyCameraUniform,
          sizeof(skyCameraUniform)
        );
        SDL_BindGPUGraphicsPipeline(worldPass, activeSkyPipeline);
        const SDL_GPUTextureSamplerBinding skyBinding = {
          skyTexture,
          skySampler,
        };
        SDL_BindGPUFragmentSamplers(worldPass, 0, &skyBinding, 1);
        SDL_DrawGPUPrimitives(worldPass, 3, 1, 0, 0);
        diagnostics.skyDrawCalls = 1;
        diagnostics.skyLoadedTextures = 1;
      }
      struct alignas(16) SceneLightUniform {
        float parameters[4] = {};
        float positionRadius[kMaxLivePointLights][4] = {};
        float colorIntensity[kMaxLivePointLights][4] = {};
        float lightParameters[kMaxLivePointLights][4] = {};
        float pointShadowParameters[4] = {};
        float sunDirectionIntensity[4] = {};
        float sunColor[4] = {};
        float fillColorIntensity[4] = {};
        float shadowOrigin[4] = {};
        float shadowRight[4] = {};
        float shadowUp[4] = {};
        float shadowForward[4] = {};
        float shadowParameters[4] = {};
        float postParameters[4] = {};
      };
      struct alignas(16) DirectLightUniform {
        float sunDirectionIntensity[4] = {};
        float sunColor[4] = {};
        float fillColorIntensity[4] = {};
      };
      SceneLightUniform sceneLightUniform;
      DirectLightUniform directLightUniform;
      const std::size_t lightCount =
        std::min<std::size_t>(
          perspectiveScene.livePointLights.size(),
          kMaxLivePointLights
        );
      sceneLightUniform.parameters[0] = static_cast<float>(lightCount);
      sceneLightUniform.parameters[1] = perspectiveScene.lights.exposure;
      sceneLightUniform.parameters[2] =
        static_cast<float>(perspectiveScene.lights.gradeQuality);
      sceneLightUniform.parameters[3] =
        static_cast<float>(perspectiveScene.lights.materialQuality);
      for (std::size_t index = 0; index < lightCount; ++index) {
        const LivePointLight& light = perspectiveScene.livePointLights[index];
        sceneLightUniform.positionRadius[index][0] = light.position.x;
        sceneLightUniform.positionRadius[index][1] = light.position.y;
        sceneLightUniform.positionRadius[index][2] = light.position.z;
        sceneLightUniform.positionRadius[index][3] = light.radius;
        sceneLightUniform.colorIntensity[index][0] = light.color.x;
        sceneLightUniform.colorIntensity[index][1] = light.color.y;
        sceneLightUniform.colorIntensity[index][2] = light.color.z;
        sceneLightUniform.colorIntensity[index][3] = light.intensity;
        sceneLightUniform.lightParameters[index][0] = light.sourceRadius;
        sceneLightUniform.lightParameters[index][1] =
          light.affectsStaticWorld ? 1.0F : 0.0F;
        for (
          std::size_t shadowSlot = 0;
          shadowSlot < pointShadowLights.size();
          ++shadowSlot
        ) {
          if (
            light.authored &&
            light.sourceIndex == pointShadowLights[shadowSlot].sourceIndex
          ) {
            sceneLightUniform.lightParameters[index][2] =
              static_cast<float>(shadowSlot + 1U);
            break;
          }
        }
        sceneLightUniform.lightParameters[index][3] = light.selectionFade;
      }
      sceneLightUniform.pointShadowParameters[0] =
        pointShadowLights.empty()
          ? 0.0F
          : static_cast<float>(pointShadowPlan.textureSize);
      sceneLightUniform.pointShadowParameters[1] =
        static_cast<float>(pointShadowLights.size());
      sceneLightUniform.pointShadowParameters[2] = 0.0015F;
      const SceneLightData& lights = perspectiveScene.lights;
      sceneLightUniform.sunDirectionIntensity[0] = lights.sunDirection.x;
      sceneLightUniform.sunDirectionIntensity[1] = lights.sunDirection.y;
      sceneLightUniform.sunDirectionIntensity[2] = lights.sunDirection.z;
      sceneLightUniform.sunDirectionIntensity[3] = lights.sunIntensity;
      std::copy(
        std::begin(sceneLightUniform.sunDirectionIntensity),
        std::end(sceneLightUniform.sunDirectionIntensity),
        std::begin(directLightUniform.sunDirectionIntensity)
      );
      sceneLightUniform.sunColor[0] = lights.sunColor.x;
      sceneLightUniform.sunColor[1] = lights.sunColor.y;
      sceneLightUniform.sunColor[2] = lights.sunColor.z;
      std::copy(
        std::begin(sceneLightUniform.sunColor),
        std::end(sceneLightUniform.sunColor),
        std::begin(directLightUniform.sunColor)
      );
      sceneLightUniform.fillColorIntensity[0] = lights.fillColor.x;
      sceneLightUniform.fillColorIntensity[1] = lights.fillColor.y;
      sceneLightUniform.fillColorIntensity[2] = lights.fillColor.z;
      sceneLightUniform.fillColorIntensity[3] = lights.fillIntensity;
      std::copy(
        std::begin(sceneLightUniform.fillColorIntensity),
        std::end(sceneLightUniform.fillColorIntensity),
        std::begin(directLightUniform.fillColorIntensity)
      );
      const SunShadowProjection& shadow = lights.shadow;
      sceneLightUniform.shadowOrigin[0] = shadow.origin.x;
      sceneLightUniform.shadowOrigin[1] = shadow.origin.y;
      sceneLightUniform.shadowOrigin[2] = shadow.origin.z;
      sceneLightUniform.shadowOrigin[3] = shadow.normalBias;
      sceneLightUniform.shadowRight[0] = shadow.right.x;
      sceneLightUniform.shadowRight[1] = shadow.right.y;
      sceneLightUniform.shadowRight[2] = shadow.right.z;
      sceneLightUniform.shadowUp[0] = shadow.up.x;
      sceneLightUniform.shadowUp[1] = shadow.up.y;
      sceneLightUniform.shadowUp[2] = shadow.up.z;
      sceneLightUniform.shadowForward[0] = shadow.forward.x;
      sceneLightUniform.shadowForward[1] = shadow.forward.y;
      sceneLightUniform.shadowForward[2] = shadow.forward.z;
      sceneLightUniform.shadowParameters[0] = shadow.halfExtent;
      sceneLightUniform.shadowParameters[1] = shadow.farPlane;
      sceneLightUniform.shadowParameters[2] =
        static_cast<float>(shadow.mapSize);
      sceneLightUniform.shadowParameters[3] = shadow.depthBias;
      sceneLightUniform.postParameters[0] =
        directPresent ? -1.0F : (bloomEffective ? 1.0F : 0.0F);
      sceneLightUniform.postParameters[1] =
        std::clamp(settings.bloomThreshold, 0.5F, 4.0F);
      sceneLightUniform.postParameters[2] =
        std::clamp(settings.bloomIntensity, 0.0F, 1.0F);
      const auto pushActiveLightUniform = [&]() {
        if (directPresent) {
          SDL_PushGPUFragmentUniformData(
            commandBuffer,
            0,
            &directLightUniform,
            sizeof(directLightUniform)
          );
        } else {
          SDL_PushGPUFragmentUniformData(
            commandBuffer,
            0,
            &sceneLightUniform,
            sizeof(sceneLightUniform)
          );
        }
      };
      if (hasStaticWorld || dynamic3DVertexCount > 0 || !perspectiveScene.simpleInstances.empty()) {
        const auto worldDrawStart = RenderClock::now();
        if (hasStaticWorld) {
          diagnostics.worldSourceTriangles = worldMesh->sourceTriangles;
          diagnostics.worldRenderedTriangles =
            worldMesh->sourceTriangles - worldMesh->duplicateTrianglesCulled;
          diagnostics.worldDuplicateTrianglesCulled =
            worldMesh->duplicateTrianglesCulled;
          diagnostics.worldVertexCount = worldMesh->vertexCount;
          diagnostics.worldDrawCalls = 0U;
          diagnostics.worldSubmittedRanges = 0U;
          diagnostics.worldTotalChunks =
            static_cast<std::uint32_t>(worldMesh->chunkBatches.size());
          const bool submittedCulledWorld =
            settings.worldFrustumCull && worldMesh->useCulledBatches;
          // Visibility telemetry describes the geometry actually submitted.
          // The adaptive policy may deliberately keep aggregate full-world
          // draws when extra ranges cost more than the rejected triangles.
          diagnostics.worldVisibleChunks = submittedCulledWorld
            ? worldMesh->visibilityScratch.visibleChunkCount
            : diagnostics.worldTotalChunks;
          diagnostics.worldCulledChunks =
            diagnostics.worldTotalChunks - diagnostics.worldVisibleChunks;
          diagnostics.worldVisibilityTestedNodes = settings.worldFrustumCull
            ? worldMesh->visibilityScratch.testedNodes
            : 0U;
          diagnostics.worldLoadedTextures = worldMesh->loadedTextures;
          diagnostics.worldMissingTextures = worldMesh->missingTextures;
          diagnostics.worldReferencedMaterials = worldMesh->referencedMaterials;
          diagnostics.worldTextureFilter = worldMesh->samplerTextureFilter;
          diagnostics.worldRequestedTextureAnisotropy =
            worldMesh->samplerTextureAnisotropy;
          diagnostics.worldAppliedTextureAnisotropy =
            worldMesh->samplerAppliedTextureAnisotropy;
          diagnostics.worldTextureLodBias = worldMesh->samplerTextureLodBias;
          diagnostics.ambientGroundingQuality = worldMesh->ambientQuality;
          diagnostics.ambientStaticRays = worldMesh->ambientBakeStats.raysCast;
          diagnostics.ambientStaticSamples =
            worldMesh->ambientBakeStats.uniqueSamples;
          diagnostics.ambientStaticCacheHits =
            worldMesh->ambientBakeStats.cacheHits;
          diagnostics.ambientStaticMinimum =
            worldMesh->ambientBakeStats.minimumVisibility;
          diagnostics.ambientStaticMaximum =
            worldMesh->ambientBakeStats.maximumVisibility;
          diagnostics.ambientProbeCount = static_cast<std::uint32_t>(
            worldMesh->ambientProbeGrid.visibility.size()
          );
          diagnostics.ambientProbeRays = worldMesh->ambientProbeGrid.raysCast;
          diagnostics.ambientProbeBytes = worldMesh->ambientProbeGrid.byteSize();
          diagnostics.ambientProbeFingerprint =
            worldMesh->ambientProbeGrid.fingerprint;
          diagnostics.ambientProbeBuildMilliseconds =
            worldMesh->ambientProbeGrid.buildMilliseconds;
          for (const WorldTexture& texture : worldMesh->textures) {
            diagnostics.worldMaxTextureMipLevels = std::max(
              diagnostics.worldMaxTextureMipLevels,
              texture.mipLevels
            );
          }
        }
        SDL_PushGPUVertexUniformData(
          commandBuffer,
          0,
          &worldCameraUniform,
          sizeof(worldCameraUniform)
        );
        pushActiveLightUniform();
        SDL_BindGPUGraphicsPipeline(
          worldPass,
          directPresent ? directWorldSurfacePipeline : pipelineWorldSurface
        );
        const std::array<SDL_GPUTextureSamplerBinding, 2> shadowBindings = {{
          {
            sampledSunShadowTexture,
            sunShadowSampler,
          },
          {
            sampledPointShadowTexture,
            pointShadowSampler,
          },
        }};
        const SDL_GPUTextureSamplerBinding pointShadowBinding = {
          sampledPointShadowTexture,
          pointShadowSampler,
        };
        if (!directPresent) {
          SDL_BindGPUFragmentSamplers(
            worldPass,
            1,
            shadowBindings.data(),
            static_cast<Uint32>(shadowBindings.size())
          );
        }
        if (hasStaticWorld) {
          const SDL_GPUBufferBinding staticBinding = {worldMesh->vertexBuffer, 0};
          SDL_BindGPUVertexBuffers(worldPass, 0, &staticBinding, 1);
          RenderClock::time_point staticWorldStart = {};
          if (settings.benchmarkTimingEnabled) {
            staticWorldStart = RenderClock::now();
          }
          drawStaticWorldGeometry(
            commandBuffer,
            worldPass,
            *worldMesh,
            settings.worldFrustumCull,
            &diagnostics
          );
          if (settings.benchmarkTimingEnabled) {
            staticWorldEncodingMilliseconds +=
              millisecondsBetween(staticWorldStart, RenderClock::now());
          }
        }
        SDL_BindGPUGraphicsPipeline(
          worldPass,
          directPresent ? directWorldPipeline : pipeline3D
        );
        const SDL_GPUBufferBinding binding = {vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(worldPass, 0, &binding, 1);
        if (worldAtlas != nullptr) {
          const SDL_GPUTextureSamplerBinding worldBinding = {
            worldAtlas->texture,
            worldAtlas->sampler,
          };
          SDL_BindGPUFragmentSamplers(worldPass, 0, &worldBinding, 1);
        } else if (
          hasStaticWorld &&
          !worldMesh->textures.empty() &&
          worldMesh->textures.front().texture != nullptr
        ) {
          const SDL_GPUTextureSamplerBinding whiteBinding = {
            worldMesh->textures.front().texture,
            worldMesh->sampler,
          };
          SDL_BindGPUFragmentSamplers(worldPass, 0, &whiteBinding, 1);
        }
        if (opaqueDynamicVertexCount > 0) {
          SDL_DrawGPUPrimitives(
            worldPass,
            opaqueDynamicVertexCount,
            1,
            0,
            0
          );
        }
        const Uint32 translucentVertexCount =
          dynamic3DVertexCount - opaqueDynamicVertexCount;
        if (translucentVertexCount > 0) {
          SDL_BindGPUGraphicsPipeline(
            worldPass,
            pipeline3DTranslucent
          );
          if (worldAtlas != nullptr) {
            const SDL_GPUTextureSamplerBinding worldBinding = {
              worldAtlas->texture,
              worldAtlas->sampler,
            };
            SDL_BindGPUFragmentSamplers(worldPass, 0, &worldBinding, 1);
          } else if (
            hasStaticWorld &&
            !worldMesh->textures.empty() &&
            worldMesh->textures.front().texture != nullptr
          ) {
            const SDL_GPUTextureSamplerBinding whiteBinding = {
              worldMesh->textures.front().texture,
              worldMesh->sampler,
            };
            SDL_BindGPUFragmentSamplers(worldPass, 0, &whiteBinding, 1);
          }
          SDL_DrawGPUPrimitives(
            worldPass,
            translucentVertexCount,
            1,
            opaqueDynamicVertexCount,
            0
          );
        }
        drawStaticMeshBatches(
          worldPass,
          directPresent ? directStaticMeshPipeline : staticMeshPipeline,
          directPresent ? directMaterialMeshPipeline : materialMeshPipeline,
          simpleResources,
          perspectiveScene,
          RenderPass::OpaqueWorld,
          !directPresent,
          directPresent ? nullptr : &pointShadowBinding
        );
        const GltfMaterialQualityPlan gltfMaterialPlan =
          gltfMaterialQualityPlan(
            perspectiveScene.lights.materialQuality,
            gltfPlayerResources != nullptr &&
              gltfPlayerResources->authoredMaterialTexturesReady
          );
        diagnostics.gltfMaterialTextureBinds =
          gltfMaterialPlan.samplesAlbedo &&
          !perspectiveScene.gltfPlayerModelBatches.empty()
            ? 1U
            : 0U;
        if (!directPresent) {
          SDL_BindGPUFragmentSamplers(
            worldPass,
            gltfMaterialPlan.samplesAlbedo ? 2U : 0U,
            shadowBindings.data(),
            static_cast<Uint32>(shadowBindings.size())
          );
        }
        drawGltfPlayerModelBatches(
          worldPass,
          directPresent
            ? directGltfPlayerPipeline
            : gltfMaterialPlan.samplesAlbedo
              ? gltfPlayerModelPipeline
              : gltfPlayerModelFlatPipeline,
          gltfPlayerResources,
          perspectiveScene,
          gltfMaterialPlan.samplesAlbedo
        );
        if (!directPresent) {
          SDL_BindGPUFragmentSamplers(
            worldPass,
            0,
            &pointShadowBinding,
            1
          );
        }
        drawSimpleInstanceBatches(
          worldPass,
          directPresent ? directInstancedMeshPipeline : instancedMeshPipeline,
          directPresent ? nullptr : instancedGlowPipeline,
          simpleResources,
          perspectiveScene
        );
        diagnostics.worldDrawIssueMilliseconds =
          millisecondsBetween(worldDrawStart, RenderClock::now());
        if (textureDebugEnabled()) {
          static std::uint32_t loggedFrames = 0;
          if ((loggedFrames++ % 120U) == 0U) {
            std::cerr
              << "LG_DUEL_TEXTURE_PIPELINE_V2 world frame counters"
              << " sourceTriangles=" << diagnostics.worldSourceTriangles
              << " renderedTriangles=" << diagnostics.worldRenderedTriangles
              << " submittedTriangles=" << diagnostics.worldSubmittedTriangles
              << " duplicateTrianglesCulled="
              << diagnostics.worldDuplicateTrianglesCulled
              << " staticVertices=" << diagnostics.worldVertexCount
              << " drawCalls=" << diagnostics.worldDrawCalls
              << " visibleChunks=" << diagnostics.worldVisibleChunks
              << '/' << diagnostics.worldTotalChunks
              << " loadedTextures=" << diagnostics.worldLoadedTextures
              << " referencedMaterials=" << diagnostics.worldReferencedMaterials
              << " sceneBuildMs=" << diagnostics.sceneBuildMilliseconds
              << " dynamicUploadMs=" << diagnostics.gpuVertexUploadMilliseconds
              << " worldDrawIssueMs=" << diagnostics.worldDrawIssueMilliseconds
              << '\n';
          }
        }
      }
    SDL_EndGPURenderPass(worldPass);
    gpuTiming.endPass(commandBuffer, GpuTimedPass::MainScene);

    colorTarget.texture = directPresent ? swapchainTexture : sceneColorTexture;
    colorTarget.resolve_texture = nullptr;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
    // The final composite samples this slot on every frame. Keep a stored
    // clear value when no viewmodel draws so the binding never goes stale.
    viewModelDepthTexture = ensureDepthTexture(
      device,
      viewModelDepthTexture,
      viewModelDepthWidth,
      viewModelDepthHeight,
      outputWidth,
      outputHeight,
      depthFormat,
      SDL_GPU_SAMPLECOUNT_1,
      true
    );
    if (viewModelDepthTexture == nullptr) {
      (void)submitCommandBuffer();
      return false;
    }
    SDL_GPUDepthStencilTargetInfo viewModelDepthTarget = {};
    viewModelDepthTarget.texture = viewModelDepthTexture;
    viewModelDepthTarget.clear_depth = 1.0F;
    viewModelDepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    viewModelDepthTarget.store_op = directPresent
      ? SDL_GPU_STOREOP_DONT_CARE
      : SDL_GPU_STOREOP_STORE;
    viewModelDepthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    viewModelDepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    viewModelDepthTarget.cycle = true;
    if (perspectiveScene.viewModelStats.drawCalls > 0U) {
      gpuTiming.beginPass(commandBuffer, GpuTimedPass::ViewModel);
      SDL_GPURenderPass* viewModelPass = SDL_BeginGPURenderPass(
        commandBuffer,
        &colorTarget,
        1,
        &viewModelDepthTarget
      );
      if (viewModelPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      struct alignas(16) ViewModelCameraUniform {
        float position[4];
        float right[4];
        float up[4];
        float forward[4];
        float projection[4];
      };
      PerspectiveCamera viewModelCamera = perspectiveScene.camera;
      if (settings.localSelectedWeapon == Weapon::FreezeGun) {
        constexpr float kPi = 3.14159265359F;
        const float viewModelFov =
          std::max(50.0F, settings.fieldOfView - 10.0F);
        viewModelCamera.focalLength = 1.0F / std::tan(
          viewModelFov * (kPi / 180.0F) * 0.5F
        );
      }
      const ViewModelCameraUniform viewModelUniform = {
        {
          viewModelCamera.position.x,
          viewModelCamera.position.y,
          viewModelCamera.position.z,
          0.0F,
        },
        {
          viewModelCamera.right.x,
          viewModelCamera.right.y,
          viewModelCamera.right.z,
          0.0F,
        },
        {
          viewModelCamera.up.x,
          viewModelCamera.up.y,
          viewModelCamera.up.z,
          0.0F,
        },
        {
          viewModelCamera.forward.x,
          viewModelCamera.forward.y,
          viewModelCamera.forward.z,
          0.0F,
        },
        {
          viewModelCamera.focalLength,
          viewModelCamera.aspectRatio,
          viewModelCamera.nearPlane,
          512.0F,
        },
      };
      SceneLightUniform viewModelLights = sceneLightUniform;
      viewModelLights.postParameters[3] = 1.0F;
      SDL_PushGPUVertexUniformData(
        commandBuffer,
        0,
        &viewModelUniform,
        sizeof(viewModelUniform)
      );
      if (directPresent) {
        SDL_PushGPUFragmentUniformData(
          commandBuffer,
          0,
          &directLightUniform,
          sizeof(directLightUniform)
        );
      } else {
        SDL_PushGPUFragmentUniformData(
          commandBuffer,
          0,
          &viewModelLights,
          sizeof(viewModelLights)
        );
      }
      const std::array<SDL_GPUTextureSamplerBinding, 2>
        viewModelShadowBindings = {{
          {sampledSunShadowTexture, sunShadowSampler},
          {sampledPointShadowTexture, pointShadowSampler},
        }};
      const SDL_GPUTextureSamplerBinding
        viewModelPointShadowBinding = {
          sampledPointShadowTexture,
          pointShadowSampler,
        };
      if (!directPresent) {
        SDL_BindGPUFragmentSamplers(
          viewModelPass,
          1,
          viewModelShadowBindings.data(),
          static_cast<Uint32>(viewModelShadowBindings.size())
        );
      }
      drawStaticMeshBatches(
        viewModelPass,
        directPresent ? directStaticMeshPipeline : staticMeshViewModelPipeline,
        directPresent
          ? directMaterialMeshPipeline
          : materialMeshViewModelPipeline,
        simpleResources,
        perspectiveScene,
        RenderPass::ViewModel,
        !directPresent,
        directPresent ? nullptr : &viewModelPointShadowBinding
      );
      SDL_EndGPURenderPass(viewModelPass);
      gpuTiming.endPass(commandBuffer, GpuTimedPass::ViewModel);
    } else if (bloomEffective) {
      SDL_GPURenderPass* viewModelDepthClearPass =
        SDL_BeginGPURenderPass(
          commandBuffer,
          nullptr,
          0,
          &viewModelDepthTarget
        );
      if (viewModelDepthClearPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_EndGPURenderPass(viewModelDepthClearPass);
    }

    if (!directPresent) {
    const PostProcessPlan postPlan = buildPostProcessPlan(
      outputWidth,
      outputHeight,
      bloomEffective
    );
    diagnostics.sceneColorWidth = postPlan.sceneWidth;
    diagnostics.sceneColorHeight = postPlan.sceneHeight;
    diagnostics.sceneCompositePasses = postPlan.sceneCompositePasses;
    diagnostics.sceneCompositeEnabled = true;
    diagnostics.bloomEnabled = postPlan.bloomEnabled;
    if (postPlan.bloomEnabled) {
      gpuTiming.beginPass(commandBuffer, GpuTimedPass::Bloom);
      if (
        bloomTextureB != nullptr &&
        (
          bloomWidth != postPlan.bloomWidth ||
          bloomHeight != postPlan.bloomHeight
        )
      ) {
        SDL_ReleaseGPUTexture(device, bloomTextureB);
        bloomTextureB = nullptr;
      }
      bloomTextureA = ensureSampledColorTexture(
        device,
        bloomTextureA,
        bloomWidth,
        bloomHeight,
        postPlan.bloomWidth,
        postPlan.bloomHeight,
        sceneColorFormat
      );
      Uint32 bloomScratchWidth = bloomWidth;
      Uint32 bloomScratchHeight = bloomHeight;
      bloomTextureB = ensureSampledColorTexture(
        device,
        bloomTextureB,
        bloomScratchWidth,
        bloomScratchHeight,
        postPlan.bloomWidth,
        postPlan.bloomHeight,
        sceneColorFormat
      );
      bloomDepthTexture = ensureDepthTexture(
        device,
        bloomDepthTexture,
        bloomDepthWidth,
        bloomDepthHeight,
        postPlan.bloomWidth,
        postPlan.bloomHeight,
        depthFormat
      );
      if (
        bloomTextureA == nullptr ||
        bloomTextureB == nullptr ||
        bloomDepthTexture == nullptr
      ) {
        (void)submitCommandBuffer();
        return false;
      }
      const SDL_GPUViewport bloomViewport = {
        0.0F,
        0.0F,
        static_cast<float>(postPlan.bloomWidth),
        static_cast<float>(postPlan.bloomHeight),
        0.0F,
        1.0F,
      };
      SDL_GPUDepthStencilTargetInfo bloomDepthTarget = {};
      bloomDepthTarget.texture = bloomDepthTexture;
      bloomDepthTarget.clear_depth = 1.0F;
      bloomDepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
      bloomDepthTarget.store_op = SDL_GPU_STOREOP_STORE;
      bloomDepthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
      bloomDepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
      bloomDepthTarget.cycle = true;
      SDL_GPURenderPass* bloomDepthPass = SDL_BeginGPURenderPass(
        commandBuffer,
        nullptr,
        0,
        &bloomDepthTarget
      );
      if (bloomDepthPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_SetGPUViewport(bloomDepthPass, &bloomViewport);
      // Viewmodels use another projection. Restore the world camera before
      // both the occluder rebuild and selective glow redraw.
      SDL_PushGPUVertexUniformData(
        commandBuffer,
        0,
        &worldCameraUniform,
        sizeof(worldCameraUniform)
      );
      drawOpaqueDepth(bloomDepthPass);
      SDL_EndGPURenderPass(bloomDepthPass);

      SDL_GPUColorTargetInfo bloomTarget = {};
      bloomTarget.texture = bloomTextureA;
      bloomTarget.clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
      bloomTarget.load_op = SDL_GPU_LOADOP_CLEAR;
      bloomTarget.store_op = SDL_GPU_STOREOP_STORE;
      bloomTarget.cycle = true;
      bloomDepthTarget.load_op = SDL_GPU_LOADOP_LOAD;
      bloomDepthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
      bloomDepthTarget.cycle = false;
      SDL_GPURenderPass* bloomSourcePass = SDL_BeginGPURenderPass(
        commandBuffer,
        &bloomTarget,
        1,
        &bloomDepthTarget
      );
      if (bloomSourcePass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_SetGPUViewport(bloomSourcePass, &bloomViewport);
      SDL_PushGPUVertexUniformData(
        commandBuffer,
        0,
        &worldCameraUniform,
        sizeof(worldCameraUniform)
      );
      SDL_PushGPUFragmentUniformData(
        commandBuffer,
        0,
        &sceneLightUniform,
        sizeof(sceneLightUniform)
      );
      drawSimpleGlowBatches(
        bloomSourcePass,
        bloomSourcePipeline,
        simpleResources,
        perspectiveScene
      );
      SDL_EndGPURenderPass(bloomSourcePass);

      struct alignas(16) BlurUniform {
        float parameters[4];
      };
      const std::array<std::pair<SDL_GPUTexture*, SDL_GPUTexture*>, 2>
        blurTextures = {{
          {bloomTextureA, bloomTextureB},
          {bloomTextureB, bloomTextureA},
        }};
      for (std::size_t passIndex = 0; passIndex < blurTextures.size(); ++passIndex) {
        bloomTarget.texture = blurTextures[passIndex].second;
        bloomTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        SDL_GPURenderPass* blurPass =
          SDL_BeginGPURenderPass(commandBuffer, &bloomTarget, 1, nullptr);
        if (blurPass == nullptr) {
          (void)submitCommandBuffer();
          return false;
        }
        SDL_BindGPUGraphicsPipeline(blurPass, bloomBlurPipeline);
        const SDL_GPUTextureSamplerBinding blurBinding = {
          blurTextures[passIndex].first,
          postProcessSampler,
        };
        SDL_BindGPUFragmentSamplers(blurPass, 0, &blurBinding, 1);
        const BlurUniform blurUniform = {{
          passIndex == 0U ? 1.0F / static_cast<float>(postPlan.bloomWidth) : 0.0F,
          passIndex == 1U ? 1.0F / static_cast<float>(postPlan.bloomHeight) : 0.0F,
          0.0F,
          0.0F,
        }};
        SDL_PushGPUFragmentUniformData(
          commandBuffer,
          0,
          &blurUniform,
          sizeof(blurUniform)
        );
        SDL_DrawGPUPrimitives(blurPass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(blurPass);
      }
      diagnostics.bloomWidth = postPlan.bloomWidth;
      diagnostics.bloomHeight = postPlan.bloomHeight;
      diagnostics.bloomPasses = postPlan.bloomPasses;
      gpuTiming.endPass(commandBuffer, GpuTimedPass::Bloom);
    }

    colorTarget.texture = swapchainTexture;
    colorTarget.resolve_texture = nullptr;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.clear_color = {0.0F, 0.0F, 0.0F, 1.0F};
    gpuTiming.beginPass(commandBuffer, GpuTimedPass::SceneComposite);
    SDL_GPURenderPass* sceneCompositePass =
      SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
    if (sceneCompositePass == nullptr) {
      (void)submitCommandBuffer();
      return false;
    }
    SDL_BindGPUGraphicsPipeline(
      sceneCompositePass,
      postPlan.bloomEnabled
        ? sceneCompositePipeline
        : sceneCompositeNoBloomPipeline
    );
    if (postPlan.bloomEnabled) {
      const std::array<SDL_GPUTextureSamplerBinding, 3> sceneBindings = {{
        {sceneColorTexture, postProcessSampler},
        {bloomTextureA, postProcessSampler},
        {viewModelDepthTexture, postProcessSampler},
      }};
      SDL_BindGPUFragmentSamplers(
        sceneCompositePass,
        0,
        sceneBindings.data(),
        static_cast<Uint32>(sceneBindings.size())
      );
    } else {
      const SDL_GPUTextureSamplerBinding sceneBinding = {
        sceneColorTexture,
        postProcessSampler,
      };
      SDL_BindGPUFragmentSamplers(
        sceneCompositePass,
        0,
        &sceneBinding,
        1
      );
    }
    struct alignas(16) CompositeUniform {
      float parameters[4];
    };
    const CompositeUniform compositeUniform = {{
      perspectiveScene.lights.exposure,
      static_cast<float>(perspectiveScene.lights.gradeQuality),
      postPlan.bloomEnabled
        ? std::clamp(settings.bloomIntensity, 0.0F, 1.0F)
        : 0.0F,
      clampedDisplayGamma(settings.displayGamma),
    }};
    SDL_PushGPUFragmentUniformData(
      commandBuffer,
      0,
      &compositeUniform,
      sizeof(compositeUniform)
    );
    SDL_DrawGPUPrimitives(sceneCompositePass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(sceneCompositePass);
    gpuTiming.endPass(commandBuffer, GpuTimedPass::SceneComposite);
    colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
    } else {
      // Direct mode has already written display-ready color to the
      // swapchain. Outlines and HUD keep their normal LOAD order.
      diagnostics.sceneColorWidth = 0U;
      diagnostics.sceneColorHeight = 0U;
      diagnostics.sceneCompositePasses = 0U;
      diagnostics.sceneCompositeEnabled = false;
      diagnostics.bloomWidth = 0U;
      diagnostics.bloomHeight = 0U;
      diagnostics.bloomPasses = 0U;
      diagnostics.bloomEnabled = false;
      colorTarget.texture = swapchainTexture;
      colorTarget.resolve_texture = nullptr;
      colorTarget.store_op = SDL_GPU_STOREOP_STORE;
      colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
    }

    const OutlineWorkPlan outlinePlan = buildOutlineWorkPlan(
      perspectiveScene.camera,
      std::span<const Vertex3D>(
        perspectiveScene.vertices.data(),
        perspectiveScene.vertices.size()
      ),
      std::span<const StaticMeshInstance>(
        perspectiveScene.staticMeshInstances.data(),
        perspectiveScene.staticMeshInstances.size()
      ),
      std::span<const GltfPlayerModelInstance>(
        perspectiveScene.gltfPlayerModelInstances.data(),
        perspectiveScene.gltfPlayerModelInstances.size()
      ),
      std::span<const OutlineMaskDraw>(
        perspectiveScene.outlineMaskDraws.data(),
        perspectiveScene.outlineMaskDraws.size()
      ),
      outputWidth,
      outputHeight,
      settings.playerOutlineMode == PlayerOutlineMode::NativeScreenSpace
        ? 1.0F
        : kOutlineWorkScale
    );
    const bool nativeOutline =
      settings.playerOutlineMode == PlayerOutlineMode::NativeScreenSpace;
    // SDL_GPU does not resolve depth. With MSAA, rebuild a 1x outline depth
    // target while keeping the native mask and composite shaders.
    const OutlineDepthPlan outlineDepthPlan = buildOutlineDepthPlan(
      nativeOutline,
      sampleCount == SDL_GPU_SAMPLECOUNT_1
    );
    const bool reuseWorldDepth = outlineDepthPlan.reuseWorldDepth;
    SDL_GPUGraphicsPipeline* activeOutlineDilation = nativeOutline
      ? pipelineOutlineNativeDilation
      : pipelineOutlineDilation;
    SDL_GPUGraphicsPipeline* activeOutlineComposite = nativeOutline
      ? pipelineOutlineNativeComposite
      : pipelineOutlineComposite;
    const bool outlineDepthReady =
      reuseWorldDepth || pipelineOutlineClear != nullptr;
    const bool outlineCompositeEnabled =
      outlineDepthReady &&
      pipelineOutlineColorClear != nullptr &&
      pipelineOutlineMask != nullptr &&
      staticMeshOutlineMaskPipeline != nullptr &&
      materialMeshOutlineMaskPipeline != nullptr &&
      gltfPlayerModelOutlineMaskPipeline != nullptr &&
      activeOutlineDilation != nullptr &&
      activeOutlineComposite != nullptr &&
      outlineMaskSampler != nullptr &&
      outlinePlan.hasWork;
    if (outlineCompositeEnabled) {
      const Uint32 workWidth = outlinePlan.dimensions.workWidth;
      const Uint32 workHeight = outlinePlan.dimensions.workHeight;
      const bool outlineTargetsResized =
        outlineMaskTexture == nullptr ||
        outlineMaskWidth != workWidth ||
        outlineMaskHeight != workHeight ||
        outlineDilationTexture == nullptr ||
        outlineDilationWidth != workWidth ||
        outlineDilationHeight != workHeight ||
        (!reuseWorldDepth && (
          outlineDepthTexture == nullptr ||
          outlineDepthWidth != workWidth ||
          outlineDepthHeight != workHeight
        ));
      outlineMaskTexture = ensureOutlineMaskTexture(
        device,
        outlineMaskTexture,
        outlineMaskWidth,
        outlineMaskHeight,
        workWidth,
        workHeight
      );
      outlineDilationTexture = ensureOutlineMaskTexture(
        device,
        outlineDilationTexture,
        outlineDilationWidth,
        outlineDilationHeight,
        workWidth,
        workHeight
      );
      if (!reuseWorldDepth) {
        outlineDepthTexture = ensureDepthTexture(
          device,
          outlineDepthTexture,
          outlineDepthWidth,
          outlineDepthHeight,
          workWidth,
          workHeight,
          depthFormat
        );
      }
      if (
        outlineMaskTexture == nullptr ||
        outlineDilationTexture == nullptr ||
        (!reuseWorldDepth && outlineDepthTexture == nullptr)
      ) {
        (void)submitCommandBuffer();
        return false;
      }

      const SDL_Rect workScissor = {
        outlinePlan.workRect.x,
        outlinePlan.workRect.y,
        outlinePlan.workRect.width,
        outlinePlan.workRect.height,
      };
      const SDL_Rect compositeScissor = {
        outlinePlan.finalRect.x,
        outlinePlan.finalRect.y,
        outlinePlan.finalRect.width,
        outlinePlan.finalRect.height,
      };
      const SDL_GPUViewport outlineViewport = {
        0.0F,
        0.0F,
        static_cast<float>(workWidth),
        static_cast<float>(workHeight),
        0.0F,
        1.0F,
      };

      SDL_GPUColorTargetInfo maskColorTarget = {};
      maskColorTarget.texture = outlineMaskTexture;
      maskColorTarget.clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
      maskColorTarget.load_op =
        outlineTargetsResized ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
      maskColorTarget.store_op = SDL_GPU_STOREOP_STORE;
      maskColorTarget.cycle = outlineTargetsResized;

      SDL_GPUDepthStencilTargetInfo maskDepthTarget = {};
      maskDepthTarget.texture =
        reuseWorldDepth ? depthTexture : outlineDepthTexture;
      maskDepthTarget.clear_depth = 1.0F;
      maskDepthTarget.load_op = outlineTargetsResized
        ? SDL_GPU_LOADOP_CLEAR
        : SDL_GPU_LOADOP_LOAD;
      maskDepthTarget.store_op = SDL_GPU_STOREOP_STORE;
      maskDepthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
      maskDepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
      maskDepthTarget.cycle = reuseWorldDepth ? false : outlineTargetsResized;
      gpuTiming.beginOutline(commandBuffer);
      gpuTiming.beginPass(commandBuffer, GpuTimedPass::OutlineMask);
      SDL_GPURenderPass* outlineDepthPass = nullptr;
      if (outlineDepthPlan.rebuildDepth) {
        SDL_GPURenderPass* outlineClearPass = SDL_BeginGPURenderPass(
          commandBuffer,
          &maskColorTarget,
          1,
          &maskDepthTarget
        );
        if (outlineClearPass == nullptr) {
          (void)submitCommandBuffer();
          return false;
        }
        SDL_SetGPUViewport(outlineClearPass, &outlineViewport);
        SDL_SetGPUScissor(outlineClearPass, &workScissor);
        SDL_BindGPUGraphicsPipeline(outlineClearPass, pipelineOutlineClear);
        SDL_DrawGPUPrimitives(outlineClearPass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(outlineClearPass);

        maskColorTarget.load_op = SDL_GPU_LOADOP_LOAD;
        maskColorTarget.store_op = SDL_GPU_STOREOP_STORE;
        maskColorTarget.cycle = false;
        maskDepthTarget.load_op = SDL_GPU_LOADOP_LOAD;
        maskDepthTarget.store_op = SDL_GPU_STOREOP_STORE;
        maskDepthTarget.cycle = false;
        outlineDepthPass = SDL_BeginGPURenderPass(
          commandBuffer,
          nullptr,
          0,
          &maskDepthTarget
        );
        if (outlineDepthPass == nullptr) {
          (void)submitCommandBuffer();
          return false;
        }
        SDL_SetGPUViewport(outlineDepthPass, &outlineViewport);
        SDL_SetGPUScissor(outlineDepthPass, &workScissor);
      }
      struct alignas(16) MaskCameraUniform {
        float position[4];
        float right[4];
        float up[4];
        float forward[4];
        float projection[4];
      };
      const PerspectiveCamera& maskCamera = perspectiveScene.camera;
      const MaskCameraUniform maskCameraUniform = {
        {
          maskCamera.position.x,
          maskCamera.position.y,
          maskCamera.position.z,
          0.0F,
        },
        {maskCamera.right.x, maskCamera.right.y, maskCamera.right.z, 0.0F},
        {maskCamera.up.x, maskCamera.up.y, maskCamera.up.z, 0.0F},
        {
          maskCamera.forward.x,
          maskCamera.forward.y,
          maskCamera.forward.z,
          0.0F,
        },
        {
          maskCamera.focalLength,
          maskCamera.aspectRatio,
          maskCamera.nearPlane,
          512.0F,
        },
      };
      if (outlineDepthPass != nullptr) {
        SDL_PushGPUVertexUniformData(
          commandBuffer,
          0,
          &worldCameraUniform,
          sizeof(worldCameraUniform)
        );
        drawOpaqueDepth(outlineDepthPass);
        SDL_EndGPURenderPass(outlineDepthPass);
      }

      maskDepthTarget.load_op = SDL_GPU_LOADOP_LOAD;
      maskDepthTarget.store_op = SDL_GPU_STOREOP_STORE;
      maskDepthTarget.cycle = false;
      if (outlineDepthPlan.rebuildDepth) {
        SDL_GPURenderPass* outlineColorClearPass = SDL_BeginGPURenderPass(
          commandBuffer,
          &maskColorTarget,
          1,
          nullptr
        );
        if (outlineColorClearPass == nullptr) {
          (void)submitCommandBuffer();
          return false;
        }
        SDL_SetGPUViewport(outlineColorClearPass, &outlineViewport);
        SDL_SetGPUScissor(outlineColorClearPass, &workScissor);
        SDL_BindGPUGraphicsPipeline(
          outlineColorClearPass,
          pipelineOutlineColorClear
        );
        SDL_DrawGPUPrimitives(outlineColorClearPass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(outlineColorClearPass);
      }

      maskColorTarget.load_op = outlineDepthPlan.rebuildDepth
        ? SDL_GPU_LOADOP_LOAD
        : SDL_GPU_LOADOP_CLEAR;
      maskColorTarget.store_op = SDL_GPU_STOREOP_STORE;
      maskColorTarget.cycle =
        !outlineDepthPlan.rebuildDepth && outlineTargetsResized;
      maskDepthTarget.load_op = SDL_GPU_LOADOP_LOAD;
      maskDepthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
      maskDepthTarget.cycle = false;
      SDL_GPURenderPass* maskPass = SDL_BeginGPURenderPass(
        commandBuffer,
        &maskColorTarget,
        1,
        &maskDepthTarget
      );
      if (maskPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_SetGPUViewport(maskPass, &outlineViewport);
      SDL_SetGPUScissor(maskPass, &workScissor);
      SDL_PushGPUVertexUniformData(
        commandBuffer,
        0,
        &maskCameraUniform,
        sizeof(maskCameraUniform)
      );
      struct alignas(16) MaskUniform {
        float group[4];
      };
      for (const OutlineMaskDraw& draw : perspectiveScene.outlineMaskDraws) {
        if (
          draw.vertexCount == 0U &&
          draw.instanceCount == 0U &&
          (!draw.gltfPlayerModel || draw.gltfInstanceCount == 0U)
        ) {
          continue;
        }
        const MaskUniform maskUniform = {{
          draw.state.group == OutlineGroup::Enemy
            ? 1.0F
            : draw.state.group == OutlineGroup::Teammate ? 0.5F : 0.0F,
          0.0F,
          0.0F,
          std::clamp(draw.state.fadeAlpha, 0.0F, 1.0F),
        }};
        SDL_PushGPUFragmentUniformData(
          commandBuffer,
          0,
          &maskUniform,
          sizeof(maskUniform)
        );
        if (draw.gltfPlayerModel && draw.gltfInstanceCount > 0U) {
          drawGltfPlayerModelInstanceRange(
            maskPass,
            gltfPlayerModelOutlineMaskPipeline,
            gltfPlayerResources,
            draw.gltfFirstInstance,
            draw.gltfInstanceCount
          );
        } else if (draw.instanceCount > 0U) {
          drawStaticMeshInstanceRange(
            maskPass,
            staticMeshOutlineMaskPipeline,
            materialMeshOutlineMaskPipeline,
            simpleResources,
            draw.mesh,
            draw.firstInstance,
            draw.instanceCount
          );
        } else {
          SDL_BindGPUGraphicsPipeline(maskPass, pipelineOutlineMask);
          const SDL_GPUBufferBinding maskBinding = {vertexBuffer, 0};
          SDL_BindGPUVertexBuffers(maskPass, 0, &maskBinding, 1);
          SDL_DrawGPUPrimitives(
            maskPass,
            draw.vertexCount,
            1,
            draw.firstVertex,
            0
          );
        }
      }
      SDL_EndGPURenderPass(maskPass);
      gpuTiming.endPass(commandBuffer, GpuTimedPass::OutlineMask);

      SDL_GPUColorTargetInfo dilationColorTarget = {};
      dilationColorTarget.texture = outlineDilationTexture;
      dilationColorTarget.clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
      dilationColorTarget.load_op = nativeOutline
        ? SDL_GPU_LOADOP_DONT_CARE
        : outlineTargetsResized
          ? SDL_GPU_LOADOP_CLEAR
          : SDL_GPU_LOADOP_LOAD;
      dilationColorTarget.store_op = SDL_GPU_STOREOP_STORE;
      dilationColorTarget.cycle = outlineTargetsResized;
      gpuTiming.beginPass(commandBuffer, GpuTimedPass::OutlineDilation);
      SDL_GPURenderPass* dilationPass = SDL_BeginGPURenderPass(
        commandBuffer,
        &dilationColorTarget,
        1,
        nullptr
      );
      if (dilationPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_SetGPUViewport(dilationPass, &outlineViewport);
      SDL_SetGPUScissor(dilationPass, &workScissor);
      SDL_BindGPUGraphicsPipeline(dilationPass, activeOutlineDilation);
      const SDL_GPUTextureSamplerBinding dilationMaskBinding = {
        outlineMaskTexture,
        outlineMaskSampler,
      };
      SDL_BindGPUFragmentSamplers(dilationPass, 0, &dilationMaskBinding, 1);
      struct alignas(16) DilationUniform {
        float texelSizeAndWidths[4];
        float workRect[4];
      };
      const DilationUniform dilationUniform = {
        {
          1.0F / static_cast<float>(workWidth),
          1.0F / static_cast<float>(workHeight),
          outlineWorkRadiusPixels(
            settings.playerOutlineMode == PlayerOutlineMode::NativeScreenSpace
              ? settings.playerOutlineWidth
              : settings.enemyOutlineWidth,
            outlinePlan.dimensions.workScale
          ),
          outlineWorkRadiusPixels(
            settings.playerOutlineMode == PlayerOutlineMode::NativeScreenSpace
              ? settings.playerOutlineWidth
              : settings.teammateOutlineWidth,
            outlinePlan.dimensions.workScale
          ),
        },
        {
          static_cast<float>(outlinePlan.workRect.x),
          static_cast<float>(outlinePlan.workRect.y),
          static_cast<float>(outlinePlan.workRect.x + outlinePlan.workRect.width),
          static_cast<float>(outlinePlan.workRect.y + outlinePlan.workRect.height),
        },
      };
      SDL_PushGPUFragmentUniformData(
        commandBuffer,
        0,
        &dilationUniform,
        sizeof(dilationUniform)
      );
      SDL_DrawGPUPrimitives(dilationPass, 3, 1, 0, 0);
      SDL_EndGPURenderPass(dilationPass);
      gpuTiming.endPass(commandBuffer, GpuTimedPass::OutlineDilation);

      float enemyPulse = 0.0F;
      float teammatePulse = 0.0F;
      for (const OutlineMaskDraw& draw : perspectiveScene.outlineMaskDraws) {
        if (draw.state.group == OutlineGroup::Enemy) {
          enemyPulse = std::max(enemyPulse, draw.state.pulse);
        } else if (draw.state.group == OutlineGroup::Teammate) {
          teammatePulse = std::max(teammatePulse, draw.state.pulse);
        }
      }
      const auto normalizedColor = [](std::uint8_t red,
                                      std::uint8_t green,
                                      std::uint8_t blue,
                                      float alpha,
                                      float pulse) {
        const float intensity =
          1.0F + std::clamp(pulse, 0.0F, 1.0F) * 0.35F;
        return std::array<float, 4>{
          std::clamp(static_cast<float>(red) / 255.0F * intensity, 0.0F, 1.0F),
          std::clamp(static_cast<float>(green) / 255.0F * intensity, 0.0F, 1.0F),
          std::clamp(static_cast<float>(blue) / 255.0F * intensity, 0.0F, 1.0F),
          std::clamp(alpha, 0.0F, 1.0F),
        };
      };
      const std::array<float, 4> enemyColor = normalizedColor(
        settings.enemyOutlineRed,
        settings.enemyOutlineGreen,
        settings.enemyOutlineBlue,
        settings.enemyOutlineAlpha,
        enemyPulse
      );
      const std::array<float, 4> teammateColor = normalizedColor(
        settings.teammateOutlineRed,
        settings.teammateOutlineGreen,
        settings.teammateOutlineBlue,
        settings.teammateOutlineAlpha,
        teammatePulse
      );
      struct alignas(16) CompositeUniform {
        float texelSizeAndWidths[4];
        float enemyColor[4];
        float teammateColor[4];
        float workRect[4];
      };
      const CompositeUniform compositeUniform = {
        {
          1.0F / static_cast<float>(workWidth),
          1.0F / static_cast<float>(workHeight),
          nativeOutline && settings.playerOutlineDebugMask ? 1.0F : 0.0F,
          0.0F,
        },
        {enemyColor[0], enemyColor[1], enemyColor[2], enemyColor[3]},
        {
          teammateColor[0],
          teammateColor[1],
          teammateColor[2],
          teammateColor[3],
        },
        {
          static_cast<float>(outlinePlan.workRect.x),
          static_cast<float>(outlinePlan.workRect.y),
          static_cast<float>(outlinePlan.workRect.x + outlinePlan.workRect.width),
          static_cast<float>(outlinePlan.workRect.y + outlinePlan.workRect.height),
        },
      };

      gpuTiming.beginPass(commandBuffer, GpuTimedPass::OutlineComposite);
      SDL_GPURenderPass* compositePass =
        SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
      if (compositePass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_BindGPUGraphicsPipeline(compositePass, activeOutlineComposite);
      SDL_SetGPUScissor(compositePass, &compositeScissor);
      const std::array<SDL_GPUTextureSamplerBinding, 2> samplerBindings = {{
        {outlineMaskTexture, outlineMaskSampler},
        {outlineDilationTexture, outlineMaskSampler},
      }};
      SDL_BindGPUFragmentSamplers(
        compositePass,
        0,
        samplerBindings.data(),
        static_cast<Uint32>(samplerBindings.size())
      );
      SDL_PushGPUFragmentUniformData(
        commandBuffer,
        0,
        &compositeUniform,
        sizeof(compositeUniform)
      );
      SDL_DrawGPUPrimitives(compositePass, 3, 1, 0, 0);
      SDL_EndGPURenderPass(compositePass);
      gpuTiming.endPass(commandBuffer, GpuTimedPass::OutlineComposite);
      gpuTiming.endOutline(commandBuffer);

      diagnostics.outlineMaskWidth = outlineMaskWidth;
      diagnostics.outlineMaskHeight = outlineMaskHeight;
      diagnostics.outlineWorkWidth = workWidth;
      diagnostics.outlineWorkHeight = workHeight;
      diagnostics.outlineWorkScale = outlinePlan.dimensions.workScale;
      diagnostics.outlineWorkRectX = outlinePlan.finalRect.x;
      diagnostics.outlineWorkRectY = outlinePlan.finalRect.y;
      diagnostics.outlineWorkRectWidth = outlinePlan.finalRect.width;
      diagnostics.outlineWorkRectHeight = outlinePlan.finalRect.height;
      diagnostics.outlineWorkAreaPercent =
        outputWidth > 0U && outputHeight > 0U
          ? (
              static_cast<float>(
                outlinePlan.finalRect.width * outlinePlan.finalRect.height
              ) * 100.0F
            ) / static_cast<float>(outputWidth * outputHeight)
          : 0.0F;
      diagnostics.outlineMaskDrawCalls = outlinePlan.maskDrawCalls;
      diagnostics.outlineDilationDrawCalls = outlinePlan.dilationDrawCalls;
      diagnostics.outlineCompositeDrawCalls = outlinePlan.compositeDrawCalls;
      diagnostics.outlineUploadBytes = outlinePlan.uploadBytes;
      diagnostics.outlineGpuTimingAvailable = false;
      diagnostics.outlineGpuMilliseconds = 0.0F;
      diagnostics.outlinePasses = outlineDepthPlan.passCount;
      diagnostics.outlineCompositeEnabled = true;
    }

    if (settings.benchmarkTimingEnabled) {
      // Static-world calls are removed from the surrounding 3D command span;
      // the remainder is dynamic geometry, instances, outlines, and viewmodel.
      const float totalThreeDimensionalEncodingMilliseconds =
        millisecondsBetween(threeDimensionalEncodingStart, RenderClock::now());
      diagnostics.worldCommandEncodingMilliseconds =
        staticWorldEncodingMilliseconds;
      diagnostics.dynamicCommandEncodingMilliseconds = std::max(
        0.0F,
        totalThreeDimensionalEncodingMilliseconds - staticWorldEncodingMilliseconds
      );
    }
    RenderClock::time_point uiEncodingStart = {};
    if (settings.benchmarkTimingEnabled) {
      uiEncodingStart = RenderClock::now();
    }
    const Uint32 overlayVertexCount =
      static_cast<Uint32>(vertices.size()) - worldVertexCount;
    if (overlayVertexCount > 0U && !overlayBatches.empty()) {
      gpuTiming.beginPass(commandBuffer, GpuTimedPass::UiOverlay);
      SDL_GPURenderPass* overlayPass =
        SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
      if (overlayPass == nullptr) {
        (void)submitCommandBuffer();
        return false;
      }
      SDL_BindGPUGraphicsPipeline(overlayPass, pipeline2D);
      const SDL_GPUBufferBinding binding = {vertexBuffer, 0};
      SDL_BindGPUVertexBuffers(overlayPass, 0, &binding, 1);

      const SDL_Rect fullScissor = {
        0,
        0,
        static_cast<int>(outputWidth),
        static_cast<int>(outputHeight),
      };
      SDL_SetGPUScissor(overlayPass, &fullScissor);
      (void)worldVertexCount;
      for (const OverlayDrawBatch& batch : overlayBatches) {
        if (
          batch.fontAtlas == nullptr ||
          batch.fontAtlas->texture == nullptr ||
          batch.vertexCount == 0U
        ) {
          continue;
        }
        const SDL_GPUTextureSamplerBinding fontBinding = {
          batch.fontAtlas->texture,
          fontSampler,
        };
        SDL_BindGPUFragmentSamplers(
          overlayPass,
          0,
          &fontBinding,
          1
        );
        SDL_DrawGPUPrimitives(
          overlayPass,
          batch.vertexCount,
          1,
          batch.firstVertex,
          0
        );
      }
      SDL_EndGPURenderPass(overlayPass);
      gpuTiming.endPass(commandBuffer, GpuTimedPass::UiOverlay);
    }
    if (settings.benchmarkTimingEnabled) {
      // Final UI encoding is added to the earlier HUD/batch construction span.
      diagnostics.uiMilliseconds +=
        millisecondsBetween(uiEncodingStart, RenderClock::now());
    }
    diagnostics.worldDrawIssueMilliseconds =
      millisecondsBetween(drawIssueStart, RenderClock::now());
  } else if (settings.benchmarkGpuFrameIndex.has_value()) {
    gpuTiming.publishUnavailableFrame(
      *settings.benchmarkGpuFrameIndex,
      false,
      "no_swapchain_texture"
    );
  }

  const auto submitStart = RenderClock::now();
  diagnostics.renderBuildUploadMilliseconds =
    millisecondsBetween(buildStart, submitStart);
  // The frame duration ends after the last overlay pass. A later screenshot
  // copy stays outside the benchmark range.
  if (timingActive) {
    gpuTiming.endFrame(commandBuffer);
  }
  // Submit is CPU-side command submission time, not actual display present time.
  SDL_GPUTransferBuffer* captureTransfer = nullptr;
  SDL_GPUTextureFormat captureFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
  if (captureRequest != nullptr && captureResult != nullptr) {
    captureResult->requested = true;
    captureResult->path = captureRequest->path;
    captureResult->width = outputWidth;
    captureResult->height = outputHeight;
    captureFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    const bool supportedFormat =
      captureFormat == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
      captureFormat == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB ||
      captureFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
      captureFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    const std::uint64_t captureBytes64 =
      static_cast<std::uint64_t>(outputWidth) * outputHeight * 4U;
    if (swapchainTexture == nullptr || outputWidth == 0U || outputHeight == 0U) {
      captureResult->error = "renderer did not acquire a usable swapchain texture";
    } else if (!supportedFormat || captureBytes64 > std::numeric_limits<Uint32>::max()) {
      captureResult->error = "SDL_GPU swapchain format or dimensions are not capture-compatible";
    } else {
      const SDL_GPUTransferBufferCreateInfo transferInfo = {
        SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        static_cast<Uint32>(captureBytes64),
        0,
      };
      captureTransfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
      SDL_GPUCopyPass* copyPass = captureTransfer != nullptr
        ? SDL_BeginGPUCopyPass(commandBuffer)
        : nullptr;
      if (copyPass == nullptr) {
        captureResult->error = std::string("could not create GPU capture resources: ") + SDL_GetError();
      } else {
        const SDL_GPUTextureRegion source = {
          swapchainTexture, 0, 0, 0, 0, 0, outputWidth, outputHeight, 1,
        };
        const SDL_GPUTextureTransferInfo destination = {
          captureTransfer, 0, outputWidth, outputHeight,
        };
        SDL_DownloadFromGPUTexture(copyPass, &source, &destination);
        SDL_EndGPUCopyPass(copyPass);
      }
    }
  }

  SDL_GPUFence* captureFence = nullptr;
  const bool submitted = submitCommandBuffer(
    captureTransfer != nullptr ? &captureFence : nullptr
  );
  if (submitted && pointShadowCacheRendered) {
    pointShadowCacheKey = pendingPointShadowCacheKey;
  }
  if (captureTransfer != nullptr && captureFence != nullptr && captureResult != nullptr) {
    SDL_GPUFence* fences[] = {captureFence};
    if (!SDL_WaitForGPUFences(device, true, fences, 1)) {
      captureResult->error = std::string("GPU capture fence wait failed: ") + SDL_GetError();
    } else {
      const void* mapped = SDL_MapGPUTransferBuffer(device, captureTransfer, false);
      if (mapped == nullptr) {
        captureResult->error = std::string("GPU capture buffer mapping failed: ") + SDL_GetError();
      } else {
        const std::size_t pixelBytes =
          static_cast<std::size_t>(outputWidth) * outputHeight * 4U;
        std::vector<std::uint8_t> rgba(pixelBytes);
        std::memcpy(rgba.data(), mapped, pixelBytes);
        SDL_UnmapGPUTransferBuffer(device, captureTransfer);
        if (
          captureFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
          captureFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB
        ) {
          for (std::size_t index = 0; index < rgba.size(); index += 4U) {
            std::swap(rgba[index], rgba[index + 2U]);
          }
        }
        captureResult->ok = dev::writeRgbaPng(
          captureRequest->path, outputWidth, outputHeight, rgba, captureResult->error
        );
      }
    }
    if (!timingOwnedSubmittedFence) {
      SDL_ReleaseGPUFence(device, captureFence);
    }
  }
  if (captureTransfer != nullptr) SDL_ReleaseGPUTransferBuffer(device, captureTransfer);
  diagnostics.submitMilliseconds =
    millisecondsBetween(submitStart, RenderClock::now());
  return submitted;
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

[[nodiscard]] SDL_FColor remoteModelColor(
  const RenderSettings& settings,
  float enemyHitAmount,
  bool teammate,
  float fadeAlpha = 1.0F
) {
  const float hitAmount = std::clamp(enemyHitAmount, 0.0F, 1.0F);
  const float modelAlpha = std::clamp(fadeAlpha, 0.0F, 1.0F);
  if (teammate) {
    return {
      static_cast<float>(settings.teammateRed) / 255.0F,
      static_cast<float>(settings.teammateGreen) / 255.0F,
      static_cast<float>(settings.teammateBlue) / 255.0F,
      std::clamp(settings.teammateAlpha, 0.0F, 1.0F) * modelAlpha,
    };
  }
  return {
    static_cast<float>(
      blendChannel(settings.enemyRed, settings.enemyHitRed, hitAmount)
    ) / 255.0F,
    static_cast<float>(
      blendChannel(settings.enemyGreen, settings.enemyHitGreen, hitAmount)
    ) / 255.0F,
    static_cast<float>(
      blendChannel(settings.enemyBlue, settings.enemyHitBlue, hitAmount)
    ) / 255.0F,
    std::clamp(settings.enemyAlpha, 0.0F, 1.0F) * modelAlpha,
  };
}

[[nodiscard]] SDL_FColor remoteOutlineColor(
  const RenderSettings& settings,
  bool teammate,
  float fadeAlpha = 1.0F
) {
  return {
    static_cast<float>(
      teammate ? settings.teammateOutlineRed : settings.enemyOutlineRed
    ) / 255.0F,
    static_cast<float>(
      teammate ? settings.teammateOutlineGreen : settings.enemyOutlineGreen
    ) / 255.0F,
    static_cast<float>(
      teammate ? settings.teammateOutlineBlue : settings.enemyOutlineBlue
    ) / 255.0F,
    std::clamp(
      teammate ? settings.teammateOutlineAlpha : settings.enemyOutlineAlpha,
      0.0F,
      1.0F
    ) * std::clamp(fadeAlpha, 0.0F, 1.0F),
  };
}

[[nodiscard]] SDL_FColor localBeamColor(const RenderSettings& settings) {
  const float hitAmount = std::clamp(settings.beamHitAmount, 0.0F, 1.0F);
  const bool freezeBeam = settings.localSelectedWeapon == Weapon::FreezeGun;
  return {
    static_cast<float>(
      blendChannel(
        freezeBeam ? 154U : settings.beamRed,
        freezeBeam ? 230U : settings.beamHitRed,
        hitAmount
      )
    ) / 255.0F,
    static_cast<float>(
      blendChannel(
        freezeBeam ? 232U : settings.beamGreen,
        freezeBeam ? 255U : settings.beamHitGreen,
        hitAmount
      )
    ) / 255.0F,
    static_cast<float>(
      blendChannel(
        freezeBeam ? 255U : settings.beamBlue,
        freezeBeam ? 255U : settings.beamHitBlue,
        hitAmount
      )
    ) / 255.0F,
    std::clamp(settings.beamAlpha, 0.0F, 1.0F),
  };
}

void drawFilledQuad(
  SDL_Renderer* renderer,
  const std::array<SDL_FPoint, 4>& points,
  SDL_FColor color
) {
  const std::array<SDL_Vertex, 4> vertices = {{
    {points[0], color, {}},
    {points[1], color, {}},
    {points[2], color, {}},
    {points[3], color, {}},
  }};
  constexpr std::array<int, 6> indices = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(
    renderer,
    nullptr,
    vertices.data(),
    static_cast<int>(vertices.size()),
    indices.data(),
    static_cast<int>(indices.size())
  );
}

void drawThickLine(
  SDL_Renderer* renderer,
  float startX,
  float startY,
  float endX,
  float endY,
  float width
) {
  const float deltaX = endX - startX;
  const float deltaY = endY - startY;
  const float lineLength = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
  const float normalX = lineLength > 0.0F ? -deltaY / lineLength : 0.0F;
  const float normalY = lineLength > 0.0F ? deltaX / lineLength : 1.0F;
  const int halfWidth = std::max(0, static_cast<int>(width * 0.5F));
  for (int offset = -halfWidth; offset <= halfWidth; ++offset) {
    const float offsetX = normalX * static_cast<float>(offset);
    const float offsetY = normalY * static_cast<float>(offset);
    SDL_RenderLine(
      renderer,
      startX + offsetX,
      startY + offsetY,
      endX + offsetX,
      endY + offsetY
    );
  }
}

void drawSniperScopeOverlay(
  SDL_Renderer* renderer,
  const SniperScopeOverlay2D& scope
) {
  CachedSniperScopeMesh& mesh = sniperScopeMesh(scope);
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    const SniperScopeMeshVertex& source = mesh.vertices[index];
    const ScreenPoint point =
      transformedSniperScopePoint(scope, source.position);
    const RenderColor color =
      modulatedSniperScopeColor(source.color, scope.opacity);
    mesh.fallbackVertices[index] = {
      {point.x, point.y},
      {
        static_cast<float>(color.red) / 255.0F,
        static_cast<float>(color.green) / 255.0F,
        static_cast<float>(color.blue) / 255.0F,
        static_cast<float>(color.alpha) / 255.0F,
      },
      {},
    };
  }
  SDL_RenderGeometry(
    renderer,
    nullptr,
    mesh.fallbackVertices.data(),
    static_cast<int>(mesh.fallbackVertices.size()),
    mesh.indices.data(),
    static_cast<int>(mesh.indices.size())
  );
}

void drawCommands(
  SDL_Renderer* renderer,
  const std::vector<DrawCommand2D>& commands
) {
  for (const DrawCommand2D& command : commands) {
    std::visit(
      [renderer](const auto& primitive) {
        using Primitive = std::decay_t<decltype(primitive)>;
        SDL_SetRenderDrawColor(
          renderer,
          primitive.color.red,
          primitive.color.green,
          primitive.color.blue,
          primitive.color.alpha
        );
        if constexpr (std::is_same_v<Primitive, FilledQuad2D>) {
          const std::array<SDL_FPoint, 4> points = {{
            {primitive.points[0].x, primitive.points[0].y},
            {primitive.points[1].x, primitive.points[1].y},
            {primitive.points[2].x, primitive.points[2].y},
            {primitive.points[3].x, primitive.points[3].y},
          }};
          const SDL_FColor color = {
            static_cast<float>(primitive.color.red) / 255.0F,
            static_cast<float>(primitive.color.green) / 255.0F,
            static_cast<float>(primitive.color.blue) / 255.0F,
            static_cast<float>(primitive.color.alpha) / 255.0F,
          };
          drawFilledQuad(renderer, points, color);
        } else if constexpr (std::is_same_v<Primitive, Line2D>) {
          drawThickLine(
            renderer,
            primitive.start.x,
            primitive.start.y,
            primitive.end.x,
            primitive.end.y,
            primitive.width
          );
        } else if constexpr (
          std::is_same_v<Primitive, SniperScopeOverlay2D>
        ) {
          drawSniperScopeOverlay(renderer, primitive);
        } else if constexpr (std::is_same_v<Primitive, Text2D>) {
          const float snappedScale =
            kUiFontPixelHeights[nearestUiFontPixelHeightIndex(primitive.scale)] /
            kBitmapGlyphSize;
          const float textWidth =
            static_cast<float>(utf8GlyphCount(primitive.text)) *
            kBitmapGlyphSize *
            snappedScale;
          float x = primitive.position.x;
          if (primitive.horizontalAlignment == TextHorizontalAlignment::Center) {
            x -= textWidth * 0.5F;
          } else if (primitive.horizontalAlignment == TextHorizontalAlignment::Right) {
            x -= textWidth;
          }
          SDL_SetRenderScale(renderer, snappedScale, snappedScale);
          SDL_RenderDebugText(
            renderer,
            x / snappedScale,
            primitive.position.y / snappedScale,
            primitive.text.c_str()
          );
          SDL_SetRenderScale(renderer, 1.0F, 1.0F);
        }
      },
      command
    );
  }
}

void drawCommandList(SDL_Renderer* renderer, const DrawList2D& drawList) {
  const SDL_Rect clip = {
    static_cast<int>(drawList.clip.x),
    static_cast<int>(drawList.clip.y),
    static_cast<int>(drawList.clip.width),
    static_cast<int>(drawList.clip.height),
  };
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderClipRect(renderer, &clip);
  drawCommands(renderer, drawList.commands);
  SDL_SetRenderClipRect(renderer, nullptr);
  drawCommands(renderer, drawList.overlayCommands);
}

void drawPerspectiveLine(
  SDL_Renderer* renderer,
  const PerspectiveCamera& camera,
  int width,
  int height,
  Vec3 start,
  Vec3 end
) {
  if (!clipPerspectiveLine(camera, start, end)) {
    return;
  }
  ProjectedPoint projectedStart;
  ProjectedPoint projectedEnd;
  if (
    !projectPerspectivePoint(camera, start, projectedStart) ||
    !projectPerspectivePoint(camera, end, projectedEnd)
  ) {
    return;
  }
  const auto screenX = [width](float x) {
    return (x + 1.0F) * 0.5F * static_cast<float>(width);
  };
  const auto screenY = [height](float y) {
    return (1.0F - y) * 0.5F * static_cast<float>(height);
  };
  SDL_RenderLine(
    renderer,
    screenX(projectedStart.x),
    screenY(projectedStart.y),
    screenX(projectedEnd.x),
    screenY(projectedEnd.y)
  );
}

void drawThickPerspectiveLine(
  SDL_Renderer* renderer,
  const PerspectiveCamera& camera,
  int width,
  int height,
  Vec3 start,
  Vec3 end,
  float lineWidth
) {
  if (!clipPerspectiveLine(camera, start, end)) {
    return;
  }
  ProjectedPoint projectedStart;
  ProjectedPoint projectedEnd;
  if (
    !projectPerspectivePoint(camera, start, projectedStart) ||
    !projectPerspectivePoint(camera, end, projectedEnd)
  ) {
    return;
  }
  const auto screenX = [width](float x) {
    return (x + 1.0F) * 0.5F * static_cast<float>(width);
  };
  const auto screenY = [height](float y) {
    return (1.0F - y) * 0.5F * static_cast<float>(height);
  };
  drawThickLine(
    renderer,
    screenX(projectedStart.x),
    screenY(projectedStart.y),
    screenX(projectedEnd.x),
    screenY(projectedEnd.y),
    lineWidth
  );
}

[[nodiscard]] bool projectPerspectiveScreenPoint(
  const PerspectiveCamera& camera,
  int width,
  int height,
  Vec3 worldPosition,
  SDL_FPoint& screenPoint
) {
  ProjectedPoint projected;
  if (!projectPerspectivePoint(camera, worldPosition, projected)) {
    return false;
  }
  screenPoint = {
    (projected.x + 1.0F) * 0.5F * static_cast<float>(width),
    (1.0F - projected.y) * 0.5F * static_cast<float>(height),
  };
  return true;
}

void drawWireBox(
  SDL_Renderer* renderer,
  const PerspectiveCamera& camera,
  int width,
  int height,
  Vec3 minimum,
  Vec3 maximum
) {
  const std::array<Vec3, 8> corners = {{
    {minimum.x, minimum.y, minimum.z},
    {maximum.x, minimum.y, minimum.z},
    {maximum.x, maximum.y, minimum.z},
    {minimum.x, maximum.y, minimum.z},
    {minimum.x, minimum.y, maximum.z},
    {maximum.x, minimum.y, maximum.z},
    {maximum.x, maximum.y, maximum.z},
    {minimum.x, maximum.y, maximum.z},
  }};
  constexpr std::array<std::array<std::size_t, 2>, 12> edges = {{
    {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
    {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
    {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
  }};
  for (const auto& edge : edges) {
    drawPerspectiveLine(
      renderer,
      camera,
      width,
      height,
      corners[edge[0]],
      corners[edge[1]]
    );
  }
}

void drawSolidBox(
  SDL_Renderer* renderer,
  const PerspectiveCamera& camera,
  int width,
  int height,
  Vec3 minimum,
  Vec3 maximum,
  SDL_FColor color
) {
  const std::array<Vec3, 8> corners = {{
    {minimum.x, minimum.y, minimum.z},
    {maximum.x, minimum.y, minimum.z},
    {maximum.x, maximum.y, minimum.z},
    {minimum.x, maximum.y, minimum.z},
    {minimum.x, minimum.y, maximum.z},
    {maximum.x, minimum.y, maximum.z},
    {maximum.x, maximum.y, maximum.z},
    {minimum.x, maximum.y, maximum.z},
  }};
  constexpr std::array<std::array<std::size_t, 4>, 6> faceIndices = {{
    {{0, 1, 2, 3}},
    {{4, 7, 6, 5}},
    {{0, 4, 5, 1}},
    {{1, 5, 6, 2}},
    {{2, 6, 7, 3}},
    {{3, 7, 4, 0}},
  }};
  constexpr std::array<float, 6> faceBrightness = {
    0.55F, 1.0F, 0.72F, 0.86F, 0.66F, 0.78F,
  };

  struct ProjectedFace {
    std::array<SDL_FPoint, 4> points = {};
    SDL_FColor color = {};
    float depth = 0.0F;
  };
  std::array<ProjectedFace, 6> faces;
  std::size_t faceCount = 0;
  for (std::size_t faceIndex = 0; faceIndex < faceIndices.size(); ++faceIndex) {
    ProjectedFace face;
    bool visible = true;
    for (std::size_t cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
      const Vec3 corner = corners[faceIndices[faceIndex][cornerIndex]];
      visible = visible && projectPerspectiveScreenPoint(
        camera,
        width,
        height,
        corner,
        face.points[cornerIndex]
      );
      face.depth += dot(corner - camera.position, camera.forward);
    }
    if (!visible) {
      continue;
    }
    const float brightness = faceBrightness[faceIndex];
    face.color = {
      color.r * brightness,
      color.g * brightness,
      color.b * brightness,
      color.a,
    };
    face.depth *= 0.25F;
    faces[faceCount++] = face;
  }

  std::sort(
    faces.begin(),
    faces.begin() + static_cast<std::ptrdiff_t>(faceCount),
    [](const ProjectedFace& lhs, const ProjectedFace& rhs) {
      return lhs.depth > rhs.depth;
    }
  );
  for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    drawFilledQuad(renderer, faces[faceIndex].points, faces[faceIndex].color);
  }
}

void drawPerspectiveWorld(
  SDL_Renderer* renderer,
  int width,
  int height,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const std::array<bool, kDuelPlayerCount>& remoteRenderVisible,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
  const RenderSettings& settings
) {
  const float aspectRatio =
    static_cast<float>(width) / static_cast<float>(std::max(1, height));
  const PerspectiveCamera camera =
    playerPerspectiveCamera(player, aspectRatio, settings.fieldOfView);

  const std::array<Vec3, 4> floorCorners = {{
    {arena.min.x, arena.min.y, arena.min.z},
    {arena.max.x, arena.min.y, arena.min.z},
    {arena.max.x, arena.max.y, arena.min.z},
    {arena.min.x, arena.max.y, arena.min.z},
  }};
  std::array<SDL_FPoint, 4> floorPoints = {};
  bool floorVisible = true;
  for (std::size_t index = 0; index < floorCorners.size(); ++index) {
    floorVisible = floorVisible &&
      projectPerspectiveScreenPoint(
        camera,
        width,
        height,
        floorCorners[index],
        floorPoints[index]
      );
  }
  if (floorVisible) {
    drawFilledQuad(renderer, floorPoints, SDL_FColor{0.10F, 0.11F, 0.13F, 1.0F});
  }

  SDL_SetRenderDrawColor(renderer, 82, 94, 108, 255);
  constexpr float kGridStep = 1.0F;
  for (float x = arena.min.x; x <= arena.max.x; x += kGridStep) {
    drawPerspectiveLine(
      renderer,
      camera,
      width,
      height,
      {x, arena.min.y, arena.min.z},
      {x, arena.max.y, arena.min.z}
    );
  }
  for (float y = arena.min.y; y <= arena.max.y; y += kGridStep) {
    drawPerspectiveLine(
      renderer,
      camera,
      width,
      height,
      {arena.min.x, y, arena.min.z},
      {arena.max.x, y, arena.min.z}
    );
  }

  SDL_SetRenderDrawColor(renderer, 120, 138, 156, 255);
  drawWireBox(renderer, camera, width, height, arena.min, arena.max);

  std::array<const ArenaWall*, Arena::kWallCount + Arena::kVisualWallCount>
    wallDrawOrder = {};
  std::size_t wallDrawCount = 0;
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    if (!arena.walls[index].renderable) {
      continue;
    }
    wallDrawOrder[wallDrawCount++] = &arena.walls[index];
  }
  for (std::size_t index = 0; index < arena.visualWallCount; ++index) {
    wallDrawOrder[wallDrawCount++] = &arena.visualWalls[index];
  }
  std::sort(
    wallDrawOrder.begin(),
    wallDrawOrder.begin() + static_cast<std::ptrdiff_t>(wallDrawCount),
    [&camera](const ArenaWall* lhs, const ArenaWall* rhs) {
      const Vec3 lhsCenter = (lhs->min + lhs->max) * 0.5F;
      const Vec3 rhsCenter = (rhs->min + rhs->max) * 0.5F;
      return dot(lhsCenter - camera.position, camera.forward) >
        dot(rhsCenter - camera.position, camera.forward);
    }
  );

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  for (std::size_t orderIndex = 0; orderIndex < wallDrawCount; ++orderIndex) {
    const ArenaWall& wall = *wallDrawOrder[orderIndex];
    drawSolidBox(
      renderer,
      camera,
      width,
      height,
      wall.min,
      wall.max,
      SDL_FColor{0.46F, 0.47F, 0.50F, 1.0F}
    );
  }

  SDL_SetRenderDrawColor(renderer, 120, 138, 156, 255);
  for (std::size_t orderIndex = 0; orderIndex < wallDrawCount; ++orderIndex) {
    const ArenaWall& wall = *wallDrawOrder[orderIndex];
    drawWireBox(renderer, camera, width, height, wall.min, wall.max);
    SDL_SetRenderDrawColor(renderer, 156, 170, 184, 255);
    for (float z = wall.min.z + 1.0F; z < wall.max.z; z += 1.0F) {
      drawPerspectiveLine(
        renderer, camera, width, height,
        {wall.min.x, wall.min.y, z}, {wall.max.x, wall.min.y, z}
      );
      drawPerspectiveLine(
        renderer, camera, width, height,
        {wall.max.x, wall.max.y, z}, {wall.min.x, wall.max.y, z}
      );
    }
    SDL_SetRenderDrawColor(renderer, 120, 138, 156, 255);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (std::size_t remoteIndex = 0; remoteIndex < remotePlayers.size(); ++remoteIndex) {
    const RemotePlayerView& remote = remotePlayers[remoteIndex];
    if (!remote.visible || !remote.bodyFade.visible) {
      continue;
    }
    if (!remoteRenderVisible[remoteIndex]) {
      continue;
    }
    const PlayerState& remotePlayer = remote.player;
    const bool outlineEnabled = remote.teammate
      ? settings.teammateOutlineEnabled
      : settings.enemyOutlineEnabled;
    const float outlineWidth = remote.teammate
      ? settings.teammateOutlineWidth
      : settings.enemyOutlineWidth;
    if (
      outlineEnabled &&
      usesGeometryPlayerOutlineFallback(
        settings.playerOutlineMode,
        settings.playerOutlineStyle
      ) &&
      outlineWidth > 0.0F
    ) {
      // SDL_Renderer has no screen-space outline mask path; style 0 keeps the
      // old approximate geometry fallback for diagnostics and compatibility.
      const float legacyWorldOutlineWidth =
        outlineWidth * kLegacyOutlineWorldUnitsPerPixel;
      const SDL_FColor outlineColor = remoteOutlineColor(
        settings,
        remote.teammate,
        remote.bodyFade.outlineAlpha
      );
      SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(outlineColor.r * 255.0F),
        static_cast<Uint8>(outlineColor.g * 255.0F),
        static_cast<Uint8>(outlineColor.b * 255.0F),
        static_cast<Uint8>(outlineColor.a * 255.0F)
      );
      drawWireBox(
        renderer,
        camera,
        width,
        height,
        {
          remotePlayer.position.x - remotePlayer.bounds.radius -
            legacyWorldOutlineWidth,
          remotePlayer.position.y - remotePlayer.bounds.radius -
            legacyWorldOutlineWidth,
          remotePlayer.position.z - remotePlayer.bounds.halfHeight -
            legacyWorldOutlineWidth,
        },
        {
          remotePlayer.position.x + remotePlayer.bounds.radius +
            legacyWorldOutlineWidth,
          remotePlayer.position.y + remotePlayer.bounds.radius +
            legacyWorldOutlineWidth,
          remotePlayer.position.z + remotePlayer.bounds.halfHeight +
            legacyWorldOutlineWidth,
        }
      );
    }
    drawSolidBox(
      renderer,
      camera,
      width,
      height,
      {
        remotePlayer.position.x - remotePlayer.bounds.radius,
        remotePlayer.position.y - remotePlayer.bounds.radius,
        remotePlayer.position.z - remotePlayer.bounds.halfHeight,
      },
      {
        remotePlayer.position.x + remotePlayer.bounds.radius,
        remotePlayer.position.y + remotePlayer.bounds.radius,
        remotePlayer.position.z + remotePlayer.bounds.halfHeight,
      },
      remoteModelColor(
        settings,
        remote.enemyHitAmount,
        remote.teammate,
        remote.bodyFade.modelAlpha
      )
    );
  }

  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    if (!healthPickupAvailable[index]) {
      continue;
    }
    const ArenaHealthPickup& pickup = arena.healthPickups[index];
    const bool large = pickup.type == HealthPickupType::Large;
    const Vec3 halfExtents = large
      ? Vec3{0.34F, 0.34F, 0.18F}
      : Vec3{0.24F, 0.24F, 0.14F};
    const Vec3 center = pickup.position + Vec3{0.0F, 0.0F, halfExtents.z};
    drawSolidBox(
      renderer,
      camera,
      width,
      height,
      center - halfExtents,
      center + halfExtents,
      SDL_FColor{0.96F, 0.97F, 0.98F, 1.0F}
    );
    const float z = center.z + halfExtents.z + 0.02F;
    const float longExtent = halfExtents.x * 0.6F;
    const float shortExtent = halfExtents.x * 0.16F;
    drawSolidBox(
      renderer,
      camera,
      width,
      height,
      {center.x - longExtent, center.y - shortExtent, z},
      {center.x + longExtent, center.y + shortExtent, z + 0.04F},
      SDL_FColor{0.85F, 0.13F, 0.19F, 1.0F}
    );
    drawSolidBox(
      renderer,
      camera,
      width,
      height,
      {center.x - shortExtent, center.y - longExtent, z + 0.01F},
      {center.x + shortExtent, center.y + longExtent, z + 0.05F},
      SDL_FColor{0.85F, 0.13F, 0.19F, 1.0F}
    );
  }

  if (settings.showLagCompensation && localLightningGun.hasRewindDebug) {
    const auto drawTargetBounds =
      [&](Vec3 position, CollisionBounds bounds, Uint8 red, Uint8 green, Uint8 blue) {
        SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
        drawWireBox(
          renderer,
          camera,
          width,
          height,
          {
            position.x - bounds.radius,
            position.y - bounds.radius,
            position.z - bounds.halfHeight,
          },
          {
            position.x + bounds.radius,
            position.y + bounds.radius,
            position.z + bounds.halfHeight,
          }
        );
      };
    drawTargetBounds(
      localLightningGun.currentTargetPosition,
      localLightningGun.currentTargetBounds,
      64,
      220,
      255
    );
    drawTargetBounds(
      localLightningGun.rewoundTargetPosition,
      localLightningGun.rewoundTargetBounds,
      255,
      190,
      64
    );
  }

  const auto drawBeam =
    [&](const LightningGunResult& beam, bool local) {
      if (!beam.active) {
        return;
      }
      SDL_FColor beamColor = local
        ? localBeamColor(settings)
        : SDL_FColor{
            static_cast<float>(settings.enemyBeamRed) / 255.0F,
            static_cast<float>(settings.enemyBeamGreen) / 255.0F,
            static_cast<float>(settings.enemyBeamBlue) / 255.0F,
            settings.enemyBeamAlpha,
          };
      const float pulse = std::clamp(settings.beamPulse, -1.0F, 1.0F);
      const float brightness = 1.0F + pulse * 0.05F;
      beamColor.r = std::clamp(beamColor.r * brightness, 0.0F, 1.0F);
      beamColor.g = std::clamp(beamColor.g * brightness, 0.0F, 1.0F);
      beamColor.b = std::clamp(beamColor.b * brightness, 0.0F, 1.0F);
      SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(beamColor.r * 255.0F),
        static_cast<Uint8>(beamColor.g * 255.0F),
        static_cast<Uint8>(beamColor.b * 255.0F),
        static_cast<Uint8>(beamColor.a * 255.0F)
      );
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      if (local) {
        const float centerX = static_cast<float>(width) * 0.5F;
        const float centerY = static_cast<float>(height) * 0.5F;
        drawThickLine(
          renderer,
          centerX,
          static_cast<float>(height) * 1.15F,
          centerX,
          centerY,
          settings.beamWidth * (1.0F + pulse * 0.04F)
        );
        return;
      }
      drawThickPerspectiveLine(
        renderer,
        camera,
        width,
        height,
        beam.start,
        beam.end,
        settings.enemyBeamWidth * (1.0F + pulse * 0.04F)
      );
    };
  for (const RemotePlayerView& remote : remotePlayers) {
    if (remote.visible && remote.player.health > 0) {
      drawBeam(remote.lightningGun, false);
    }
  }
  drawBeam(localLightningGun, true);

  for (std::size_t fireIndex = 0; fireIndex < weaponFires.size(); ++fireIndex) {
    const WeaponFireResult& fire = weaponFires[fireIndex];
    if (!fire.fired) {
      continue;
    }
    if (fire.weapon == Weapon::Railgun) {
      Vec3 trace = fireIndex < settings.sniperSmokeTracerDirections.size()
        ? settings.sniperSmokeTracerDirections[fireIndex]
        : Vec3{};
      float traceLength = fireIndex < settings.sniperSmokeTracerTraceLengths.size()
        ? settings.sniperSmokeTracerTraceLengths[fireIndex]
        : 0.0F;
      if (length(trace) <= 0.0001F || traceLength <= 0.0001F) {
        trace = fire.end - fire.start;
        traceLength = length(trace);
      }
      if (length(trace) <= 0.0001F || traceLength <= 0.0001F) {
        continue;
      }
      const float alpha = fireIndex < settings.sniperSmokeTracerAlpha.size()
        ? std::clamp(settings.sniperSmokeTracerAlpha[fireIndex], 0.0F, 1.0F)
        : 1.0F;
      if (alpha <= 0.0F) {
        continue;
      }
      const Vec3 smokeEnd = fire.start + normalize(trace) * std::min(
        traceLength,
        kSniperSmokeTracerMaximumLength
      );
      // The SDL fallback has no translucent mesh path. Keep a short neutral
      // smoke cue so it matches the GPU path's limits without a glow beam.
      SDL_SetRenderDrawColor(
        renderer,
        164,
        170,
        170,
        static_cast<Uint8>(std::round(150.0F * alpha))
      );
      drawThickPerspectiveLine(
        renderer,
        camera,
        width,
        height,
        fire.start,
        smokeEnd,
        1.5F
      );
      continue;
    }
    if (fire.weapon == Weapon::Revolver) {
      SDL_SetRenderDrawColor(
        renderer,
        fire.hit ? 255 : 128,
        fire.hit ? 248 : 230,
        fire.hit ? 180 : 255,
        255
      );
      drawThickPerspectiveLine(
        renderer,
        camera,
        width,
        height,
        fire.start,
        fire.end,
        fire.hit ? 4.0F : 2.5F
      );
    }
  }
  for (const RocketProjectileSnapshot& projectile : rockets) {
    if (!projectile.active) {
      continue;
    }
    const float size = projectile.radius > 0.0F ? projectile.radius : 0.14F;
    if (projectile.weapon == Weapon::GrenadeLauncher) {
      SDL_SetRenderDrawColor(renderer, 255, 126, 40, 255);
      drawWireBox(
        renderer,
        camera,
        width,
        height,
        projectile.position - Vec3{size * 1.4F, size * 1.4F, size * 1.4F},
        projectile.position + Vec3{size * 1.4F, size * 1.4F, size * 1.4F}
      );
      continue;
    }
    SDL_SetRenderDrawColor(renderer, 255, 126, 40, 255);
    drawWireBox(
      renderer,
      camera,
      width,
      height,
      projectile.position - Vec3{size, size, size},
      projectile.position + Vec3{size, size, size}
    );
  }
  (void)rocketExplosions;
}

#endif

} // namespace

Renderer::~Renderer() {
  shutdown();
}

bool Renderer::initialize(void* window) {
#if LG_DUEL_HAS_SDL3
  window_ = window;
  requestedBackendName_ = gpuBackendRequested() ? "gpu" : "fallback";
  vulkanApiVersion_ = environmentValue("LG_DUEL_VULKAN_API_VERSION");
  vulkanIcdPath_ = environmentValue("LG_DUEL_VULKAN_ICD_PATH");
  vulkanIcdSha256_ = environmentValue("LG_DUEL_VULKAN_ICD_SHA256");
  if (gpuBackendRequested()) {
    SDL_GPUDevice* device = createGpuDevice();
    if (device != nullptr) {
      if (SDL_ClaimWindowForGPUDevice(
            device,
            static_cast<SDL_Window*>(window)
          )) {
        const SDL_GPUTextureFormat depthFormat =
          chooseDepthStencilFormat(device);
        const SDL_GPUTextureFormat swapchainFormat =
          SDL_GetGPUSwapchainTextureFormat(
            device,
            static_cast<SDL_Window*>(window)
          );
        SDL_GPUTextureFormat sceneColorFormat =
          chooseLinearSceneColorFormat(device);
        SDL_GPUGraphicsPipeline* pipeline = createGpuPipeline(
          device,
          static_cast<SDL_Window*>(window)
        );
        GpuSceneColorPipelines scenePipelines =
          createGpuSceneColorPipelines(
            device,
            static_cast<SDL_Window*>(window),
            depthFormat,
            sceneColorFormat
          );
        if (
          sceneColorFormat == SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT &&
          !sceneColorPipelinesReady(scenePipelines)
        ) {
          destroyGpuSceneColorPipelines(device, scenePipelines);
          sceneColorFormat = chooseLinearSceneColorFormat(device, false);
          std::cerr << "SDL_GPU R11 scene pipeline setup failed; retrying "
            << gpuTextureFormatName(sceneColorFormat) << '\n';
          scenePipelines = createGpuSceneColorPipelines(
            device,
            static_cast<SDL_Window*>(window),
            depthFormat,
            sceneColorFormat
          );
        }
        GpuDirectPresentPipelines directPipelines =
          createGpuDirectPresentPipelines(
            device,
            static_cast<SDL_Window*>(window),
            depthFormat,
            swapchainFormat
          );
        if (!directPresentPipelinesReady(directPipelines)) {
          destroyGpuDirectPresentPipelines(device, directPipelines);
          std::cerr
            << "SDL_GPU direct-present pipeline setup failed; using "
            << "scene composite\n";
        }
        SDL_GPUGraphicsPipeline* skyPipeline = createGpuSkyPipeline(
          device,
          sceneColorFormat,
          depthFormat,
          SDL_GPU_SAMPLECOUNT_1,
          false
        );
        SDL_GPUGraphicsPipeline* directSkyPipeline =
          createGpuSkyPipeline(
            device,
            swapchainFormat,
            depthFormat,
            SDL_GPU_SAMPLECOUNT_1,
            true
          );
        if (skyPipeline == nullptr || directSkyPipeline == nullptr) {
          std::cerr
            << "SDL_GPU sky pipeline setup failed; maps will keep the "
            << "existing clear colour\n";
        }
        SDL_GPUGraphicsPipeline* pipeline3D = scenePipelines.world;
        SDL_GPUGraphicsPipeline* pipelineWorldSurface =
          scenePipelines.worldSurface;
        SDL_GPUGraphicsPipeline* pipeline3DTranslucent =
          scenePipelines.translucent;
        SDL_GPUGraphicsPipeline* instancedMeshPipeline =
          scenePipelines.instancedMesh;
        SDL_GPUGraphicsPipeline* staticMeshPipeline =
          scenePipelines.staticMesh;
        SDL_GPUGraphicsPipeline* materialMeshPipeline =
          scenePipelines.materialMesh;
        SDL_GPUGraphicsPipeline* staticMeshViewModelPipeline =
          scenePipelines.staticViewModel;
        SDL_GPUGraphicsPipeline* materialMeshViewModelPipeline =
          scenePipelines.materialViewModel;
        SDL_GPUGraphicsPipeline* gltfPlayerModelPipeline =
          scenePipelines.gltfPlayer;
        SDL_GPUGraphicsPipeline* gltfPlayerModelFlatPipeline =
          scenePipelines.gltfPlayerFlat;
        SDL_GPUGraphicsPipeline* instancedGlowPipeline =
          scenePipelines.instancedGlow;
        SDL_GPUGraphicsPipeline* depthWorldPipeline = createGpuPipeline3D(
          device,
          static_cast<SDL_Window*>(window),
          true,
          depthFormat,
          SDL_GPU_COMPAREOP_LESS,
          "world3d.frag.spv",
          1,
          SDL_GPU_SAMPLECOUNT_1,
          SDL_GPU_TEXTUREFORMAT_INVALID,
          1,
          true,
          "outline_mask_world.vert.spv"
        );
        SDL_GPUGraphicsPipeline* depthInstancedPipeline =
          createGpuInstancedPipeline3D(
            device,
            static_cast<SDL_Window*>(window),
            "instanced_mesh.vert.spv",
            "instanced_color.frag.spv",
            true,
            false,
            depthFormat,
            SDL_GPU_SAMPLECOUNT_1,
            SDL_GPU_TEXTUREFORMAT_INVALID,
            true,
            true
          );
        SDL_GPUGraphicsPipeline* depthStaticPipeline =
          createGpuStaticMeshPipeline3D(
            device,
            static_cast<SDL_Window*>(window),
            depthFormat,
            SDL_GPU_SAMPLECOUNT_1,
            SDL_GPU_TEXTUREFORMAT_INVALID,
            true
          );
        SDL_GPUGraphicsPipeline* depthMaterialPipeline =
          createGpuMaterialMeshPipeline3D(
            device,
            static_cast<SDL_Window*>(window),
            depthFormat,
            SDL_GPU_SAMPLECOUNT_1,
            SDL_GPU_TEXTUREFORMAT_INVALID,
            true
          );
        SDL_GPUGraphicsPipeline* depthGltfPipeline =
          createGpuGltfPlayerModelPipeline(
            device,
            static_cast<SDL_Window*>(window),
            false,
            depthFormat,
            SDL_GPU_SAMPLECOUNT_1,
            SDL_GPU_TEXTUREFORMAT_INVALID,
            true
          );
        SDL_GPUGraphicsPipeline* bloomSourcePipeline =
          scenePipelines.bloomSource;
        SDL_GPUGraphicsPipeline* bloomBlurPipeline =
          scenePipelines.bloomBlur;
        constexpr FragmentResourceLayout kSceneCompositeLayout =
          sceneCompositeFragmentLayout();
        SDL_GPUGraphicsPipeline* sceneCompositePipeline =
          createGpuPostProcessPipeline(
            device,
            "scene_composite.frag.spv",
            kSceneCompositeLayout.samplers,
            SDL_GetGPUSwapchainTextureFormat(
              device,
              static_cast<SDL_Window*>(window)
            )
          );
        constexpr FragmentResourceLayout kSceneCompositeNoBloomLayout =
          sceneCompositeNoBloomFragmentLayout();
        SDL_GPUGraphicsPipeline* sceneCompositeNoBloomPipeline =
          createGpuPostProcessPipeline(
            device,
            "scene_composite_no_bloom.frag.spv",
            kSceneCompositeNoBloomLayout.samplers,
            SDL_GetGPUSwapchainTextureFormat(
              device,
              static_cast<SDL_Window*>(window)
            )
          );
        SDL_GPUGraphicsPipeline* pipelineOutlineMask =
          createGpuPipelineOutlineMask(device, depthFormat);
        SDL_GPUGraphicsPipeline* staticMeshOutlineMaskPipeline =
          createGpuStaticMeshOutlineMaskPipeline(device, depthFormat, false);
        SDL_GPUGraphicsPipeline* materialMeshOutlineMaskPipeline =
          createGpuStaticMeshOutlineMaskPipeline(device, depthFormat, true);
        SDL_GPUGraphicsPipeline* gltfPlayerModelOutlineMaskPipeline =
          createGpuGltfPlayerModelPipeline(
            device,
            static_cast<SDL_Window*>(window),
            true,
            depthFormat
          );
        SDL_GPUGraphicsPipeline* pipelineOutlineClear =
          createGpuPipelineOutlineClear(device, depthFormat);
        SDL_GPUGraphicsPipeline* pipelineOutlineColorClear =
          createGpuPipelineOutlineColorClear(device);
        SDL_GPUGraphicsPipeline* pipelineOutlineDilation =
          createGpuPipelineOutlineDilation(device);
        SDL_GPUGraphicsPipeline* pipelineOutlineComposite =
          createGpuPipelineOutlineComposite(
            device,
            static_cast<SDL_Window*>(window)
          );
        SDL_GPUGraphicsPipeline* pipelineOutlineNativeDilation =
          createGpuPipelineOutlineDilation(
            device,
            "outline_native_dilate.frag.spv"
          );
        SDL_GPUGraphicsPipeline* pipelineOutlineNativeComposite =
          createGpuPipelineOutlineComposite(
            device,
            static_cast<SDL_Window*>(window),
            "outline_native_composite.frag.spv"
          );
        SDL_GPUGraphicsPipeline* sunShadowWorldPipeline =
          createGpuSunShadowPipeline(device, depthFormat, 0);
        SDL_GPUGraphicsPipeline* sunShadowStaticPipeline =
          createGpuSunShadowPipeline(device, depthFormat, 1);
        SDL_GPUGraphicsPipeline* sunShadowMaterialPipeline =
          createGpuSunShadowPipeline(device, depthFormat, 2);
        SDL_GPUGraphicsPipeline* sunShadowGltfPipeline =
          createGpuGltfSunShadowPipeline(device, depthFormat);
        SDL_GPUGraphicsPipeline* pointShadowWorldPipeline =
          createGpuPointShadowPipeline(device, depthFormat);
        if (pointShadowWorldPipeline == nullptr) {
          std::cerr
            << "SDL_GPU point-shadow pipeline setup failed; point shadows "
            << "are disabled for this renderer\n";
        }
        GpuSimpleResources* simpleResources = createGpuSimpleResources(device);
        GpuGltfPlayerResources* gltfPlayerResources =
          createGpuGltfPlayerResources(device, workerPlayerModel());
        const SDL_GPUBufferCreateInfo vertexBufferInfo = {
          SDL_GPU_BUFFERUSAGE_VERTEX,
          static_cast<Uint32>(kMaxGpuVertices * sizeof(GpuVertex)),
          0,
        };
        SDL_GPUBuffer* vertexBuffer =
          SDL_CreateGPUBuffer(device, &vertexBufferInfo);
        const SDL_GPUTransferBufferCreateInfo transferBufferInfo = {
          SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
          static_cast<Uint32>(kMaxGpuVertices * sizeof(GpuVertex)),
          0,
        };
        SDL_GPUTransferBuffer* transferBuffer =
          SDL_CreateGPUTransferBuffer(device, &transferBufferInfo);
        FontAtlasSet* fontAtlasSet = createFontAtlasSet(
          device,
          "bahnschrift.ttf"
        );
        const SDL_GPUSamplerCreateInfo fontSamplerInfo = {
          SDL_GPU_FILTER_LINEAR,
          SDL_GPU_FILTER_LINEAR,
          SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
          SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          0.0F,
          1.0F,
          SDL_GPU_COMPAREOP_ALWAYS,
          0.0F,
          0.0F,
          false,
          false,
          0,
          0,
          0,
        };
        const SDL_GPUSamplerCreateInfo nearestSamplerInfo = {
          SDL_GPU_FILTER_NEAREST,
          SDL_GPU_FILTER_NEAREST,
          SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
          SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          0.0F,
          1.0F,
          SDL_GPU_COMPAREOP_ALWAYS,
          0.0F,
          0.0F,
          false,
          false,
          0,
          0,
          0,
        };
        SDL_GPUSampler* fontSampler =
          SDL_CreateGPUSampler(device, &fontSamplerInfo);
        SDL_GPUSampler* outlineMaskSampler =
          SDL_CreateGPUSampler(device, &nearestSamplerInfo);
        SDL_GPUSampler* postProcessSampler =
          SDL_CreateGPUSampler(device, &fontSamplerInfo);
        SDL_GPUSamplerCreateInfo shadowSamplerInfo = nearestSamplerInfo;
        shadowSamplerInfo.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        shadowSamplerInfo.enable_compare = true;
        SDL_GPUSampler* sunShadowSampler =
          SDL_CreateGPUSampler(device, &shadowSamplerInfo);
        SDL_GPUSampler* pointShadowSampler =
          SDL_CreateGPUSampler(device, &shadowSamplerInfo);
        SDL_GPUTexture* sunShadowFallbackTexture =
          createClearedSunShadowFallbackTexture(device, depthFormat);
        SDL_GPUTexture* pointShadowFallbackTexture =
          createClearedPointShadowFallbackTexture(device, depthFormat);
        if (
          pipeline != nullptr &&
          pipelineWorldSurface != nullptr &&
          pipeline3D != nullptr &&
          pipeline3DTranslucent != nullptr &&
          instancedMeshPipeline != nullptr &&
          staticMeshPipeline != nullptr &&
          materialMeshPipeline != nullptr &&
          staticMeshViewModelPipeline != nullptr &&
          materialMeshViewModelPipeline != nullptr &&
          gltfPlayerModelPipeline != nullptr &&
          gltfPlayerModelFlatPipeline != nullptr &&
          depthWorldPipeline != nullptr &&
          depthInstancedPipeline != nullptr &&
          depthStaticPipeline != nullptr &&
          depthMaterialPipeline != nullptr &&
          depthGltfPipeline != nullptr &&
          instancedGlowPipeline != nullptr &&
          bloomSourcePipeline != nullptr &&
          bloomBlurPipeline != nullptr &&
          sceneCompositePipeline != nullptr &&
          sceneCompositeNoBloomPipeline != nullptr &&
          pipelineOutlineClear != nullptr &&
          pipelineOutlineColorClear != nullptr &&
          pipelineOutlineMask != nullptr &&
          staticMeshOutlineMaskPipeline != nullptr &&
          materialMeshOutlineMaskPipeline != nullptr &&
          gltfPlayerModelOutlineMaskPipeline != nullptr &&
          pipelineOutlineDilation != nullptr &&
          pipelineOutlineComposite != nullptr &&
          pipelineOutlineNativeDilation != nullptr &&
          pipelineOutlineNativeComposite != nullptr &&
          sunShadowWorldPipeline != nullptr &&
          sunShadowStaticPipeline != nullptr &&
          sunShadowMaterialPipeline != nullptr &&
          sunShadowGltfPipeline != nullptr &&
          simpleResources != nullptr &&
          gltfPlayerResources != nullptr &&
          vertexBuffer != nullptr &&
          transferBuffer != nullptr &&
          fontAtlasSet != nullptr &&
          fontAtlasSet->atlases[kDefaultUiFontPixelHeightIndex] != nullptr &&
          fontAtlasSet->atlases[kDefaultUiFontPixelHeightIndex]->texture !=
            nullptr &&
          fontSampler != nullptr &&
          outlineMaskSampler != nullptr &&
          postProcessSampler != nullptr &&
          sunShadowSampler != nullptr &&
          sunShadowFallbackTexture != nullptr &&
          pointShadowSampler != nullptr &&
          pointShadowFallbackTexture != nullptr &&
          SDL_SetGPUAllowedFramesInFlight(device, 1)
        ) {
          gpuDevice_ = device;
          gpuPipeline_ = pipeline;
          gpuPipelineWorldSurface_ = pipelineWorldSurface;
          gpuPipeline3D_ = pipeline3D;
          gpuPipeline3DTranslucent_ = pipeline3DTranslucent;
          gpuPipelineInstancedMesh_ = instancedMeshPipeline;
          gpuPipelineStaticMesh_ = staticMeshPipeline;
          gpuPipelineMaterialMesh_ = materialMeshPipeline;
          gpuPipelineStaticMeshViewModel_ = staticMeshViewModelPipeline;
          gpuPipelineMaterialMeshViewModel_ = materialMeshViewModelPipeline;
          gpuPipelineGltfPlayerModel_ = gltfPlayerModelPipeline;
          gpuPipelineGltfPlayerModelFlat_ = gltfPlayerModelFlatPipeline;
          gpuPipelineDirectWorldSurface_ = directPipelines.worldSurface;
          gpuPipelineDirectWorld_ = directPipelines.world;
          gpuPipelineDirectInstancedMesh_ = directPipelines.instancedMesh;
          gpuPipelineDirectStaticMesh_ = directPipelines.staticMesh;
          gpuPipelineDirectMaterialMesh_ = directPipelines.materialMesh;
          gpuPipelineDirectGltfPlayer_ = directPipelines.gltfPlayer;
          gpuPipelineDepthWorld_ = depthWorldPipeline;
          gpuPipelineDepthInstanced_ = depthInstancedPipeline;
          gpuPipelineDepthStatic_ = depthStaticPipeline;
          gpuPipelineDepthMaterial_ = depthMaterialPipeline;
          gpuPipelineDepthGltf_ = depthGltfPipeline;
          gpuPipelineInstancedGlow_ = instancedGlowPipeline;
          gpuPipelineBloomSource_ = bloomSourcePipeline;
          gpuPipelineBloomBlur_ = bloomBlurPipeline;
          gpuPipelineSceneComposite_ = sceneCompositePipeline;
          gpuPipelineSceneCompositeNoBloom_ =
            sceneCompositeNoBloomPipeline;
          gpuPipelineSky_ = skyPipeline;
          gpuPipelineDirectSky_ = directSkyPipeline;
          gpuPipelineOutlineClear_ = pipelineOutlineClear;
          gpuPipelineOutlineColorClear_ = pipelineOutlineColorClear;
          gpuPipelineOutlineMask_ = pipelineOutlineMask;
          gpuPipelineStaticMeshOutlineMask_ = staticMeshOutlineMaskPipeline;
          gpuPipelineMaterialMeshOutlineMask_ =
            materialMeshOutlineMaskPipeline;
          gpuPipelineGltfPlayerModelOutlineMask_ =
            gltfPlayerModelOutlineMaskPipeline;
          gpuPipelineOutlineDilation_ = pipelineOutlineDilation;
          gpuPipelineOutlineComposite_ = pipelineOutlineComposite;
          gpuPipelineOutlineNativeDilation_ =
            pipelineOutlineNativeDilation;
          gpuPipelineOutlineNativeComposite_ = pipelineOutlineNativeComposite;
          gpuPipelineSunShadowWorld_ = sunShadowWorldPipeline;
          gpuPipelineSunShadowStatic_ = sunShadowStaticPipeline;
          gpuPipelineSunShadowMaterial_ = sunShadowMaterialPipeline;
          gpuPipelineSunShadowGltf_ = sunShadowGltfPipeline;
          gpuPipelinePointShadowWorld_ = pointShadowWorldPipeline;
          gpuDepthFormat_ = static_cast<std::uint32_t>(depthFormat);
          gpuSceneColorFormat_ =
            static_cast<std::uint32_t>(sceneColorFormat);
          gpuVertexBuffer_ = vertexBuffer;
          gpuTransferBuffer_ = transferBuffer;
          gpuSimpleResources_ = simpleResources;
          gpuSkyResources_ = new GpuSkyResources();
          gpuGltfPlayerResources_ = gltfPlayerResources;
          gpuFontAtlas_ = fontAtlasSet;
          gpuFontSampler_ = fontSampler;
          gpuOutlineMaskSampler_ = outlineMaskSampler;
          gpuPostProcessSampler_ = postProcessSampler;
          gpuSunShadowSampler_ = sunShadowSampler;
          gpuSunShadowFallbackTexture_ = sunShadowFallbackTexture;
          gpuPointShadowSampler_ = pointShadowSampler;
          gpuPointShadowFallbackTexture_ = pointShadowFallbackTexture;
          gpuWorldTextureAtlas_ = nullptr;
          auto* vertexScratch = new std::vector<GpuVertex>();
          vertexScratch->reserve(kMaxGpuVertices);
          gpuVertexScratch_ = vertexScratch;
          gpuBackend_ = true;
          const char* driver = SDL_GetGPUDeviceDriver(device);
          backendName_ = "SDL_GPU/";
          backendName_ += driver != nullptr ? driver : "unknown";
          const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(device);
          if (properties != 0) {
            gpuName_ = SDL_GetStringProperty(
              properties,
              SDL_PROP_GPU_DEVICE_NAME_STRING,
              ""
            );
            graphicsDriverName_ = SDL_GetStringProperty(
              properties,
              SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,
              ""
            );
            graphicsDriverVersion_ = SDL_GetStringProperty(
              properties,
              SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,
              ""
            );
            graphicsDriverInfo_ = SDL_GetStringProperty(
              properties,
              SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,
              ""
            );
          }
          softwareRenderer_ = looksLikeSoftwareRenderer(
            gpuName_ + " " + graphicsDriverName_ + " " + graphicsDriverInfo_
          );
          gpuTimestampTiming_.initialize(device, backendName_);
          return true;
        }

        std::cerr
          << "SDL_GPU resource initialization failed: " << SDL_GetError()
          << "\nGPU startup aborted.\n";
        if (transferBuffer != nullptr) {
          SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        }
        if (fontSampler != nullptr) {
          SDL_ReleaseGPUSampler(device, fontSampler);
        }
        if (outlineMaskSampler != nullptr) {
          SDL_ReleaseGPUSampler(device, outlineMaskSampler);
        }
        if (postProcessSampler != nullptr) {
          SDL_ReleaseGPUSampler(device, postProcessSampler);
        }
        if (sunShadowSampler != nullptr) {
          SDL_ReleaseGPUSampler(device, sunShadowSampler);
        }
        if (pointShadowSampler != nullptr) {
          SDL_ReleaseGPUSampler(device, pointShadowSampler);
        }
        if (sunShadowFallbackTexture != nullptr) {
          SDL_ReleaseGPUTexture(device, sunShadowFallbackTexture);
        }
        if (pointShadowFallbackTexture != nullptr) {
          SDL_ReleaseGPUTexture(device, pointShadowFallbackTexture);
        }
        if (fontAtlasSet != nullptr) {
          destroyFontAtlasSet(device, fontAtlasSet);
        }
        if (vertexBuffer != nullptr) {
          SDL_ReleaseGPUBuffer(device, vertexBuffer);
        }
        destroyGpuSimpleResources(device, simpleResources);
        destroyGpuGltfPlayerResources(device, gltfPlayerResources);
        destroyGpuDirectPresentPipelines(device, directPipelines);
        if (skyPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, skyPipeline);
        }
        if (directSkyPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, directSkyPipeline);
        }
        if (pipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        }
        if (pipelineWorldSurface != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineWorldSurface);
        }
        if (pipeline3D != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline3D);
        }
        if (pipeline3DTranslucent != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline3DTranslucent);
        }
        if (instancedMeshPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, instancedMeshPipeline);
        }
        if (staticMeshPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, staticMeshPipeline);
        }
        if (materialMeshPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, materialMeshPipeline);
        }
        if (staticMeshViewModelPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, staticMeshViewModelPipeline);
        }
        if (materialMeshViewModelPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, materialMeshViewModelPipeline);
        }
        if (gltfPlayerModelPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, gltfPlayerModelPipeline);
        }
        if (gltfPlayerModelFlatPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, gltfPlayerModelFlatPipeline);
        }
        for (SDL_GPUGraphicsPipeline* depthPipeline : {
               depthWorldPipeline,
               depthInstancedPipeline,
               depthStaticPipeline,
               depthMaterialPipeline,
               depthGltfPipeline,
             }) {
          if (depthPipeline != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device, depthPipeline);
          }
        }
        if (instancedGlowPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, instancedGlowPipeline);
        }
        if (bloomSourcePipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, bloomSourcePipeline);
        }
        if (bloomBlurPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, bloomBlurPipeline);
        }
        if (sceneCompositePipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, sceneCompositePipeline);
        }
        if (sceneCompositeNoBloomPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(
            device,
            sceneCompositeNoBloomPipeline
          );
        }
        if (pipelineOutlineClear != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineOutlineClear);
        }
        if (pipelineOutlineColorClear != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineOutlineColorClear);
        }
        if (pipelineOutlineMask != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineOutlineMask);
        }
        if (staticMeshOutlineMaskPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, staticMeshOutlineMaskPipeline);
        }
        if (materialMeshOutlineMaskPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(
            device,
            materialMeshOutlineMaskPipeline
          );
        }
        if (gltfPlayerModelOutlineMaskPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, gltfPlayerModelOutlineMaskPipeline);
        }
        if (pipelineOutlineDilation != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineOutlineDilation);
        }
        if (pipelineOutlineComposite != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineOutlineComposite);
        }
        if (pipelineOutlineNativeDilation != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(
            device,
            pipelineOutlineNativeDilation
          );
        }
        if (pipelineOutlineNativeComposite != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipelineOutlineNativeComposite);
        }
        if (sunShadowWorldPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, sunShadowWorldPipeline);
        }
        if (sunShadowStaticPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, sunShadowStaticPipeline);
        }
        if (sunShadowMaterialPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, sunShadowMaterialPipeline);
        }
        if (sunShadowGltfPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, sunShadowGltfPipeline);
        }
        if (pointShadowWorldPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pointShadowWorldPipeline);
        }
        SDL_ReleaseWindowFromGPUDevice(
          device,
          static_cast<SDL_Window*>(window)
        );
        SDL_DestroyGPUDevice(device);
        device = nullptr;
      }

      if (device != nullptr) {
        std::cerr
          << "SDL_GPU could not claim the window: " << SDL_GetError()
          << "\nGPU startup aborted.\n";
        SDL_DestroyGPUDevice(device);
      }
    }
  }

  if (gpuBackendRequested()) {
    // An explicit GPU request is a renderer-class requirement. Silently
    // changing it to SDL_Renderer makes visual review and benchmarks invalid.
    std::cerr
      << "SDL_GPU/vulkan was requested but could not be initialized; "
      << "refusing SDL_Renderer fallback.\n";
    backendName_ = "unavailable";
    window_ = nullptr;
    return false;
  }

  renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window), nullptr);
  if (renderer_ == nullptr) {
    backendName_ = "unavailable";
    window_ = nullptr;
    return false;
  }
  const char* rendererName =
    SDL_GetRendererName(static_cast<SDL_Renderer*>(renderer_));
  backendName_ = "SDL_Renderer/";
  backendName_ += rendererName != nullptr ? rendererName : "unknown";
  gpuTimestampTiming_.initialize(nullptr, backendName_);
  gpuName_.clear();
  graphicsDriverName_.clear();
  graphicsDriverVersion_.clear();
  graphicsDriverInfo_.clear();
  softwareRenderer_ = rendererName != nullptr &&
    looksLikeSoftwareRenderer(rendererName);
  return true;
#else
  (void)window;
  return false;
#endif
}

void Renderer::render(
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const IcePoolArray& icePools,
  const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  std::uint32_t newExplosionEventsConsumed,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console,
  LateViewSampler lateViewSampler,
  const FrameCaptureRequest* captureRequest,
  FrameCaptureResult* captureResult
) {
#if LG_DUEL_HAS_SDL3
  const auto renderStart = RenderClock::now();
  lastFrameDiagnostics_.lateMouseSampleMilliseconds = 0.0F;
  lastFrameDiagnostics_.mouseSampleToSubmitMilliseconds = 0.0F;
  lastFrameDiagnostics_.mouseSamplePhaseGainMilliseconds = 0.0F;
  lastFrameDiagnostics_.lateMouseSampleEnabled =
    lateViewSampler.sample != nullptr;
  lastFrameDiagnostics_.lateMouseSampleApplied = false;
  float stepSmoothingDt = 0.0F;
  if (previousCameraStepUpdate_ != RenderClock::time_point{}) {
    stepSmoothingDt = std::clamp(
      secondsBetween(previousCameraStepUpdate_, renderStart),
      0.0F,
      0.1F
    );
  }
  previousCameraStepUpdate_ = renderStart;
  if (hasPreviousCameraPlayerZ_) {
    // Compensate only the rendered camera for step-sized vertical discontinuities.
    // Authoritative and predicted player positions remain untouched.
    const float playerZDelta = player.position.z - previousCameraPlayerZ_;
    if (
      player.onGround &&
      playerZDelta > 0.01F &&
      playerZDelta <= kMaxVisualStepSmoothHeight
    ) {
      cameraStepOffset_ =
        std::max(cameraStepOffset_ - playerZDelta, -kMaxVisualStepSmoothHeight);
    } else if (
      player.onGround &&
      playerZDelta < -0.01F &&
      -playerZDelta <= kMaxVisualStepSmoothHeight
    ) {
      cameraStepOffset_ =
        std::min(cameraStepOffset_ - playerZDelta, kMaxVisualStepSmoothHeight);
    } else if (std::fabs(playerZDelta) > kMaxVisualStepSmoothHeight) {
      // Large changes are jumps, teleports, or corrections; smoothing them would
      // make the camera visibly lag behind authoritative movement.
      cameraStepOffset_ = 0.0F;
    }
  }
  previousCameraPlayerZ_ = player.position.z;
  hasPreviousCameraPlayerZ_ = true;
  if (cameraStepOffset_ < 0.0F) {
    cameraStepOffset_ = std::min(
      0.0F,
      cameraStepOffset_ + (kVisualStepSmoothSpeed * stepSmoothingDt)
    );
  } else {
    cameraStepOffset_ = std::max(
      0.0F,
      cameraStepOffset_ - (kVisualStepSmoothSpeed * stepSmoothingDt)
    );
  }

  if (gpuBackend_) {
    auto* gpuDevice = static_cast<SDL_GPUDevice*>(gpuDevice_);
    auto* gpuWindow = static_cast<SDL_Window*>(window_);
    const SDL_GPUTextureFormat depthFormat =
      static_cast<SDL_GPUTextureFormat>(gpuDepthFormat_);
    const SDL_GPUTextureFormat colorFormat =
      static_cast<SDL_GPUTextureFormat>(gpuSceneColorFormat_);
    const auto sampleCountFor = [](std::uint32_t samples) {
      return samples >= 4U
        ? SDL_GPU_SAMPLECOUNT_4
        : samples >= 2U ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
    };
    std::uint32_t desiredSamples =
      antiAliasingSampleCount(settings.antiAliasingQuality);
    while (
      desiredSamples > 1U &&
      (
        !SDL_GPUTextureSupportsSampleCount(
          gpuDevice,
          colorFormat,
          sampleCountFor(desiredSamples)
        ) ||
        !SDL_GPUTextureSupportsSampleCount(
          gpuDevice,
          depthFormat,
          sampleCountFor(desiredSamples)
        )
      )
    ) {
      desiredSamples /= 2U;
    }
    if (desiredSamples != gpuSampleCount_) {
      const SDL_GPUSampleCount desiredSampleCount =
        sampleCountFor(desiredSamples);
      SDL_GPUGraphicsPipeline* replacementWorldSurface = createGpuPipeline3D(
        gpuDevice,
        gpuWindow,
        true,
        depthFormat,
        SDL_GPU_COMPAREOP_LESS,
        "world_surface.frag.spv",
        3,
        desiredSampleCount,
        colorFormat,
        2,
        false,
        "world_surface.vert.spv"
      );
      SDL_GPUGraphicsPipeline* replacement3D = createGpuPipeline3D(
        gpuDevice,
        gpuWindow,
        true,
        depthFormat,
        SDL_GPU_COMPAREOP_LESS,
        "world3d.frag.spv",
        1,
        desiredSampleCount,
        colorFormat
      );
      SDL_GPUGraphicsPipeline* replacement3DTranslucent =
        createGpuPipeline3D(
          gpuDevice,
          gpuWindow,
          false,
          depthFormat,
          SDL_GPU_COMPAREOP_LESS,
          "world3d.frag.spv",
          1,
          desiredSampleCount,
          colorFormat
        );
      SDL_GPUGraphicsPipeline* replacementInstanced =
        createGpuInstancedPipeline3D(
          gpuDevice,
          gpuWindow,
          "instanced_mesh.vert.spv",
          "instanced_color.frag.spv",
          true,
          false,
          depthFormat,
          desiredSampleCount,
          colorFormat,
          true,
          false,
          {1U, 1U}
        );
      SDL_GPUGraphicsPipeline* replacementStatic =
        createGpuStaticMeshPipeline3D(
          gpuDevice,
          gpuWindow,
          depthFormat,
          desiredSampleCount,
          colorFormat,
          false,
          "instanced_color.frag.spv",
          instancedColorFragmentLayout(),
          true
        );
      SDL_GPUGraphicsPipeline* replacementMaterial =
        createGpuMaterialMeshPipeline3D(
          gpuDevice,
          gpuWindow,
          depthFormat,
          desiredSampleCount,
          colorFormat,
          false,
          "material_weapon.frag.spv",
          {3U, 1U},
          true
        );
      SDL_GPUGraphicsPipeline* replacementGltf =
        createGpuGltfPlayerModelPipeline(
          gpuDevice,
          gpuWindow,
          false,
          depthFormat,
          desiredSampleCount,
          colorFormat,
          false,
          "gltf_player_model.frag.spv",
          {4U, 1U},
          true
        );
      SDL_GPUGraphicsPipeline* replacementGltfFlat =
        createGpuGltfPlayerModelPipeline(
          gpuDevice,
          gpuWindow,
          false,
          depthFormat,
          desiredSampleCount,
          colorFormat,
          false,
          "gltf_player_model_flat.frag.spv",
          {2U, 1U},
          true
        );
      SDL_GPUGraphicsPipeline* replacementGlow =
        createGpuInstancedPipeline3D(
          gpuDevice,
          gpuWindow,
          "instanced_billboard.vert.spv",
          "instanced_glow.frag.spv",
          false,
          true,
          depthFormat,
          desiredSampleCount,
          colorFormat,
          true,
          false,
          untexturedSceneLightFragmentLayout()
        );
      SDL_GPUGraphicsPipeline* replacementSky = createGpuSkyPipeline(
        gpuDevice,
        colorFormat,
        depthFormat,
        desiredSampleCount,
        false
      );
      const std::array<SDL_GPUGraphicsPipeline*, 9> replacements = {{
        replacementWorldSurface,
        replacement3D,
        replacement3DTranslucent,
        replacementInstanced,
        replacementStatic,
        replacementMaterial,
        replacementGltf,
        replacementGltfFlat,
        replacementGlow,
      }};
      const bool replacementsReady = std::all_of(
        replacements.begin(),
        replacements.end(),
        [](SDL_GPUGraphicsPipeline* pipeline) { return pipeline != nullptr; }
      );
      if (replacementsReady) {
        const std::array<void**, 9> destinations = {{
          &gpuPipelineWorldSurface_,
          &gpuPipeline3D_,
          &gpuPipeline3DTranslucent_,
          &gpuPipelineInstancedMesh_,
          &gpuPipelineStaticMesh_,
          &gpuPipelineMaterialMesh_,
          &gpuPipelineGltfPlayerModel_,
          &gpuPipelineGltfPlayerModelFlat_,
          &gpuPipelineInstancedGlow_,
        }};
        for (std::size_t index = 0; index < destinations.size(); ++index) {
          SDL_ReleaseGPUGraphicsPipeline(
            gpuDevice,
            static_cast<SDL_GPUGraphicsPipeline*>(*destinations[index])
          );
          *destinations[index] = replacements[index];
        }
        if (gpuPipelineSky_ != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(
            gpuDevice,
            static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSky_)
          );
        }
        gpuPipelineSky_ = replacementSky;
        if (gpuDepthTexture_ != nullptr) {
          SDL_ReleaseGPUTexture(
            gpuDevice,
            static_cast<SDL_GPUTexture*>(gpuDepthTexture_)
          );
          gpuDepthTexture_ = nullptr;
        }
        if (gpuMsaaColorTexture_ != nullptr) {
          SDL_ReleaseGPUTexture(
            gpuDevice,
            static_cast<SDL_GPUTexture*>(gpuMsaaColorTexture_)
          );
          gpuMsaaColorTexture_ = nullptr;
        }
        gpuDepthWidth_ = 0U;
        gpuDepthHeight_ = 0U;
        gpuMsaaColorWidth_ = 0U;
        gpuMsaaColorHeight_ = 0U;
        gpuSampleCount_ = desiredSamples;
      } else {
        for (SDL_GPUGraphicsPipeline* pipeline : replacements) {
          if (pipeline != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(gpuDevice, pipeline);
          }
        }
        if (replacementSky != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(gpuDevice, replacementSky);
        }
      }
    }
    // Fence and query checks here never wait for GPU work.
    gpuTimestampTiming_.poll(gpuDevice_);
    auto* fontAtlasSet = static_cast<FontAtlasSet*>(gpuFontAtlas_);
    const std::string requestedFont =
      settings.uiFont.empty() ? "bahnschrift.ttf" : settings.uiFont;
    if (
      fontAtlasSet == nullptr ||
      fontAtlasSet->requestedFont != requestedFont
    ) {
      FontAtlasSet* replacement = createFontAtlasSet(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        requestedFont
      );
      if (replacement != nullptr) {
        // Keep the old atlas alive until replacement creation succeeds so an
        // invalid runtime font request cannot destroy readable UI text.
        destroyFontAtlasSet(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          fontAtlasSet
        );
        gpuFontAtlas_ = replacement;
        fontAtlasSet = replacement;
      }
    }
    if (fontAtlasSet == nullptr) {
      if (!gpuErrorReported_) {
        std::cerr << "SDL_GPU font atlas set is unavailable.\n";
        gpuErrorReported_ = true;
      }
      return;
    }
    auto* depthTexture = static_cast<SDL_GPUTexture*>(gpuDepthTexture_);
    auto* viewModelDepthTexture =
      static_cast<SDL_GPUTexture*>(gpuViewModelDepthTexture_);
    auto* msaaColorTexture =
      static_cast<SDL_GPUTexture*>(gpuMsaaColorTexture_);
    auto* sceneColorTexture =
      static_cast<SDL_GPUTexture*>(gpuSceneColorTexture_);
    auto* bloomTextureA =
      static_cast<SDL_GPUTexture*>(gpuBloomTextureA_);
    auto* bloomTextureB =
      static_cast<SDL_GPUTexture*>(gpuBloomTextureB_);
    auto* bloomDepthTexture =
      static_cast<SDL_GPUTexture*>(gpuBloomDepthTexture_);
    auto* outlineMaskTexture =
      static_cast<SDL_GPUTexture*>(gpuOutlineMaskTexture_);
    auto* outlineDilationTexture =
      static_cast<SDL_GPUTexture*>(gpuOutlineDilationTexture_);
    auto* outlineDepthTexture =
      static_cast<SDL_GPUTexture*>(gpuOutlineDepthTexture_);
    auto* sunShadowTexture =
      static_cast<SDL_GPUTexture*>(gpuSunShadowTexture_);
    auto* pointShadowTexture =
      static_cast<SDL_GPUTexture*>(gpuPointShadowTexture_);
    auto* staticWorld = static_cast<StaticWorldMesh*>(gpuStaticWorld_);
    const GltfSkinnedModel* requestedPlayerModel = &workerPlayerModel();
    auto* gltfPlayerResources =
      static_cast<GpuGltfPlayerResources*>(gpuGltfPlayerResources_);
    if (
      settings.playerModel > 0 &&
      requestedPlayerModel->loaded() &&
      (
        gltfPlayerResources == nullptr ||
        gltfPlayerResources->sourcePath != requestedPlayerModel->sourcePath()
      )
    ) {
      GpuGltfPlayerResources* replacement = createGpuGltfPlayerResources(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        *requestedPlayerModel
      );
      if (replacement != nullptr && !replacement->primitives.empty()) {
        destroyGpuGltfPlayerResources(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          gltfPlayerResources
        );
        gpuGltfPlayerResources_ = replacement;
        gltfPlayerResources = replacement;
      } else {
        destroyGpuGltfPlayerResources(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          replacement
        );
      }
    }
    auto* skyResources =
      static_cast<GpuSkyResources*>(gpuSkyResources_);
    if (
      skyResources != nullptr &&
      arena.skyId != SkyId::None &&
      (gpuPipelineSky_ != nullptr || gpuPipelineDirectSky_ != nullptr)
    ) {
      ensureSkyLoaded(gpuDevice, *skyResources, arena.skyId);
    }
    SDL_GPUTexture* activeSkyTexture = nullptr;
    SDL_GPUSampler* activeSkySampler = nullptr;
    const std::size_t skyIndex = static_cast<std::size_t>(arena.skyId);
    if (
      skyResources != nullptr &&
      skyIndex < skyResources->textures.size() &&
      skyResources->cache.state(arena.skyId) ==
        SkyAssetLoadState::Loaded
    ) {
      activeSkyTexture = skyResources->textures[skyIndex];
      activeSkySampler = skyResources->sampler;
    }
    const bool compatibilityOutlineResourcesAvailable =
      gpuPipelineOutlineClear_ != nullptr &&
      gpuPipelineOutlineColorClear_ != nullptr &&
      gpuPipelineOutlineMask_ != nullptr &&
      gpuPipelineStaticMeshOutlineMask_ != nullptr &&
      gpuPipelineMaterialMeshOutlineMask_ != nullptr &&
      gpuPipelineGltfPlayerModelOutlineMask_ != nullptr &&
      gpuPipelineOutlineDilation_ != nullptr &&
      gpuPipelineOutlineComposite_ != nullptr &&
      gpuOutlineMaskSampler_ != nullptr;
    const bool nativeOutlineResourcesAvailable =
      compatibilityOutlineResourcesAvailable &&
      gpuPipelineOutlineNativeDilation_ != nullptr &&
      gpuPipelineOutlineNativeComposite_ != nullptr;
    const PlayerOutlinePathPlan outlinePath = buildPlayerOutlinePathPlan(
      settings.playerOutlineMode,
      settings.playerOutlineStyle,
      true,
      nativeOutlineResourcesAvailable,
      compatibilityOutlineResourcesAvailable
    );
    const RenderSettings* effectiveSettings = &settings;
    RenderSettings fallbackSettings = {};
    if (
      outlinePath.mode != settings.playerOutlineMode ||
      outlinePath.style != settings.playerOutlineStyle
    ) {
      fallbackSettings = settings;
      fallbackSettings.playerOutlineMode = outlinePath.mode;
      fallbackSettings.playerOutlineStyle = outlinePath.style;
      effectiveSettings = &fallbackSettings;
    }
    if (!renderGpuFrame(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineWorldSurface_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline3D_),
          static_cast<SDL_GPUGraphicsPipeline*>(
            gpuPipeline3DTranslucent_
          ),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedMesh_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineStaticMesh_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineMaterialMesh_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineStaticMeshViewModel_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineMaterialMeshViewModel_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineGltfPlayerModel_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineGltfPlayerModelFlat_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectWorldSurface_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectWorld_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectInstancedMesh_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectStaticMesh_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectMaterialMesh_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectGltfPlayer_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDepthWorld_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDepthInstanced_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDepthStatic_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDepthMaterial_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDepthGltf_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedGlow_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineClear_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineColorClear_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineMask_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineStaticMeshOutlineMask_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineMaterialMeshOutlineMask_
        ),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineGltfPlayerModelOutlineMask_
        ),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineDilation_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineComposite_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineOutlineNativeDilation_
        ),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineNativeComposite_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowWorld_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowStatic_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowMaterial_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowGltf_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelinePointShadowWorld_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineBloomSource_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineBloomBlur_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSceneComposite_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineSceneCompositeNoBloom_
        ),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSky_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineDirectSky_),
          activeSkyTexture,
          activeSkySampler,
          static_cast<SDL_GPUBuffer*>(gpuVertexBuffer_),
          static_cast<SDL_GPUTransferBuffer*>(gpuTransferBuffer_),
          static_cast<GpuSimpleResources*>(gpuSimpleResources_),
          gltfPlayerResources,
          fontAtlasSet,
          static_cast<SDL_GPUSampler*>(gpuFontSampler_),
          static_cast<TextureAtlas*>(gpuWorldTextureAtlas_),
          staticWorld,
          depthTexture,
          viewModelDepthTexture,
          msaaColorTexture,
          sceneColorTexture,
          bloomTextureA,
          bloomTextureB,
          bloomDepthTexture,
          outlineMaskTexture,
          outlineDilationTexture,
          outlineDepthTexture,
          sunShadowTexture,
          static_cast<SDL_GPUTexture*>(gpuSunShadowFallbackTexture_),
          pointShadowTexture,
          static_cast<SDL_GPUTexture*>(gpuPointShadowFallbackTexture_),
          static_cast<SDL_GPUSampler*>(gpuOutlineMaskSampler_),
          static_cast<SDL_GPUSampler*>(gpuSunShadowSampler_),
          static_cast<SDL_GPUSampler*>(gpuPointShadowSampler_),
          static_cast<SDL_GPUSampler*>(gpuPostProcessSampler_),
          gpuDepthWidth_,
          gpuDepthHeight_,
          gpuViewModelDepthWidth_,
          gpuViewModelDepthHeight_,
          gpuMsaaColorWidth_,
          gpuMsaaColorHeight_,
          gpuSceneColorWidth_,
          gpuSceneColorHeight_,
          gpuBloomWidth_,
          gpuBloomHeight_,
          gpuBloomDepthWidth_,
          gpuBloomDepthHeight_,
          gpuOutlineMaskWidth_,
          gpuOutlineMaskHeight_,
          gpuOutlineDilationWidth_,
          gpuOutlineDilationHeight_,
          gpuOutlineDepthWidth_,
          gpuOutlineDepthHeight_,
          gpuSunShadowSize_,
          gpuSunShadowCacheKey_,
          gpuPointShadowSize_,
          gpuPointShadowLightCount_,
          gpuPointShadowCacheKey_,
          static_cast<SDL_GPUTextureFormat>(gpuDepthFormat_),
          static_cast<SDL_GPUTextureFormat>(gpuSceneColorFormat_),
          sampleCountFor(gpuSampleCount_),
          *static_cast<std::vector<GpuVertex>*>(gpuVertexScratch_),
          static_cast<SDL_Window*>(window_),
          arena,
          player,
          lateViewSampler,
          remotePlayers,
          localLightningGun,
          weaponFires,
          rocketExplosions,
          rockets,
          icePools,
          healthPickupAvailable,
          transientTracers,
          transientEffects,
          newExplosionEventsConsumed,
          *effectiveSettings,
          outlinePath.fallbackReason,
          hud,
          console,
          cameraStepOffset_,
          lastFrameDiagnostics_,
          gpuTimestampTiming_,
          captureRequest,
          captureResult
        ) &&
        !gpuErrorReported_) {
      std::cerr << "SDL_GPU frame submission failed: " << SDL_GetError() << '\n';
      gpuErrorReported_ = true;
    }
    gpuStaticWorld_ = staticWorld;
    gpuDepthTexture_ = depthTexture;
    gpuViewModelDepthTexture_ = viewModelDepthTexture;
    gpuMsaaColorTexture_ = msaaColorTexture;
    gpuSceneColorTexture_ = sceneColorTexture;
    gpuBloomTextureA_ = bloomTextureA;
    gpuBloomTextureB_ = bloomTextureB;
    gpuBloomDepthTexture_ = bloomDepthTexture;
    gpuOutlineMaskTexture_ = outlineMaskTexture;
    gpuOutlineDilationTexture_ = outlineDilationTexture;
    gpuOutlineDepthTexture_ = outlineDepthTexture;
    gpuSunShadowTexture_ = sunShadowTexture;
    gpuPointShadowTexture_ = pointShadowTexture;
    if (
      const GpuFrameTimingResult* timing =
        gpuTimestampTiming_.latestResult();
      timing != nullptr &&
      timing->outlineApplicable &&
      timing->outlineGpuMilliseconds.has_value()
    ) {
      // The diagnostics show the newest completed query. Benchmark data still
      // uses the stored frame id and never pairs results by arrival order.
      lastFrameDiagnostics_.outlineGpuTimingAvailable = true;
      lastFrameDiagnostics_.outlineGpuMilliseconds =
        static_cast<float>(*timing->outlineGpuMilliseconds);
    }
    lastFrameDiagnostics_.totalRenderMilliseconds =
      millisecondsBetween(renderStart, RenderClock::now());
    return;
  }

  const PlayerOutlinePathPlan sdlOutlinePath = buildPlayerOutlinePathPlan(
    settings.playerOutlineMode,
    settings.playerOutlineStyle,
    false,
    false,
    true
  );
  const RenderSettings* effectiveSdlSettings = &settings;
  RenderSettings sdlFallbackSettings = {};
  if (
    sdlOutlinePath.mode != settings.playerOutlineMode ||
    sdlOutlinePath.style != settings.playerOutlineStyle
  ) {
    sdlFallbackSettings = settings;
    sdlFallbackSettings.playerOutlineMode = sdlOutlinePath.mode;
    sdlFallbackSettings.playerOutlineStyle = sdlOutlinePath.style;
    effectiveSdlSettings = &sdlFallbackSettings;
  }
  lastFrameDiagnostics_.swapchainAcquireMilliseconds = 0.0F;
  lastFrameDiagnostics_.lateMouseSampleMilliseconds = 0.0F;
  lastFrameDiagnostics_.mouseSampleToSubmitMilliseconds = 0.0F;
  lastFrameDiagnostics_.mouseSamplePhaseGainMilliseconds = 0.0F;
  lastFrameDiagnostics_.lateMouseSampleEnabled =
    lateViewSampler.sample != nullptr;
  lastFrameDiagnostics_.lateMouseSampleApplied = false;
  lastFrameDiagnostics_.renderInstanceConstructionMilliseconds = 0.0F;
  lastFrameDiagnostics_.worldVisibilityMilliseconds = 0.0F;
  lastFrameDiagnostics_.worldCommandEncodingMilliseconds = 0.0F;
  lastFrameDiagnostics_.dynamicCommandEncodingMilliseconds = 0.0F;
  lastFrameDiagnostics_.uiMilliseconds = 0.0F;
  lastFrameDiagnostics_.sceneBuildMilliseconds = 0.0F;
  lastFrameDiagnostics_.gpuVertexUploadMilliseconds = 0.0F;
  lastFrameDiagnostics_.worldDrawIssueMilliseconds = 0.0F;
  lastFrameDiagnostics_.renderBuildUploadMilliseconds = 0.0F;
  lastFrameDiagnostics_.submitMilliseconds = 0.0F;
  lastFrameDiagnostics_.worldSourceTriangles = 0;
  lastFrameDiagnostics_.worldRenderedTriangles = 0;
  lastFrameDiagnostics_.worldSubmittedTriangles = 0;
  lastFrameDiagnostics_.worldDuplicateTrianglesCulled = 0;
  lastFrameDiagnostics_.worldVertexCount = 0;
  lastFrameDiagnostics_.worldDrawCalls = 0;
  lastFrameDiagnostics_.skyDrawCalls = 0;
  lastFrameDiagnostics_.skyLoadedTextures = 0;
  lastFrameDiagnostics_.worldSubmittedRanges = 0;
  lastFrameDiagnostics_.worldTotalChunks = 0;
  lastFrameDiagnostics_.worldVisibleChunks = 0;
  lastFrameDiagnostics_.worldCulledChunks = 0;
  lastFrameDiagnostics_.worldVisibilityTestedNodes = 0;
  lastFrameDiagnostics_.worldVisibilityQueryMilliseconds = 0.0F;
  lastFrameDiagnostics_.gpuDepthBits = 0;
  lastFrameDiagnostics_.worldLoadedTextures = 0;
  lastFrameDiagnostics_.worldMissingTextures = 0;
  lastFrameDiagnostics_.worldReferencedMaterials = 0;
  lastFrameDiagnostics_.worldMaxTextureMipLevels = 0;
  lastFrameDiagnostics_.worldTextureFilter =
    normalizedTextureFilter(settings.textureFilter);
  lastFrameDiagnostics_.worldRequestedTextureAnisotropy =
    normalizedTextureAnisotropy(settings.textureAnisotropy);
  lastFrameDiagnostics_.worldAppliedTextureAnisotropy = 1;
  lastFrameDiagnostics_.worldTextureLodBias =
    normalizedTextureLodBias(settings.textureLodBias);
  lastFrameDiagnostics_.dynamicOpaqueVertices = 0;
  lastFrameDiagnostics_.dynamicTranslucentVertices = 0;
  lastFrameDiagnostics_.totalUploadedVertices = 0;
  lastFrameDiagnostics_.dynamicTriangles = 0;
  lastFrameDiagnostics_.normalPlayerBodyDynamicVertices = 0;
  lastFrameDiagnostics_.geometryOutlineDynamicVertices = 0;
  lastFrameDiagnostics_.outlinedPlayers = 0;
  lastFrameDiagnostics_.outlineStyle =
    static_cast<int>(effectiveSdlSettings->playerOutlineStyle);
  lastFrameDiagnostics_.outlineMaskWidth = 0;
  lastFrameDiagnostics_.outlineMaskHeight = 0;
  lastFrameDiagnostics_.outlineWorkWidth = 0;
  lastFrameDiagnostics_.outlineWorkHeight = 0;
  lastFrameDiagnostics_.outlineWorkScale = 0.0F;
  lastFrameDiagnostics_.outlineWorkRectX = 0;
  lastFrameDiagnostics_.outlineWorkRectY = 0;
  lastFrameDiagnostics_.outlineWorkRectWidth = 0;
  lastFrameDiagnostics_.outlineWorkRectHeight = 0;
  lastFrameDiagnostics_.outlineWorkAreaPercent = 0.0F;
  lastFrameDiagnostics_.outlineMaskDrawCalls = 0;
  lastFrameDiagnostics_.outlineDilationDrawCalls = 0;
  lastFrameDiagnostics_.outlineCompositeDrawCalls = 0;
  lastFrameDiagnostics_.outlineUploadBytes = 0;
  lastFrameDiagnostics_.outlineGpuTimingAvailable = false;
  lastFrameDiagnostics_.outlineGpuMilliseconds = 0.0F;
  lastFrameDiagnostics_.outlinePasses = 0;
  lastFrameDiagnostics_.outlineCompositeEnabled = false;
  lastFrameDiagnostics_.geometryOutlineFallbackUsed = false;
  lastFrameDiagnostics_.nativeOutlineFallbackReason =
    sdlOutlinePath.fallbackReason;
  lastFrameDiagnostics_.sceneColorWidth = 0;
  lastFrameDiagnostics_.sceneColorHeight = 0;
  lastFrameDiagnostics_.sceneColorFormat = 0;
  lastFrameDiagnostics_.sceneCompositePasses = 0;
  lastFrameDiagnostics_.bloomWidth = 0;
  lastFrameDiagnostics_.bloomHeight = 0;
  lastFrameDiagnostics_.bloomPasses = 0;
  lastFrameDiagnostics_.sceneCompositeEnabled = false;
  lastFrameDiagnostics_.bloomEnabled = false;
  lastFrameDiagnostics_.directPresentEligible = false;
  lastFrameDiagnostics_.directPresentUsed = false;
  lastFrameDiagnostics_.directPresentFallbackReason = "fallback-renderer";
  lastFrameDiagnostics_.directPresentFormat.clear();
  lastFrameDiagnostics_.visibleRemotePlayers = 0;
  lastFrameDiagnostics_.remoteBodyModelsBuilt = 0;
  lastFrameDiagnostics_.remoteWeaponModelsBuilt = 0;
  lastFrameDiagnostics_.playerOutlinesBuilt = 0;
  lastFrameDiagnostics_.remoteCandidates = 0;
  lastFrameDiagnostics_.remoteFrustumVisible = 0;
  lastFrameDiagnostics_.remoteFrustumCulled = 0;
  lastFrameDiagnostics_.remoteWeaponCandidates = 0;
  lastFrameDiagnostics_.remoteWeaponsFrustumCulled = 0;
  lastFrameDiagnostics_.remoteWeaponInstances = 0;
  lastFrameDiagnostics_.remoteWeaponInstanceUploadBytes = 0;
  lastFrameDiagnostics_.remoteWeaponBatches = 0;
  lastFrameDiagnostics_.remoteWeaponDrawCalls = 0;
  lastFrameDiagnostics_.legacyRemoteWeaponDynamicVertices = 0;
  lastFrameDiagnostics_.gltfPlayerModelInstances = 0;
  lastFrameDiagnostics_.gltfPlayerModelFrustumCulled = 0;
  lastFrameDiagnostics_.gltfStaticMeshGpuBytes = 0;
  lastFrameDiagnostics_.gltfStaticIndexGpuBytes = 0;
  lastFrameDiagnostics_.gltfMaterialTextureGpuBytes = 0;
  lastFrameDiagnostics_.gltfMaterialTextureMipLevels = 0;
  lastFrameDiagnostics_.gltfMaterialTextureBinds = 0;
  lastFrameDiagnostics_.gltfAuthoredMaterialTexturesReady = false;
  lastFrameDiagnostics_.gltfMaterialFallbackUsed = false;
  lastFrameDiagnostics_.gltfPoseUploadBytes = 0;
  lastFrameDiagnostics_.gltfBonePaletteEntriesUploaded = 0;
  lastFrameDiagnostics_.gltfRigidFallbackInstances = 0;
  lastFrameDiagnostics_.gltfGpuSkinnedInstances = 0;
  lastFrameDiagnostics_.gltfBodyBatches = 0;
  lastFrameDiagnostics_.gltfBodyDrawCalls = 0;
  lastFrameDiagnostics_.gltfShadowCasterInstances = 0;
  lastFrameDiagnostics_.gltfShadowCasterDrawCalls = 0;
  lastFrameDiagnostics_.gltfOutlineMaskBatches = 0;
  lastFrameDiagnostics_.gltfOutlineMaskDrawCalls = 0;
  lastFrameDiagnostics_.legacyCpuSkinnedGltfVertexUploadBytes = 0;
  lastFrameDiagnostics_.visibleProceduralBoxPlayers = 0;
  lastFrameDiagnostics_.culledProceduralBoxPlayers = 0;
  lastFrameDiagnostics_.playerBoxInstancesSubmitted = 0;
  lastFrameDiagnostics_.playerBoxInstanceUploadBytes = 0;
  lastFrameDiagnostics_.sharedCubeStaticGpuBytes = 0;
  lastFrameDiagnostics_.proceduralPlayerOpaqueBatches = 0;
  lastFrameDiagnostics_.proceduralPlayerOpaqueDrawCalls = 0;
  lastFrameDiagnostics_.proceduralPlayerOutlineMaskBatches = 0;
  lastFrameDiagnostics_.proceduralPlayerOutlineMaskDrawCalls = 0;
  lastFrameDiagnostics_.legacyCpuGeneratedPlayerVertices = 0;
  lastFrameDiagnostics_.legacyDynamicPlayerVertexUploadBytes = 0;
  lastFrameDiagnostics_.firstPersonViewModelDrawCalls = 0;
  lastFrameDiagnostics_.firstPersonViewModelDynamicVertices = 0;
  lastFrameDiagnostics_.projectilesActive = 0;
  lastFrameDiagnostics_.projectilesFrustumCulled = 0;
  lastFrameDiagnostics_.projectilesRendered = 0;
  lastFrameDiagnostics_.plasmaInstances = 0;
  lastFrameDiagnostics_.rocketInstances = 0;
  lastFrameDiagnostics_.grenadeInstances = 0;
  lastFrameDiagnostics_.projectileCoreInstances = 0;
  lastFrameDiagnostics_.projectileGlowInstances = 0;
  lastFrameDiagnostics_.opaqueProjectileBatches = 0;
  lastFrameDiagnostics_.additiveProjectileBatches = 0;
  lastFrameDiagnostics_.projectileInstanceUploadBytes = 0;
  lastFrameDiagnostics_.projectileMeshDrawCalls = 0;
  lastFrameDiagnostics_.projectileGlowDrawCalls = 0;
  lastFrameDiagnostics_.legacyProjectileDynamicVertices = 0;
  lastFrameDiagnostics_.activeTransientEffects = 0;
  lastFrameDiagnostics_.activeMachineGunTracers = 0;
  lastFrameDiagnostics_.activeShotgunTracers = 0;
  lastFrameDiagnostics_.activeExplosionEffects = 0;
  lastFrameDiagnostics_.newExplosionEventsConsumed = 0;
  lastFrameDiagnostics_.tracerCandidates = 0;
  lastFrameDiagnostics_.tracerFrustumCulled = 0;
  lastFrameDiagnostics_.tracerInstancesSubmitted = 0;
  lastFrameDiagnostics_.tracerInstanceUploadBytes = 0;
  lastFrameDiagnostics_.tracerBatches = 0;
  lastFrameDiagnostics_.tracerDrawCalls = 0;
  lastFrameDiagnostics_.explosionCandidates = 0;
  lastFrameDiagnostics_.explosionFrustumCulled = 0;
  lastFrameDiagnostics_.explosionInstancesSubmitted = 0;
  lastFrameDiagnostics_.explosionInstanceUploadBytes = 0;
  lastFrameDiagnostics_.explosionOpaqueBatches = 0;
  lastFrameDiagnostics_.explosionAdditiveBatches = 0;
  lastFrameDiagnostics_.explosionDrawCalls = 0;
  lastFrameDiagnostics_.legacyWireframeExplosionDraws = 0;
  lastFrameDiagnostics_.legacyMachineGunShotgunVisualDraws = 0;
  lastFrameDiagnostics_.activeTemporaryLights = 0;
  lastFrameDiagnostics_.authoredPointLights = 0;
  lastFrameDiagnostics_.pointLightCandidates = 0;
  lastFrameDiagnostics_.selectedPointLights = 0;
  lastFrameDiagnostics_.droppedPointLights = 0;
  lastFrameDiagnostics_.flickeringPointLights = 0;
  lastFrameDiagnostics_.shadowedPointLights = 0;
  lastFrameDiagnostics_.activeCasings = 0;
  lastFrameDiagnostics_.activeImpactParticles = 0;
  lastFrameDiagnostics_.activeBulletDecals = 0;
  lastFrameDiagnostics_.transparentEffectsSubmitted = 0;
  auto* renderer = static_cast<SDL_Renderer*>(renderer_);
  if (renderer == nullptr) {
    lastFrameDiagnostics_.totalRenderMilliseconds =
      millisecondsBetween(renderStart, RenderClock::now());
    return;
  }

  int width = 0;
  int height = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
  PlayerState sampledPlayer = player;
  const std::uint64_t lateSampleCompletedNanoseconds = sampleLateView(
    lateViewSampler,
    sampledPlayer,
    lastFrameDiagnostics_
  );

  SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
  SDL_RenderClear(renderer);

  const Scene3D perspectiveScene = buildPerspectiveScene(
    static_cast<float>(width) / static_cast<float>(std::max(1, height)),
    arena,
    sampledPlayer,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    healthPickupAvailable,
    transientTracers,
    transientEffects,
    icePools,
    *effectiveSdlSettings,
    cameraStepOffset_
  );
  lastFrameDiagnostics_.totalUploadedVertices =
    static_cast<std::uint32_t>(
      perspectiveScene.vertices.size() +
      perspectiveScene.translucentVertices.size()
    );
  lastFrameDiagnostics_.remoteCandidates = perspectiveScene.remoteCandidates;
  lastFrameDiagnostics_.remoteFrustumVisible =
    perspectiveScene.remoteFrustumVisible;
  lastFrameDiagnostics_.remoteFrustumCulled =
    perspectiveScene.remoteFrustumCulled;
  lastFrameDiagnostics_.remoteWeaponCandidates =
    perspectiveScene.remoteWeaponStats.candidates;
  lastFrameDiagnostics_.remoteWeaponsFrustumCulled =
    perspectiveScene.remoteWeaponStats.frustumCulled;
  lastFrameDiagnostics_.remoteWeaponInstances =
    perspectiveScene.remoteWeaponStats.instancesSubmitted;
  lastFrameDiagnostics_.remoteWeaponInstanceUploadBytes =
    perspectiveScene.remoteWeaponStats.instanceUploadBytes;
  lastFrameDiagnostics_.remoteWeaponBatches =
    perspectiveScene.remoteWeaponStats.batches;
  lastFrameDiagnostics_.remoteWeaponDrawCalls =
    perspectiveScene.remoteWeaponStats.drawCalls;
  lastFrameDiagnostics_.legacyRemoteWeaponDynamicVertices =
    perspectiveScene.remoteWeaponStats.legacyDynamicVertices;
  lastFrameDiagnostics_.visibleProceduralBoxPlayers =
    perspectiveScene.playerBoxStats.visiblePlayers;
  lastFrameDiagnostics_.culledProceduralBoxPlayers =
    perspectiveScene.playerBoxStats.culledPlayers;
  lastFrameDiagnostics_.playerBoxInstancesSubmitted =
    perspectiveScene.playerBoxStats.instancesSubmitted;
  lastFrameDiagnostics_.playerBoxInstanceUploadBytes =
    perspectiveScene.playerBoxStats.instanceUploadBytes;
  lastFrameDiagnostics_.sharedCubeStaticGpuBytes =
    perspectiveScene.playerBoxStats.sharedCubeStaticGpuBytes;
  lastFrameDiagnostics_.proceduralPlayerOpaqueBatches =
    perspectiveScene.playerBoxStats.opaqueBatches;
  lastFrameDiagnostics_.proceduralPlayerOpaqueDrawCalls =
    perspectiveScene.playerBoxStats.opaqueDrawCalls;
  lastFrameDiagnostics_.proceduralPlayerOutlineMaskBatches =
    perspectiveScene.playerBoxStats.outlineMaskBatches;
  lastFrameDiagnostics_.proceduralPlayerOutlineMaskDrawCalls =
    perspectiveScene.playerBoxStats.outlineMaskDrawCalls;
  lastFrameDiagnostics_.legacyCpuGeneratedPlayerVertices =
    perspectiveScene.playerBoxStats.legacyCpuGeneratedVertices;
  lastFrameDiagnostics_.legacyDynamicPlayerVertexUploadBytes =
    perspectiveScene.playerBoxStats.legacyDynamicVertexUploadBytes;
  copyGltfPlayerModelDiagnostics(lastFrameDiagnostics_, perspectiveScene);
  lastFrameDiagnostics_.firstPersonViewModelDrawCalls =
    perspectiveScene.viewModelStats.drawCalls;
  lastFrameDiagnostics_.firstPersonViewModelDynamicVertices =
    perspectiveScene.viewModelStats.dynamicVertices;
  lastFrameDiagnostics_.projectilesActive =
    perspectiveScene.projectileStats.projectilesActive;
  lastFrameDiagnostics_.projectilesFrustumCulled =
    perspectiveScene.projectileStats.projectilesFrustumCulled;
  lastFrameDiagnostics_.projectilesRendered =
    perspectiveScene.projectileStats.projectilesRendered;
  lastFrameDiagnostics_.plasmaInstances =
    perspectiveScene.projectileStats.plasmaInstances;
  lastFrameDiagnostics_.rocketInstances =
    perspectiveScene.projectileStats.rocketInstances;
  lastFrameDiagnostics_.grenadeInstances =
    perspectiveScene.projectileStats.grenadeInstances;
  lastFrameDiagnostics_.projectileCoreInstances =
    perspectiveScene.projectileStats.projectileCoreInstances;
  lastFrameDiagnostics_.projectileGlowInstances =
    perspectiveScene.projectileStats.projectileGlowInstances;
  lastFrameDiagnostics_.opaqueProjectileBatches =
    perspectiveScene.projectileStats.opaqueProjectileBatches;
  lastFrameDiagnostics_.additiveProjectileBatches =
    perspectiveScene.projectileStats.additiveProjectileBatches;
  lastFrameDiagnostics_.projectileInstanceUploadBytes =
    perspectiveScene.projectileStats.projectileInstanceUploadBytes;
  lastFrameDiagnostics_.projectileMeshDrawCalls =
    perspectiveScene.projectileStats.projectileMeshDrawCalls;
  lastFrameDiagnostics_.projectileGlowDrawCalls =
    perspectiveScene.projectileStats.projectileGlowDrawCalls;
  lastFrameDiagnostics_.legacyProjectileDynamicVertices =
    perspectiveScene.projectileStats.legacyProjectileDynamicVertices;
  lastFrameDiagnostics_.activeTransientEffects =
    perspectiveScene.transientVfxStats.activeEffects;
  lastFrameDiagnostics_.activeMachineGunTracers =
    perspectiveScene.transientVfxStats.activeMachineGunTracers;
  lastFrameDiagnostics_.activeShotgunTracers =
    perspectiveScene.transientVfxStats.activeShotgunTracers;
  lastFrameDiagnostics_.activeExplosionEffects =
    perspectiveScene.transientVfxStats.activeExplosionEffects;
  lastFrameDiagnostics_.newExplosionEventsConsumed = newExplosionEventsConsumed;
  lastFrameDiagnostics_.tracerCandidates =
    perspectiveScene.transientVfxStats.tracerCandidates;
  lastFrameDiagnostics_.tracerFrustumCulled =
    perspectiveScene.transientVfxStats.tracerFrustumCulled;
  lastFrameDiagnostics_.tracerInstancesSubmitted =
    perspectiveScene.transientVfxStats.tracerInstancesSubmitted;
  lastFrameDiagnostics_.tracerInstanceUploadBytes =
    perspectiveScene.transientVfxStats.tracerInstanceUploadBytes;
  lastFrameDiagnostics_.tracerBatches =
    perspectiveScene.transientVfxStats.tracerBatches;
  lastFrameDiagnostics_.tracerDrawCalls =
    perspectiveScene.transientVfxStats.tracerDrawCalls;
  lastFrameDiagnostics_.explosionCandidates =
    perspectiveScene.transientVfxStats.explosionCandidates;
  lastFrameDiagnostics_.explosionFrustumCulled =
    perspectiveScene.transientVfxStats.explosionFrustumCulled;
  lastFrameDiagnostics_.explosionInstancesSubmitted =
    perspectiveScene.transientVfxStats.explosionInstancesSubmitted;
  lastFrameDiagnostics_.explosionInstanceUploadBytes =
    perspectiveScene.transientVfxStats.explosionInstanceUploadBytes;
  lastFrameDiagnostics_.explosionOpaqueBatches =
    perspectiveScene.transientVfxStats.explosionOpaqueBatches;
  lastFrameDiagnostics_.explosionAdditiveBatches =
    perspectiveScene.transientVfxStats.explosionAdditiveBatches;
  lastFrameDiagnostics_.explosionDrawCalls =
    perspectiveScene.transientVfxStats.explosionDrawCalls;
  lastFrameDiagnostics_.legacyWireframeExplosionDraws =
    perspectiveScene.transientVfxStats.legacyWireframeExplosionDraws;
  lastFrameDiagnostics_.legacyMachineGunShotgunVisualDraws = 0;
  lastFrameDiagnostics_.activeTemporaryLights =
    perspectiveScene.transientVfxStats.activeTemporaryLights;
  lastFrameDiagnostics_.authoredPointLights =
    perspectiveScene.pointLightStats.authored;
  lastFrameDiagnostics_.pointLightCandidates =
    perspectiveScene.pointLightStats.candidates;
  lastFrameDiagnostics_.selectedPointLights =
    perspectiveScene.pointLightStats.selected;
  lastFrameDiagnostics_.droppedPointLights =
    perspectiveScene.pointLightStats.dropped;
  lastFrameDiagnostics_.flickeringPointLights =
    perspectiveScene.pointLightStats.flickering;
  lastFrameDiagnostics_.shadowedPointLights = 0;
  lastFrameDiagnostics_.activeCasings =
    perspectiveScene.transientVfxStats.activeCasings;
  lastFrameDiagnostics_.activeImpactParticles =
    perspectiveScene.transientVfxStats.activeImpactParticles;
  lastFrameDiagnostics_.activeBulletDecals =
    perspectiveScene.transientVfxStats.activeBulletDecals;
  lastFrameDiagnostics_.transparentEffectsSubmitted =
    perspectiveScene.transientVfxStats.transparentEffectsSubmitted;
  lastFrameDiagnostics_.remoteBodyModelsBuilt =
    perspectiveScene.remoteBodyModelsBuilt;
  lastFrameDiagnostics_.remoteWeaponModelsBuilt =
    perspectiveScene.remoteWeaponModelsBuilt;
  lastFrameDiagnostics_.playerOutlinesBuilt =
    perspectiveScene.playerOutlinesBuilt;
  lastFrameDiagnostics_.normalPlayerBodyDynamicVertices =
    perspectiveScene.normalPlayerBodyDynamicVertices;
  lastFrameDiagnostics_.geometryOutlineDynamicVertices =
    perspectiveScene.geometryOutlineDynamicVertices;
  lastFrameDiagnostics_.outlinedPlayers = perspectiveScene.outlinedPlayers;
  lastFrameDiagnostics_.geometryOutlineFallbackUsed =
    perspectiveScene.geometryOutlineFallbackUsed;
  lastFrameDiagnostics_.plasmaInstances = 0;
  lastFrameDiagnostics_.rocketInstances = 0;
  lastFrameDiagnostics_.grenadeInstances = 0;
  lastFrameDiagnostics_.projectileCoreInstances = 0;
  lastFrameDiagnostics_.projectileGlowInstances = 0;
  lastFrameDiagnostics_.opaqueProjectileBatches = 0;
  lastFrameDiagnostics_.additiveProjectileBatches = 0;
  lastFrameDiagnostics_.projectileInstanceUploadBytes = 0;
  lastFrameDiagnostics_.projectileMeshDrawCalls = 0;
  lastFrameDiagnostics_.projectileGlowDrawCalls = 0;
  lastFrameDiagnostics_.legacyProjectileDynamicVertices =
    lastFrameDiagnostics_.projectilesRendered * 24U;
  lastFrameDiagnostics_.tracerInstancesSubmitted = 0;
  lastFrameDiagnostics_.tracerInstanceUploadBytes = 0;
  lastFrameDiagnostics_.tracerBatches = 0;
  lastFrameDiagnostics_.tracerDrawCalls = 0;
  drawPerspectiveWorld(
    renderer,
    width,
    height,
    arena,
    sampledPlayer,
    remotePlayers,
    perspectiveScene.remoteRenderVisible,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    healthPickupAvailable,
    *effectiveSdlSettings
  );
  const PerspectiveCamera camera = playerPerspectiveCamera(
    sampledPlayer,
    static_cast<float>(width) / static_cast<float>(std::max(1, height)),
    settings.fieldOfView
  );
  if (captureRequest == nullptr || !captureRequest->hideHud) {
    drawCommandList(
      renderer,
      buildFloatingHealthBars(
        width,
        height,
        camera,
        arena,
        remotePlayers,
        perspectiveScene.remoteRenderVisible,
        settings,
        hud
      )
    );
    drawCommandList(
      renderer,
      buildFloatingDamageNumbers(width, height, camera, settings, hud)
    );
    drawCommandList(
      renderer,
      buildScreenUi(
        width,
        height,
        player,
        settings,
        hud,
        console,
        &camera
      )
    );
  }
  if (captureRequest != nullptr && captureResult != nullptr) {
    captureResult->requested = true;
    captureResult->path = captureRequest->path;
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr) {
      captureResult->error = std::string("SDL_Renderer capture failed: ") + SDL_GetError();
    } else {
      SDL_Surface* rgbaSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
      SDL_DestroySurface(surface);
      if (rgbaSurface == nullptr) {
        captureResult->error = std::string("capture pixel conversion failed: ") + SDL_GetError();
      } else {
        captureResult->width = static_cast<std::uint32_t>(rgbaSurface->w);
        captureResult->height = static_cast<std::uint32_t>(rgbaSurface->h);
        std::vector<std::uint8_t> pixels(
          static_cast<std::size_t>(rgbaSurface->w) * rgbaSurface->h * 4U
        );
        const auto* source = static_cast<const std::uint8_t*>(rgbaSurface->pixels);
        for (int row = 0; row < rgbaSurface->h; ++row) {
          std::memcpy(
            pixels.data() + static_cast<std::size_t>(row) * rgbaSurface->w * 4U,
            source + static_cast<std::size_t>(row) * rgbaSurface->pitch,
            static_cast<std::size_t>(rgbaSurface->w) * 4U
          );
        }
        SDL_DestroySurface(rgbaSurface);
        captureResult->ok = dev::writeRgbaPng(
          captureRequest->path,
          captureResult->width,
          captureResult->height,
          pixels,
          captureResult->error
        );
      }
    }
  }
  recordMouseSampleToSubmit(
    lateSampleCompletedNanoseconds,
    lastFrameDiagnostics_
  );
  SDL_RenderPresent(renderer);
  lastFrameDiagnostics_.totalRenderMilliseconds =
    millisecondsBetween(renderStart, RenderClock::now());
#else
  (void)arena;
  (void)player;
  (void)remotePlayers;
  (void)localLightningGun;
  (void)settings;
  (void)hud;
  (void)console;
  (void)lateViewSampler;
  (void)captureRequest;
  (void)captureResult;
#endif
}

bool Renderer::setVSync(bool enabled) {
  return setPresentMode(enabled ? PresentMode::Fifo : PresentMode::Immediate);
}

bool Renderer::setPresentMode(PresentMode mode) {
#if LG_DUEL_HAS_SDL3
  if (gpuBackend_) {
    auto* device = static_cast<SDL_GPUDevice*>(gpuDevice_);
    auto* window = static_cast<SDL_Window*>(window_);
    SDL_GPUPresentMode presentMode = sdlGpuPresentMode(mode);
    if (
      presentMode != SDL_GPU_PRESENTMODE_VSYNC &&
      !SDL_WindowSupportsGPUPresentMode(device, window, presentMode)
    ) {
      // FIFO is the portability fallback guaranteed by the GPU path; requested
      // low-latency modes may not exist on every backend or display surface.
      std::cerr
        << "Requested present mode " << presentModeName(mode)
        << " is not supported by this SDL_GPU backend; falling back to FIFO/VSync.\n";
      presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    }
    const bool changed = SDL_SetGPUSwapchainParameters(
      device,
      window,
      SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
      presentMode
    );
    if (changed) {
      lastFrameDiagnostics_.selectedPresentModeName =
        presentModeName(presentMode);
    }
    return changed;
  }

  auto* renderer = static_cast<SDL_Renderer*>(renderer_);
  const bool enableVSync = mode != PresentMode::Immediate;
  const bool changed = renderer != nullptr &&
    SDL_SetRenderVSync(renderer, enableVSync ? 1 : SDL_RENDERER_VSYNC_DISABLED);
  if (changed) {
    lastFrameDiagnostics_.selectedPresentModeName =
      enableVSync ? "SDL_Renderer VSync" : "SDL_Renderer Immediate";
    if (mode == PresentMode::Mailbox) {
      std::cerr
        << "Requested present mode Mailbox is not available with SDL_Renderer; "
        << "falling back to renderer VSync.\n";
    }
  }
  return changed;
#else
  (void)mode;
  return false;
#endif
}

std::string_view Renderer::backendName() const {
  return backendName_;
}

std::string_view Renderer::requestedBackendName() const {
  return requestedBackendName_;
}

std::string_view Renderer::gpuName() const {
  return gpuName_;
}

std::string_view Renderer::graphicsDriverName() const {
  return graphicsDriverName_;
}

std::string_view Renderer::graphicsDriverVersion() const {
  return graphicsDriverVersion_;
}

std::string_view Renderer::graphicsDriverInfo() const {
  return graphicsDriverInfo_;
}

std::string_view Renderer::vulkanApiVersion() const {
  return vulkanApiVersion_;
}

std::string_view Renderer::vulkanIcdPath() const {
  return vulkanIcdPath_;
}

std::string_view Renderer::vulkanIcdSha256() const {
  return vulkanIcdSha256_;
}

bool Renderer::softwareRenderer() const {
  return softwareRenderer_;
}

const RendererFrameDiagnostics& Renderer::lastFrameDiagnostics() const {
  return lastFrameDiagnostics_;
}

void Renderer::resetGpuTimingResults() {
  gpuTimestampTiming_.resetResults();
}

std::span<const GpuFrameTimingResult> Renderer::takeGpuTimingResults() {
  return gpuTimestampTiming_.takeResults();
}

void Renderer::drainGpuTimings() {
  gpuTimestampTiming_.drain(gpuDevice_);
}

bool Renderer::hasPendingGpuTimings() const {
  return gpuTimestampTiming_.hasPending();
}

const GpuTimingAvailability& Renderer::gpuTimingMetadata() const {
  return gpuTimestampTiming_.metadata();
}

void Renderer::shutdown() {
#if LG_DUEL_HAS_SDL3
  if (gpuDevice_ != nullptr) {
    // Shutdown may wait; render and poll paths never do.
    gpuTimestampTiming_.shutdown(gpuDevice_);
    delete static_cast<std::vector<GpuVertex>*>(gpuVertexScratch_);
    gpuVertexScratch_ = nullptr;
    if (gpuDepthTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuDepthTexture_)
      );
      gpuDepthTexture_ = nullptr;
      gpuDepthWidth_ = 0;
      gpuDepthHeight_ = 0;
    }
    if (gpuViewModelDepthTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuViewModelDepthTexture_)
      );
      gpuViewModelDepthTexture_ = nullptr;
      gpuViewModelDepthWidth_ = 0;
      gpuViewModelDepthHeight_ = 0;
    }
    if (gpuMsaaColorTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuMsaaColorTexture_)
      );
      gpuMsaaColorTexture_ = nullptr;
      gpuMsaaColorWidth_ = 0;
      gpuMsaaColorHeight_ = 0;
    }
    for (void** texture : {
           &gpuSceneColorTexture_,
           &gpuBloomTextureA_,
           &gpuBloomTextureB_,
           &gpuBloomDepthTexture_,
         }) {
      if (*texture != nullptr) {
        SDL_ReleaseGPUTexture(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUTexture*>(*texture)
        );
        *texture = nullptr;
      }
    }
    gpuSceneColorWidth_ = 0;
    gpuSceneColorHeight_ = 0;
    gpuBloomWidth_ = 0;
    gpuBloomHeight_ = 0;
    gpuBloomDepthWidth_ = 0;
    gpuBloomDepthHeight_ = 0;
    gpuSceneColorFormat_ = 0;
    if (gpuSunShadowTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuSunShadowTexture_)
      );
      gpuSunShadowTexture_ = nullptr;
      gpuSunShadowSize_ = 0;
      gpuSunShadowCacheKey_ = 0;
    }
    if (gpuSunShadowFallbackTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuSunShadowFallbackTexture_)
      );
      gpuSunShadowFallbackTexture_ = nullptr;
    }
    if (gpuPointShadowTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuPointShadowTexture_)
      );
      gpuPointShadowTexture_ = nullptr;
      gpuPointShadowSize_ = 0;
      gpuPointShadowLightCount_ = 0;
      gpuPointShadowCacheKey_ = 0;
    }
    if (gpuPointShadowFallbackTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuPointShadowFallbackTexture_)
      );
      gpuPointShadowFallbackTexture_ = nullptr;
    }
    gpuDepthFormat_ = 0;
    if (gpuOutlineMaskTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuOutlineMaskTexture_)
      );
      gpuOutlineMaskTexture_ = nullptr;
      gpuOutlineMaskWidth_ = 0;
      gpuOutlineMaskHeight_ = 0;
    }
    if (gpuOutlineDilationTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuOutlineDilationTexture_)
      );
      gpuOutlineDilationTexture_ = nullptr;
      gpuOutlineDilationWidth_ = 0;
      gpuOutlineDilationHeight_ = 0;
    }
    if (gpuOutlineDepthTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuOutlineDepthTexture_)
      );
      gpuOutlineDepthTexture_ = nullptr;
      gpuOutlineDepthWidth_ = 0;
      gpuOutlineDepthHeight_ = 0;
    }
    destroyStaticWorldMesh(
      static_cast<SDL_GPUDevice*>(gpuDevice_),
      static_cast<StaticWorldMesh*>(gpuStaticWorld_)
    );
    gpuStaticWorld_ = nullptr;
    if (gpuFontSampler_ != nullptr) {
      SDL_ReleaseGPUSampler(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUSampler*>(gpuFontSampler_)
      );
      gpuFontSampler_ = nullptr;
    }
    if (gpuOutlineMaskSampler_ != nullptr) {
      SDL_ReleaseGPUSampler(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUSampler*>(gpuOutlineMaskSampler_)
      );
      gpuOutlineMaskSampler_ = nullptr;
    }
    if (gpuPostProcessSampler_ != nullptr) {
      SDL_ReleaseGPUSampler(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUSampler*>(gpuPostProcessSampler_)
      );
      gpuPostProcessSampler_ = nullptr;
    }
    if (gpuSunShadowSampler_ != nullptr) {
      SDL_ReleaseGPUSampler(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUSampler*>(gpuSunShadowSampler_)
      );
      gpuSunShadowSampler_ = nullptr;
    }
    if (gpuPointShadowSampler_ != nullptr) {
      SDL_ReleaseGPUSampler(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUSampler*>(gpuPointShadowSampler_)
      );
      gpuPointShadowSampler_ = nullptr;
    }
    if (gpuFontAtlas_ != nullptr) {
      destroyFontAtlasSet(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<FontAtlasSet*>(gpuFontAtlas_)
      );
      gpuFontAtlas_ = nullptr;
    }
    destroyTextureAtlas(
      static_cast<SDL_GPUDevice*>(gpuDevice_),
      static_cast<TextureAtlas*>(gpuWorldTextureAtlas_)
    );
    gpuWorldTextureAtlas_ = nullptr;
    destroyGpuSimpleResources(
      static_cast<SDL_GPUDevice*>(gpuDevice_),
      static_cast<GpuSimpleResources*>(gpuSimpleResources_)
    );
    gpuSimpleResources_ = nullptr;
    destroyGpuSkyResources(
      static_cast<SDL_GPUDevice*>(gpuDevice_),
      static_cast<GpuSkyResources*>(gpuSkyResources_)
    );
    gpuSkyResources_ = nullptr;
    destroyGpuGltfPlayerResources(
      static_cast<SDL_GPUDevice*>(gpuDevice_),
      static_cast<GpuGltfPlayerResources*>(gpuGltfPlayerResources_)
    );
    gpuGltfPlayerResources_ = nullptr;
    if (gpuTransferBuffer_ != nullptr) {
      SDL_ReleaseGPUTransferBuffer(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTransferBuffer*>(gpuTransferBuffer_)
      );
      gpuTransferBuffer_ = nullptr;
    }
    if (gpuVertexBuffer_ != nullptr) {
      SDL_ReleaseGPUBuffer(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUBuffer*>(gpuVertexBuffer_)
      );
      gpuVertexBuffer_ = nullptr;
    }
    if (gpuPipeline_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline_)
      );
      gpuPipeline_ = nullptr;
    }
    if (gpuPipeline3D_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline3D_)
      );
      gpuPipeline3D_ = nullptr;
    }
    if (gpuPipelineWorldSurface_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineWorldSurface_)
      );
      gpuPipelineWorldSurface_ = nullptr;
    }
    if (gpuPipeline3DTranslucent_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipeline3DTranslucent_
        )
      );
      gpuPipeline3DTranslucent_ = nullptr;
    }
    if (gpuPipelineInstancedMesh_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedMesh_)
      );
      gpuPipelineInstancedMesh_ = nullptr;
    }
    if (gpuPipelineStaticMesh_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineStaticMesh_)
      );
      gpuPipelineStaticMesh_ = nullptr;
    }
    if (gpuPipelineMaterialMesh_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineMaterialMesh_)
      );
      gpuPipelineMaterialMesh_ = nullptr;
    }
    if (gpuPipelineStaticMeshViewModel_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineStaticMeshViewModel_
        )
      );
      gpuPipelineStaticMeshViewModel_ = nullptr;
    }
    if (gpuPipelineMaterialMeshViewModel_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineMaterialMeshViewModel_
        )
      );
      gpuPipelineMaterialMeshViewModel_ = nullptr;
    }
    if (gpuPipelineSunShadowWorld_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowWorld_)
      );
      gpuPipelineSunShadowWorld_ = nullptr;
    }
    if (gpuPipelineSunShadowStatic_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowStatic_)
      );
      gpuPipelineSunShadowStatic_ = nullptr;
    }
    if (gpuPipelineSunShadowMaterial_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowMaterial_)
      );
      gpuPipelineSunShadowMaterial_ = nullptr;
    }
    if (gpuPipelineSunShadowGltf_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineSunShadowGltf_)
      );
      gpuPipelineSunShadowGltf_ = nullptr;
    }
    if (gpuPipelinePointShadowWorld_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelinePointShadowWorld_)
      );
      gpuPipelinePointShadowWorld_ = nullptr;
    }
    if (gpuPipelineGltfPlayerModel_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineGltfPlayerModel_)
      );
      gpuPipelineGltfPlayerModel_ = nullptr;
    }
    if (gpuPipelineGltfPlayerModelFlat_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineGltfPlayerModelFlat_)
      );
      gpuPipelineGltfPlayerModelFlat_ = nullptr;
    }
    for (void** pipeline : {
           &gpuPipelineDirectWorldSurface_,
           &gpuPipelineDirectWorld_,
           &gpuPipelineDirectInstancedMesh_,
           &gpuPipelineDirectStaticMesh_,
           &gpuPipelineDirectMaterialMesh_,
           &gpuPipelineDirectGltfPlayer_,
           &gpuPipelineDirectSky_,
         }) {
      if (*pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUGraphicsPipeline*>(*pipeline)
        );
        *pipeline = nullptr;
      }
    }
    for (void** pipeline : {
           &gpuPipelineDepthWorld_,
           &gpuPipelineDepthInstanced_,
           &gpuPipelineDepthStatic_,
           &gpuPipelineDepthMaterial_,
           &gpuPipelineDepthGltf_,
         }) {
      if (*pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUGraphicsPipeline*>(*pipeline)
        );
        *pipeline = nullptr;
      }
    }
    if (gpuPipelineInstancedGlow_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedGlow_)
      );
      gpuPipelineInstancedGlow_ = nullptr;
    }
    for (void** pipeline : {
           &gpuPipelineBloomSource_,
           &gpuPipelineBloomBlur_,
           &gpuPipelineSceneComposite_,
           &gpuPipelineSceneCompositeNoBloom_,
           &gpuPipelineSky_,
         }) {
      if (*pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUGraphicsPipeline*>(*pipeline)
        );
        *pipeline = nullptr;
      }
    }
    if (gpuPipelineOutlineClear_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineClear_)
      );
      gpuPipelineOutlineClear_ = nullptr;
    }
    if (gpuPipelineOutlineColorClear_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineColorClear_)
      );
      gpuPipelineOutlineColorClear_ = nullptr;
    }
    if (gpuPipelineOutlineMask_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineMask_)
      );
      gpuPipelineOutlineMask_ = nullptr;
    }
    if (gpuPipelineStaticMeshOutlineMask_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineStaticMeshOutlineMask_)
      );
      gpuPipelineStaticMeshOutlineMask_ = nullptr;
    }
    if (gpuPipelineMaterialMeshOutlineMask_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(
          gpuPipelineMaterialMeshOutlineMask_
        )
      );
      gpuPipelineMaterialMeshOutlineMask_ = nullptr;
    }
    if (gpuPipelineGltfPlayerModelOutlineMask_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineGltfPlayerModelOutlineMask_)
      );
      gpuPipelineGltfPlayerModelOutlineMask_ = nullptr;
    }
    if (gpuPipelineOutlineDilation_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineDilation_)
      );
      gpuPipelineOutlineDilation_ = nullptr;
    }
    if (gpuPipelineOutlineComposite_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineComposite_)
      );
      gpuPipelineOutlineComposite_ = nullptr;
    }
    if (gpuPipelineOutlineNativeDilation_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineNativeDilation_)
      );
      gpuPipelineOutlineNativeDilation_ = nullptr;
    }
    if (gpuPipelineOutlineNativeComposite_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineOutlineNativeComposite_)
      );
      gpuPipelineOutlineNativeComposite_ = nullptr;
    }
    SDL_ReleaseWindowFromGPUDevice(
      static_cast<SDL_GPUDevice*>(gpuDevice_),
      static_cast<SDL_Window*>(window_)
    );
    SDL_DestroyGPUDevice(static_cast<SDL_GPUDevice*>(gpuDevice_));
    gpuDevice_ = nullptr;
  }
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
    renderer_ = nullptr;
  }
#endif
  window_ = nullptr;
  backendName_ = "uninitialized";
  requestedBackendName_ = "fallback";
  gpuName_.clear();
  graphicsDriverName_.clear();
  graphicsDriverVersion_.clear();
  graphicsDriverInfo_.clear();
  vulkanApiVersion_.clear();
  vulkanIcdPath_.clear();
  vulkanIcdSha256_.clear();
  gpuBackend_ = false;
  softwareRenderer_ = false;
  gpuErrorReported_ = false;
}

} // namespace lg
