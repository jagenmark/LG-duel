#include "render/Renderer.hpp"
#include "render/BitmapFont.hpp"
#include "render/Scene3D.hpp"
#include "render/ScreenUi.hpp"
#include "render/Perspective.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#include "render/BitmapFontData.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
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

  SDL_GPUDevice* device =
    SDL_CreateGPUDevice(shaderFormats, debugMode, "vulkan");
  if (device != nullptr) {
    return device;
  }

  const std::string vulkanError = SDL_GetError();
  device = SDL_CreateGPUDevice(shaderFormats, debugMode, nullptr);
  if (device == nullptr) {
    std::cerr
      << "SDL_GPU Vulkan initialization failed: " << vulkanError << '\n'
      << "SDL_GPU automatic initialization failed: " << SDL_GetError() << '\n';
  }
  return device;
}

constexpr std::size_t kMaxGpuVertices = 131072;

using RenderClock = std::chrono::steady_clock;

[[nodiscard]] float millisecondsBetween(
  RenderClock::time_point start,
  RenderClock::time_point end
) {
  return std::chrono::duration<float, std::milli>(end - start).count();
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
};

constexpr Uint32 kFontAtlasWidth = 128;
constexpr Uint32 kFontAtlasHeight = 128;
constexpr float kSolidTextureU = 4.0F / static_cast<float>(kFontAtlasWidth);
constexpr float kSolidTextureV = 4.0F / static_cast<float>(kFontAtlasHeight);

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

struct WorldTexture {
  SDL_GPUTexture* texture = nullptr;
  int width = 1;
  int height = 1;
  std::string material;
  bool fallback = false;
};

struct StaticWorldBatch {
  std::uint32_t materialId = 0;
  Uint32 firstVertex = 0;
  Uint32 vertexCount = 0;
  WorldTexture* texture = nullptr;
};

struct StaticWorldMesh {
  SDL_GPUBuffer* vertexBuffer = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  std::vector<WorldTexture> textures;
  std::vector<StaticWorldBatch> batches;
  std::uint64_t arenaFingerprint = 0;
  std::uint32_t sourceTriangles = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t referencedMaterials = 0;
  std::uint32_t loadedTextures = 0;
  std::uint32_t missingTextures = 0;
  std::uint32_t buildCount = 0;
  float buildMilliseconds = 0.0F;
};

struct GpuStaticMesh {
  SDL_GPUBuffer* vertexBuffer = nullptr;
  Uint32 vertexCount = 0;
  MeshHandle handle = MeshHandle::Invalid;
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
// location 6: instanceColor ubyte4 normalized, offset 28
// location 7: instancePhase float, offset 32
struct GpuSimpleInstance {
  float position[3] = {};
  float scale[3] = {1.0F, 1.0F, 1.0F};
  float rotationRadians = 0.0F;
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
  float visualPhase = 0.0F;
};

static_assert(sizeof(GpuSimpleInstance) == 36);

struct GpuInstanceBuffer {
  SDL_GPUBuffer* buffer = nullptr;
  SDL_GPUTransferBuffer* transfer = nullptr;
  Uint32 capacity = 0;
  std::vector<GpuSimpleInstance> staging;
};

struct GpuSimpleResources {
  GpuStaticMesh plasmaCore;
  GpuBillboardMesh plasmaGlow;
  GpuInstanceBuffer instances;
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

void destroyGpuSimpleResources(SDL_GPUDevice* device, GpuSimpleResources* resources) {
  if (resources == nullptr) {
    return;
  }
  if (resources->plasmaCore.vertexBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, resources->plasmaCore.vertexBuffer);
  }
  if (resources->plasmaGlow.vertexBuffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, resources->plasmaGlow.vertexBuffer);
  }
  destroyGpuInstanceBuffer(device, resources->instances);
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
  hash = hashCombine(hash, arena.staticLightCount);
  const auto hashFloat = [](float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<std::uint64_t>(bits);
  };
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    hash = hashCombine(hash, hashFloat(wall.min.x));
    hash = hashCombine(hash, hashFloat(wall.min.y));
    hash = hashCombine(hash, hashFloat(wall.min.z));
    hash = hashCombine(hash, hashFloat(wall.max.x));
    hash = hashCombine(hash, hashFloat(wall.max.y));
    hash = hashCombine(hash, hashFloat(wall.max.z));
    hash = hashCombine(hash, wall.materialId);
    for (std::size_t faceIndex = 0; faceIndex < wall.faceMaterialIds.size(); ++faceIndex) {
      hash = hashCombine(hash, wall.faceMaterialIds[faceIndex]);
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
    hash = hashCombine(hash, brush.faceCount);
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      hash = hashCombine(hash, face.materialId);
      hash = hashCombine(hash, face.vertexCount);
      for (std::uint8_t vertexIndex = 0; vertexIndex < face.vertexCount; ++vertexIndex) {
        const Vec3 vertex = brush.vertices[face.vertices[vertexIndex]];
        hash = hashCombine(hash, hashFloat(vertex.x));
        hash = hashCombine(hash, hashFloat(vertex.y));
        hash = hashCombine(hash, hashFloat(vertex.z));
      }
      hash = hashCombine(hash, face.textureProjection.valid ? 1U : 0U);
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
  }
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

[[nodiscard]] SDL_GPUTexture* uploadRgbaTexture(
  SDL_GPUDevice* device,
  const std::uint8_t* pixels,
  int width,
  int height
) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  const SDL_GPUTextureCreateInfo textureInfo = {
    SDL_GPU_TEXTURETYPE_2D,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    SDL_GPU_TEXTUREUSAGE_SAMPLER,
    static_cast<Uint32>(width),
    static_cast<Uint32>(height),
    1,
    1,
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
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
  SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
  if (!submitted) {
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  return texture;
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
    missingMaterial ? "__missing_world_texture" : "__white_world_texture",
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
  WorldTexture texture = {
    uploadRgbaTexture(
      device,
      static_cast<const std::uint8_t*>(converted->pixels),
      converted->w,
      converted->h
    ),
    converted->w,
    converted->h,
    material.aliases[0],
    false,
  };
  SDL_DestroySurface(converted);
  return texture;
}

[[nodiscard]] SDL_GPUShader* loadGpuShader(
  SDL_GPUDevice* device,
  std::string_view filename,
  SDL_GPUShaderStage stage,
  Uint32 samplerCount = 0,
  Uint32 uniformBufferCount = 0
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
  createInfo.num_uniform_buffers = uniformBufferCount;
  SDL_GPUShader* shader = SDL_CreateGPUShader(device, &createInfo);
  SDL_free(code);
  return shader;
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
  const std::array<SDL_GPUVertexAttribute, 3> vertexAttributes = {{
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
  bool depthWrite
) {
  SDL_GPUShader* vertexShader = loadGpuShader(
    device,
    "world3d.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX,
    0,
    1
  );
  if (vertexShader == nullptr) {
    return nullptr;
  }
  SDL_GPUShader* fragmentShader = loadGpuShader(
    device,
    "world3d.frag.spv",
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
  const std::array<SDL_GPUVertexAttribute, 3> vertexAttributes = {{
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
  createInfo.rasterizer_state.enable_depth_clip = true;
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = depthWrite;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  createInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  createInfo.target_info.has_depth_stencil_target = true;

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
  bool additiveBlend
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
    fragmentShaderName,
    SDL_GPU_SHADERSTAGE_FRAGMENT
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
  const std::array<SDL_GPUVertexAttribute, 8> vertexAttributes = {{
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
      SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
      offsetof(GpuSimpleInstance, red),
    },
    {
      7,
      1,
      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
      offsetof(GpuSimpleInstance, visualPhase),
    },
  }};
  SDL_GPUColorTargetDescription colorTarget = {};
  colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);
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
  createInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  createInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  createInfo.depth_stencil_state.enable_depth_test = true;
  createInfo.depth_stencil_state.enable_depth_write = depthWrite;
  createInfo.target_info.color_target_descriptions = &colorTarget;
  createInfo.target_info.num_color_targets = 1;
  createInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  createInfo.target_info.has_depth_stencil_target = true;

  SDL_GPUGraphicsPipeline* pipeline =
    SDL_CreateGPUGraphicsPipeline(device, &createInfo);
  SDL_ReleaseGPUShader(device, fragmentShader);
  SDL_ReleaseGPUShader(device, vertexShader);
  return pipeline;
}

[[nodiscard]] std::array<std::uint8_t, kFontAtlasWidth * kFontAtlasHeight>
buildFontAtlas() {
  std::array<std::uint8_t, kFontAtlasWidth * kFontAtlasHeight> pixels = {};
  for (Uint32 y = 0; y < 8; ++y) {
    for (Uint32 x = 0; x < 8; ++x) {
      pixels[y * kFontAtlasWidth + x] = 255;
    }
  }

  const std::size_t fontGlyphCount =
    sizeof(SDL_RenderDebugTextFontData) / sizeof(SDL_RenderDebugTextFontData[0]) / 8U;
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
  }
  for (const std::uint32_t character : {
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
    const Uint32 cellX = (character % 16U) * 8U;
    const Uint32 cellY = (character / 16U) * 8U;
    for (Uint32 y = 0; y < 8; ++y) {
      const std::uint8_t bits = (*glyph)[y];
      for (Uint32 x = 0; x < 8; ++x) {
        const bool set = (bits & (1U << x)) != 0;
        pixels[(cellY + y) * kFontAtlasWidth + cellX + x] = set ? 255 : 0;
      }
    }
  }
  return pixels;
}

[[nodiscard]] SDL_GPUTexture* createFontTexture(SDL_GPUDevice* device) {
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
    return nullptr;
  }

  void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return nullptr;
  }
  const auto pixels = buildFontAtlas();
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
    return nullptr;
  }
  return texture;
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
  const Arena& arena
) {
  const auto buildStart = RenderClock::now();
  Scene3D worldScene = buildStaticWorldScene(arena);
  auto mesh = new StaticWorldMesh();
  mesh->arenaFingerprint = arenaStaticWorldFingerprint(arena);
  mesh->sourceTriangles = static_cast<std::uint32_t>(worldScene.vertices.size() / 3U);
  mesh->vertexCount = static_cast<std::uint32_t>(worldScene.vertices.size());

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
    if (textureDebugEnabled()) {
      std::cerr
        << "LG_DUEL_TEXTURE_PIPELINE_V2 world texture material="
        << stored->material
        << " source=" << stored->width << 'x' << stored->height << '\n';
    }
  }

  std::vector<GpuVertex> gpuVertices;
  gpuVertices.reserve(worldScene.vertices.size());
  mesh->batches.reserve(referenced.size() + 1U);
  const auto appendBatch = [&](std::uint32_t materialId, WorldTexture* texture) {
    const Uint32 firstVertex = static_cast<Uint32>(gpuVertices.size());
    const float width = static_cast<float>(std::max(1, texture->width));
    const float height = static_cast<float>(std::max(1, texture->height));
    for (std::size_t index = 0; index + 2U < worldScene.vertices.size(); index += 3U) {
      if (worldScene.vertices[index].materialId != materialId) {
        continue;
      }
      for (std::size_t offset = 0; offset < 3U; ++offset) {
        const Vertex3D& source = worldScene.vertices[index + offset];
        gpuVertices.push_back(gpuVertex3D(source, source.u / width, source.v / height));
      }
    }
    const Uint32 vertexCount = static_cast<Uint32>(gpuVertices.size()) - firstVertex;
    if (vertexCount > 0) {
      mesh->batches.push_back({materialId, firstVertex, vertexCount, texture});
    }
  };

  appendBatch(0U, whiteTexture);
  for (std::uint32_t materialId : referenced) {
    WorldTexture* texture = missingTexture;
    if (const auto found = textureByMaterial.find(materialId); found != textureByMaterial.end()) {
      texture = found->second;
    }
    appendBatch(materialId, texture);
  }

  const SDL_GPUSamplerCreateInfo samplerInfo = {
    SDL_GPU_FILTER_NEAREST,
    SDL_GPU_FILTER_NEAREST,
    SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
    SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
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
  mesh->sampler = SDL_CreateGPUSampler(device, &samplerInfo);
  const Uint32 uploadSize =
    static_cast<Uint32>(gpuVertices.size() * sizeof(GpuVertex));
  const SDL_GPUBufferCreateInfo vertexBufferInfo = {
    SDL_GPU_BUFFERUSAGE_VERTEX,
    std::max<Uint32>(uploadSize, 1U),
    0,
  };
  mesh->vertexBuffer = SDL_CreateGPUBuffer(device, &vertexBufferInfo);
  if (mesh->sampler == nullptr || mesh->vertexBuffer == nullptr) {
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
      << " staticVertices=" << mesh->vertexCount
      << " staticBatches=" << mesh->batches.size()
      << " referencedMaterials=" << mesh->referencedMaterials
      << " loadedTextures=" << mesh->loadedTextures
      << " missingTextures=" << mesh->missingTextures
      << " buildMs=" << mesh->buildMilliseconds
      << '\n';
  }
  return mesh;
}

[[nodiscard]] StaticWorldMesh* ensureStaticWorldMesh(
  SDL_GPUDevice* device,
  StaticWorldMesh*& mesh,
  const Arena& arena
) {
  const std::uint64_t fingerprint = arenaStaticWorldFingerprint(arena);
  if (mesh != nullptr && mesh->arenaFingerprint == fingerprint) {
    return mesh;
  }
  destroyStaticWorldMesh(device, mesh);
  mesh = buildStaticWorldMesh(device, arena);
  return mesh;
}

[[nodiscard]] bool uploadStaticVertices(
  SDL_GPUDevice* device,
  std::span<const GpuVertex> vertices,
  SDL_GPUBuffer*& buffer
) {
  const Uint32 uploadSize =
    static_cast<Uint32>(vertices.size() * sizeof(GpuVertex));
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

[[nodiscard]] GpuSimpleResources* createGpuSimpleResources(SDL_GPUDevice* device) {
  auto* resources = new GpuSimpleResources();
  const StaticMeshAsset* plasmaCore = staticMeshAsset(MeshHandle::PlasmaCore);
  if (plasmaCore == nullptr || plasmaCore->vertices.empty()) {
    destroyGpuSimpleResources(device, resources);
    return nullptr;
  }

  std::vector<GpuVertex> coreVertices;
  coreVertices.reserve(plasmaCore->vertices.size());
  for (const Vertex3D& vertex : plasmaCore->vertices) {
    coreVertices.push_back({
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
  if (!uploadStaticVertices(device, coreVertices, resources->plasmaCore.vertexBuffer)) {
    destroyGpuSimpleResources(device, resources);
    return nullptr;
  }
  resources->plasmaCore.vertexCount = static_cast<Uint32>(coreVertices.size());
  resources->plasmaCore.handle = MeshHandle::PlasmaCore;

  const std::array<GpuVertex, 6> glowQuad = {{
    {-1.0F, -1.0F, 0.0F, 255, 255, 255, 255, 0.0F, 0.0F},
    { 1.0F, -1.0F, 0.0F, 255, 255, 255, 255, 1.0F, 0.0F},
    { 1.0F,  1.0F, 0.0F, 255, 255, 255, 255, 1.0F, 1.0F},
    {-1.0F, -1.0F, 0.0F, 255, 255, 255, 255, 0.0F, 0.0F},
    { 1.0F,  1.0F, 0.0F, 255, 255, 255, 255, 1.0F, 1.0F},
    {-1.0F,  1.0F, 0.0F, 255, 255, 255, 255, 0.0F, 1.0F},
  }};
  if (!uploadStaticVertices(device, glowQuad, resources->plasmaGlow.vertexBuffer)) {
    destroyGpuSimpleResources(device, resources);
    return nullptr;
  }
  resources->plasmaGlow.vertexCount = static_cast<Uint32>(glowQuad.size());
  resources->plasmaGlow.handle = BillboardHandle::PlasmaGlow;
  resources->instances.staging.reserve(kMaxRocketProjectiles * 2U);
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
    if (batch.mesh == MeshHandle::PlasmaCore && batch.pass == RenderPass::OpaqueWorld) {
      if (
        meshPipeline == nullptr ||
        resources->plasmaCore.vertexBuffer == nullptr ||
        resources->plasmaCore.vertexCount == 0U
      ) {
        continue;
      }
      SDL_BindGPUGraphicsPipeline(pass, meshPipeline);
      const std::array<SDL_GPUBufferBinding, 2> bindings = {{
        {resources->plasmaCore.vertexBuffer, 0},
        {
          resources->instances.buffer,
          batch.firstInstance * static_cast<Uint32>(sizeof(GpuSimpleInstance)),
        },
      }};
      SDL_BindGPUVertexBuffers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
      SDL_DrawGPUPrimitives(
        pass,
        resources->plasmaCore.vertexCount,
        batch.instanceCount,
        0,
        0
      );
    } else if (
      batch.billboard == BillboardHandle::PlasmaGlow &&
      batch.pass == RenderPass::AdditiveGlow
    ) {
      if (
        glowPipeline == nullptr ||
        resources->plasmaGlow.vertexBuffer == nullptr ||
        resources->plasmaGlow.vertexCount == 0U
      ) {
        continue;
      }
      SDL_BindGPUGraphicsPipeline(pass, glowPipeline);
      const std::array<SDL_GPUBufferBinding, 2> bindings = {{
        {resources->plasmaGlow.vertexBuffer, 0},
        {
          resources->instances.buffer,
          batch.firstInstance * static_cast<Uint32>(sizeof(GpuSimpleInstance)),
        },
      }};
      SDL_BindGPUVertexBuffers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
      SDL_DrawGPUPrimitives(
        pass,
        resources->plasmaGlow.vertexCount,
        batch.instanceCount,
        0,
        0
      );
    }
  }
}

[[nodiscard]] SDL_GPUTexture* ensureDepthTexture(
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
    SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
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
  float outputWidth,
  float outputHeight
) {
  float x = text.position.x;
  float y = text.position.y;
  const float glyphSize = 8.0F * text.scale;
  for (std::size_t index = 0; index < text.text.size();) {
    const auto rawCharacter = static_cast<unsigned char>(text.text[index]);
    if (rawCharacter == '\n') {
      x = text.position.x;
      y += glyphSize;
      ++index;
      continue;
    }
    const BitmapGlyphLookup glyph = bitmapGlyphAt(text.text, index);
    const Uint32 character = glyph.atlasCodepoint;
    if (glyph.drawable) {
      const float u0 =
        static_cast<float>((character % 16U) * 8U) /
        static_cast<float>(kFontAtlasWidth);
      const float v0 =
        static_cast<float>((character / 16U) * 8U) /
        static_cast<float>(kFontAtlasHeight);
      const float u1 = u0 + 8.0F / static_cast<float>(kFontAtlasWidth);
      const float v1 = v0 + 8.0F / static_cast<float>(kFontAtlasHeight);
      const std::array<ScreenPoint, 4> points = {{
        {x, y},
        {x + glyphSize, y},
        {x + glyphSize, y + glyphSize},
        {x, y + glyphSize},
      }};
      appendTriangle(
        vertices,
        points[0],
        points[1],
        points[2],
        text.color,
        outputWidth,
        outputHeight,
        {{{u0, v0}, {u1, v0}, {u1, v1}}}
      );
      appendTriangle(
        vertices,
        points[0],
        points[2],
        points[3],
        text.color,
        outputWidth,
        outputHeight,
        {{{u0, v0}, {u1, v1}, {u0, v1}}}
      );
    }
    x += glyphSize;
    index += std::max<std::size_t>(1U, glyph.byteLength);
  }
}

void appendCommands(
  std::vector<GpuVertex>& vertices,
  const std::vector<DrawCommand2D>& commands,
  float outputWidth,
  float outputHeight
) {
  for (const DrawCommand2D& command : commands) {
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
        } else if constexpr (std::is_same_v<Primitive, Text2D>) {
          appendText(vertices, primitive, outputWidth, outputHeight);
        }
      },
      command
    );
  }
}

const PlayerState& firstVisibleRemote(
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers
) {
  for (const RemotePlayerView& remote : remotePlayers) {
    if (remote.visible) {
      return remote.player;
    }
  }
  return remotePlayers.front().player;
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
  SDL_GPUGraphicsPipeline* pipeline3D,
  SDL_GPUGraphicsPipeline* pipeline3DTranslucent,
  SDL_GPUGraphicsPipeline* instancedMeshPipeline,
  SDL_GPUGraphicsPipeline* instancedGlowPipeline,
  SDL_GPUBuffer* vertexBuffer,
  SDL_GPUTransferBuffer* transferBuffer,
  GpuSimpleResources* simpleResources,
  SDL_GPUTexture* fontTexture,
  SDL_GPUSampler* fontSampler,
  TextureAtlas* worldAtlas,
  StaticWorldMesh*& staticWorld,
  SDL_GPUTexture*& depthTexture,
  Uint32& depthWidth,
  Uint32& depthHeight,
  std::vector<GpuVertex>& vertices,
  SDL_Window* window,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console,
  RendererFrameDiagnostics& diagnostics
) {
  diagnostics.swapchainAcquireMilliseconds = 0.0F;
  diagnostics.sceneBuildMilliseconds = 0.0F;
  diagnostics.gpuVertexUploadMilliseconds = 0.0F;
  diagnostics.worldDrawIssueMilliseconds = 0.0F;
  diagnostics.renderBuildUploadMilliseconds = 0.0F;
  diagnostics.submitMilliseconds = 0.0F;
  diagnostics.worldSourceTriangles = 0;
  diagnostics.worldRenderedTriangles = 0;
  diagnostics.worldVertexCount = 0;
  diagnostics.worldDrawCalls = 0;
  diagnostics.worldLoadedTextures = 0;
  diagnostics.worldReferencedMaterials = 0;
  diagnostics.dynamicOpaqueVertices = 0;
  diagnostics.dynamicTranslucentVertices = 0;
  diagnostics.totalUploadedVertices = 0;
  diagnostics.dynamicTriangles = 0;
  diagnostics.visibleRemotePlayers = 0;
  diagnostics.remoteBodyModelsBuilt = 0;
  diagnostics.remoteWeaponModelsBuilt = 0;
  diagnostics.playerOutlinesBuilt = 0;
  diagnostics.remoteCandidates = 0;
  diagnostics.remoteFrustumVisible = 0;
  diagnostics.remoteFrustumCulled = 0;
  diagnostics.projectilesActive = 0;
  diagnostics.projectilesFrustumCulled = 0;
  diagnostics.projectilesRendered = 0;
  diagnostics.projectileCoreInstances = 0;
  diagnostics.projectileGlowInstances = 0;
  diagnostics.projectileInstanceUploadBytes = 0;
  diagnostics.projectileMeshDrawCalls = 0;
  diagnostics.projectileGlowDrawCalls = 0;
  diagnostics.legacyProjectileDynamicVertices = 0;
  SDL_GPUCommandBuffer* commandBuffer =
    SDL_AcquireGPUCommandBuffer(device);
  if (commandBuffer == nullptr) {
    return false;
  }

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
    Scene3D perspectiveScene;
    vertices.clear();
    const auto sceneBuildStart = RenderClock::now();
    perspectiveScene = buildPerspectiveScene(
      static_cast<float>(outputWidth) / static_cast<float>(outputHeight),
      arena,
      player,
      remotePlayers,
      localLightningGun,
      weaponFires,
      rocketExplosions,
      rockets,
      settings
    );
    diagnostics.remoteCandidates = perspectiveScene.remoteCandidates;
    diagnostics.remoteFrustumVisible = perspectiveScene.remoteFrustumVisible;
    diagnostics.remoteFrustumCulled = perspectiveScene.remoteFrustumCulled;
    diagnostics.projectilesActive =
      perspectiveScene.projectileStats.projectilesActive;
    diagnostics.projectilesFrustumCulled =
      perspectiveScene.projectileStats.projectilesFrustumCulled;
    diagnostics.projectilesRendered =
      perspectiveScene.projectileStats.projectilesRendered;
    diagnostics.projectileCoreInstances =
      perspectiveScene.projectileStats.projectileCoreInstances;
    diagnostics.projectileGlowInstances =
      perspectiveScene.projectileStats.projectileGlowInstances;
    diagnostics.projectileInstanceUploadBytes =
      perspectiveScene.projectileStats.projectileInstanceUploadBytes;
    diagnostics.projectileMeshDrawCalls =
      perspectiveScene.projectileStats.projectileMeshDrawCalls;
    diagnostics.projectileGlowDrawCalls =
      perspectiveScene.projectileStats.projectileGlowDrawCalls;
    diagnostics.legacyProjectileDynamicVertices =
      perspectiveScene.projectileStats.legacyProjectileDynamicVertices;
    diagnostics.remoteBodyModelsBuilt = perspectiveScene.remoteBodyModelsBuilt;
    diagnostics.remoteWeaponModelsBuilt =
      perspectiveScene.remoteWeaponModelsBuilt;
    diagnostics.playerOutlinesBuilt = perspectiveScene.playerOutlinesBuilt;
    appendScene3D(vertices, perspectiveScene, worldAtlas);
    diagnostics.sceneBuildMilliseconds =
      millisecondsBetween(sceneBuildStart, RenderClock::now());
    const Uint32 opaqueDynamicVertexCount =
      static_cast<Uint32>(vertices.size());
    appendVertices3D(vertices, perspectiveScene.translucentVertices, worldAtlas);
    const Uint32 dynamic3DVertexCount = static_cast<Uint32>(vertices.size());
    const Uint32 worldVertexCount = dynamic3DVertexCount;
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
    appendCommands(
      vertices,
      floatingHealthBars.overlayCommands,
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
    appendCommands(
      vertices,
      floatingDamageNumbers.overlayCommands,
      static_cast<float>(outputWidth),
      static_cast<float>(outputHeight)
    );
    if (
      hud.selectedWeapon != Weapon::MachineGun &&
      hud.selectedWeapon != Weapon::Shotgun
    ) {
      const DrawList2D weaponOverlay = buildPerspectiveWeaponOverlay(
        static_cast<int>(outputWidth),
        static_cast<int>(outputHeight),
        localLightningGun,
        hud.selectedWeapon,
        hud.previousWeapon,
        hud.weaponSwitchProgress,
        settings
      );
      appendCommands(
        vertices,
        weaponOverlay.overlayCommands,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight)
      );
    }
    const DrawList2D ui = buildScreenUi(
      static_cast<int>(outputWidth),
      static_cast<int>(outputHeight),
      firstVisibleRemote(remotePlayers),
      settings,
      hud,
      console
    );
    appendCommands(
      vertices,
      ui.overlayCommands,
      static_cast<float>(outputWidth),
      static_cast<float>(outputHeight)
    );
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
    diagnostics.dynamicTriangles =
      (diagnostics.dynamicOpaqueVertices + diagnostics.dynamicTranslucentVertices) / 3U;
    if (vertices.size() > kMaxGpuVertices) {
      (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
      SDL_SetError("SDL_GPU 2D vertex capacity exceeded");
      return false;
    }

    if (!vertices.empty()) {
      const auto uploadStart = RenderClock::now();
      void* mapped =
        SDL_MapGPUTransferBuffer(device, transferBuffer, true);
      if (mapped == nullptr) {
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
      }
      const Uint32 uploadSize =
        static_cast<Uint32>(vertices.size() * sizeof(GpuVertex));
      std::memcpy(mapped, vertices.data(), uploadSize);
      SDL_UnmapGPUTransferBuffer(device, transferBuffer);

      SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
      if (copyPass == nullptr) {
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
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
      )
    ) {
      (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
      return false;
    }
    diagnostics.projectileInstanceUploadBytes =
      static_cast<std::uint32_t>(
        perspectiveScene.simpleInstances.size() * sizeof(GpuSimpleInstance)
      );
    const auto uploadEnd = RenderClock::now();
    diagnostics.gpuVertexUploadMilliseconds =
      millisecondsBetween(uploadStart, uploadEnd);
    diagnostics.totalUploadedVertices =
      static_cast<std::uint32_t>(vertices.size());

    const auto drawIssueStart = uploadEnd;
    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture = swapchainTexture;
    colorTarget.clear_color = {0.047F, 0.055F, 0.071F, 1.0F};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    depthTexture = ensureDepthTexture(
      device,
      depthTexture,
      depthWidth,
      depthHeight,
      outputWidth,
      outputHeight
    );
    if (depthTexture == nullptr) {
      (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
      return false;
    }

    SDL_GPUDepthStencilTargetInfo depthTarget = {};
    depthTarget.texture = depthTexture;
    depthTarget.clear_depth = 1.0F;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle = true;
    SDL_GPURenderPass* worldPass = SDL_BeginGPURenderPass(
      commandBuffer,
      &colorTarget,
      1,
      &depthTarget
    );
    if (worldPass == nullptr) {
      (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
      return false;
    }
      StaticWorldMesh* worldMesh = ensureStaticWorldMesh(device, staticWorld, arena);
      const bool hasStaticWorld = worldMesh != nullptr &&
        worldMesh->vertexBuffer != nullptr &&
        worldMesh->sampler != nullptr &&
        !worldMesh->batches.empty();
      if (hasStaticWorld || dynamic3DVertexCount > 0 || !perspectiveScene.simpleInstances.empty()) {
        const auto worldDrawStart = RenderClock::now();
        if (hasStaticWorld) {
          diagnostics.worldSourceTriangles = worldMesh->sourceTriangles;
          diagnostics.worldRenderedTriangles = worldMesh->sourceTriangles;
          diagnostics.worldVertexCount = worldMesh->vertexCount;
          diagnostics.worldDrawCalls = static_cast<std::uint32_t>(worldMesh->batches.size());
          diagnostics.worldLoadedTextures = worldMesh->loadedTextures;
          diagnostics.worldReferencedMaterials = worldMesh->referencedMaterials;
        }
        struct alignas(16) CameraUniform {
          float position[4];
          float right[4];
          float up[4];
          float forward[4];
          float projection[4];
        };
        const PerspectiveCamera& camera = perspectiveScene.camera;
        const CameraUniform uniform = {
          {
            camera.position.x,
            camera.position.y,
            camera.position.z,
            0.0F,
          },
          {camera.right.x, camera.right.y, camera.right.z, 0.0F},
          {camera.up.x, camera.up.y, camera.up.z, 0.0F},
          {
            camera.forward.x,
            camera.forward.y,
            camera.forward.z,
            0.0F,
          },
          {
            camera.focalLength,
            camera.aspectRatio,
            camera.nearPlane,
            512.0F,
          },
        };
        SDL_PushGPUVertexUniformData(
          commandBuffer,
          0,
          &uniform,
          sizeof(uniform)
        );
        SDL_BindGPUGraphicsPipeline(worldPass, pipeline3D);
        if (hasStaticWorld) {
          const SDL_GPUBufferBinding staticBinding = {worldMesh->vertexBuffer, 0};
          SDL_BindGPUVertexBuffers(worldPass, 0, &staticBinding, 1);
          for (const StaticWorldBatch& batch : worldMesh->batches) {
            if (batch.vertexCount == 0 || batch.texture == nullptr ||
                batch.texture->texture == nullptr) {
              continue;
            }
            const SDL_GPUTextureSamplerBinding textureBinding = {
              batch.texture->texture,
              worldMesh->sampler,
            };
            SDL_BindGPUFragmentSamplers(worldPass, 0, &textureBinding, 1);
            SDL_DrawGPUPrimitives(
              worldPass,
              batch.vertexCount,
              1,
              batch.firstVertex,
              0
            );
          }
        }
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
        drawSimpleInstanceBatches(
          worldPass,
          instancedMeshPipeline,
          instancedGlowPipeline,
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
              << " staticVertices=" << diagnostics.worldVertexCount
              << " drawCalls=" << diagnostics.worldDrawCalls
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
    colorTarget.load_op = SDL_GPU_LOADOP_LOAD;

    SDL_GPURenderPass* overlayPass =
      SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
    if (overlayPass == nullptr) {
      (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
      return false;
    }

    if (!vertices.empty()) {
      SDL_BindGPUGraphicsPipeline(overlayPass, pipeline2D);
      const SDL_GPUBufferBinding binding = {vertexBuffer, 0};
      SDL_BindGPUVertexBuffers(overlayPass, 0, &binding, 1);
      const SDL_GPUTextureSamplerBinding fontBinding = {
        fontTexture,
        fontSampler,
      };
      SDL_BindGPUFragmentSamplers(
        overlayPass,
        0,
        &fontBinding,
        1
      );

      const Uint32 overlayVertexCount =
        static_cast<Uint32>(vertices.size()) - worldVertexCount;
      if (overlayVertexCount > 0) {
        const SDL_Rect fullScissor = {
          0,
          0,
          static_cast<int>(outputWidth),
          static_cast<int>(outputHeight),
        };
        SDL_SetGPUScissor(overlayPass, &fullScissor);
        SDL_DrawGPUPrimitives(
          overlayPass,
          overlayVertexCount,
          1,
          worldVertexCount,
          0
        );
      }
    }
    SDL_EndGPURenderPass(overlayPass);
    diagnostics.worldDrawIssueMilliseconds =
      millisecondsBetween(drawIssueStart, RenderClock::now());
  }

  const auto submitStart = RenderClock::now();
  diagnostics.renderBuildUploadMilliseconds =
    millisecondsBetween(buildStart, submitStart);
  // Submit is CPU-side command submission time, not actual display present time.
  const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
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
  bool teammate
) {
  const float hitAmount = std::clamp(enemyHitAmount, 0.0F, 1.0F);
  if (teammate) {
    return {
      static_cast<float>(settings.teammateRed) / 255.0F,
      static_cast<float>(settings.teammateGreen) / 255.0F,
      static_cast<float>(settings.teammateBlue) / 255.0F,
      std::clamp(settings.teammateAlpha, 0.0F, 1.0F),
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
    std::clamp(settings.enemyAlpha, 0.0F, 1.0F),
  };
}

[[nodiscard]] SDL_FColor remoteOutlineColor(
  const RenderSettings& settings,
  bool teammate
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
    ),
  };
}

[[nodiscard]] SDL_FColor localBeamColor(const RenderSettings& settings) {
  const float hitAmount = std::clamp(settings.beamHitAmount, 0.0F, 1.0F);
  return {
    static_cast<float>(
      blendChannel(settings.beamRed, settings.beamHitRed, hitAmount)
    ) / 255.0F,
    static_cast<float>(
      blendChannel(settings.beamGreen, settings.beamHitGreen, hitAmount)
    ) / 255.0F,
    static_cast<float>(
      blendChannel(settings.beamBlue, settings.beamHitBlue, hitAmount)
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
        } else if constexpr (std::is_same_v<Primitive, Text2D>) {
          SDL_SetRenderScale(renderer, primitive.scale, primitive.scale);
          SDL_RenderDebugText(
            renderer,
            primitive.position.x / primitive.scale,
            primitive.position.y / primitive.scale,
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

  std::array<std::size_t, Arena::kWallCount> wallDrawOrder = {};
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    wallDrawOrder[index] = index;
  }
  std::sort(
    wallDrawOrder.begin(),
    wallDrawOrder.begin() + static_cast<std::ptrdiff_t>(arena.wallCount),
    [&arena, &camera](std::size_t lhs, std::size_t rhs) {
      const Vec3 lhsCenter =
        (arena.walls[lhs].min + arena.walls[lhs].max) * 0.5F;
      const Vec3 rhsCenter =
        (arena.walls[rhs].min + arena.walls[rhs].max) * 0.5F;
      return dot(lhsCenter - camera.position, camera.forward) >
        dot(rhsCenter - camera.position, camera.forward);
    }
  );

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  for (std::size_t orderIndex = 0; orderIndex < arena.wallCount; ++orderIndex) {
    const ArenaWall& wall = arena.walls[wallDrawOrder[orderIndex]];
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
  for (std::size_t orderIndex = 0; orderIndex < arena.wallCount; ++orderIndex) {
    const ArenaWall& wall = arena.walls[wallDrawOrder[orderIndex]];
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
    if (!remote.visible) {
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
      usesGeometryPlayerOutlineFallback(settings.playerOutlineStyle) &&
      outlineWidth > 0.0F
    ) {
      const SDL_FColor outlineColor =
        remoteOutlineColor(settings, remote.teammate);
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
          remotePlayer.position.x - remotePlayer.bounds.radius - outlineWidth,
          remotePlayer.position.y - remotePlayer.bounds.radius - outlineWidth,
          remotePlayer.position.z - remotePlayer.bounds.halfHeight -
            outlineWidth,
        },
        {
          remotePlayer.position.x + remotePlayer.bounds.radius + outlineWidth,
          remotePlayer.position.y + remotePlayer.bounds.radius + outlineWidth,
          remotePlayer.position.z + remotePlayer.bounds.halfHeight +
            outlineWidth,
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
      remoteModelColor(settings, remote.enemyHitAmount, remote.teammate)
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
    if (remote.visible) {
      drawBeam(remote.lightningGun, false);
    }
  }
  drawBeam(localLightningGun, true);

  for (const WeaponFireResult& fire : weaponFires) {
    if (!fire.fired) {
      continue;
    }
    if (fire.weapon == Weapon::Railgun) {
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
    } else if (fire.weapon == Weapon::RocketLauncher) {
      SDL_SetRenderDrawColor(renderer, 255, 150, 70, 235);
      drawThickPerspectiveLine(
        renderer,
        camera,
        width,
        height,
        fire.start,
        fire.end,
        3.0F
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
  for (const RocketExplosionResult& explosion : rocketExplosions) {
    if (!explosion.active) {
      continue;
    }
    SDL_SetRenderDrawColor(renderer, 255, 185, 80, 220);
    const Vec3 radius{explosion.radius, explosion.radius, explosion.radius};
    drawWireBox(
      renderer,
      camera,
      width,
      height,
      explosion.position - radius,
      explosion.position + radius
    );
  }
}

#endif

} // namespace

Renderer::~Renderer() {
  shutdown();
}

bool Renderer::initialize(void* window) {
#if LG_DUEL_HAS_SDL3
  window_ = window;
  if (gpuBackendRequested()) {
    SDL_GPUDevice* device = createGpuDevice();
    if (device != nullptr) {
      if (SDL_ClaimWindowForGPUDevice(
            device,
            static_cast<SDL_Window*>(window)
          )) {
        SDL_GPUGraphicsPipeline* pipeline = createGpuPipeline(
          device,
          static_cast<SDL_Window*>(window)
        );
        SDL_GPUGraphicsPipeline* pipeline3D = createGpuPipeline3D(
          device,
          static_cast<SDL_Window*>(window),
          true
        );
        SDL_GPUGraphicsPipeline* pipeline3DTranslucent = createGpuPipeline3D(
          device,
          static_cast<SDL_Window*>(window),
          false
        );
        SDL_GPUGraphicsPipeline* instancedMeshPipeline =
          createGpuInstancedPipeline3D(
            device,
            static_cast<SDL_Window*>(window),
            "instanced_mesh.vert.spv",
            "instanced_color.frag.spv",
            true,
            false
          );
        SDL_GPUGraphicsPipeline* instancedGlowPipeline =
          createGpuInstancedPipeline3D(
            device,
            static_cast<SDL_Window*>(window),
            "instanced_billboard.vert.spv",
            "instanced_glow.frag.spv",
            false,
            true
          );
        GpuSimpleResources* simpleResources = createGpuSimpleResources(device);
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
        SDL_GPUTexture* fontTexture = createFontTexture(device);
        const SDL_GPUSamplerCreateInfo samplerInfo = {
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
          SDL_CreateGPUSampler(device, &samplerInfo);
        if (
          pipeline != nullptr &&
          pipeline3D != nullptr &&
          pipeline3DTranslucent != nullptr &&
          instancedMeshPipeline != nullptr &&
          instancedGlowPipeline != nullptr &&
          simpleResources != nullptr &&
          vertexBuffer != nullptr &&
          transferBuffer != nullptr &&
          fontTexture != nullptr &&
          fontSampler != nullptr &&
          SDL_SetGPUAllowedFramesInFlight(device, 1)
        ) {
          gpuDevice_ = device;
          gpuPipeline_ = pipeline;
          gpuPipeline3D_ = pipeline3D;
          gpuPipeline3DTranslucent_ = pipeline3DTranslucent;
          gpuPipelineInstancedMesh_ = instancedMeshPipeline;
          gpuPipelineInstancedGlow_ = instancedGlowPipeline;
          gpuVertexBuffer_ = vertexBuffer;
          gpuTransferBuffer_ = transferBuffer;
          gpuSimpleResources_ = simpleResources;
          gpuFontTexture_ = fontTexture;
          gpuFontSampler_ = fontSampler;
          gpuWorldTextureAtlas_ = nullptr;
          auto* vertexScratch = new std::vector<GpuVertex>();
          vertexScratch->reserve(kMaxGpuVertices);
          gpuVertexScratch_ = vertexScratch;
          gpuBackend_ = true;
          const char* driver = SDL_GetGPUDeviceDriver(device);
          backendName_ = "SDL_GPU/";
          backendName_ += driver != nullptr ? driver : "unknown";
          return true;
        }

        std::cerr
          << "SDL_GPU resource initialization failed: " << SDL_GetError()
          << "\nFalling back to SDL_Renderer.\n";
        if (transferBuffer != nullptr) {
          SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        }
        if (fontSampler != nullptr) {
          SDL_ReleaseGPUSampler(device, fontSampler);
        }
        if (fontTexture != nullptr) {
          SDL_ReleaseGPUTexture(device, fontTexture);
        }
        if (vertexBuffer != nullptr) {
          SDL_ReleaseGPUBuffer(device, vertexBuffer);
        }
        destroyGpuSimpleResources(device, simpleResources);
        if (pipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
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
        if (instancedGlowPipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, instancedGlowPipeline);
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
          << "\nFalling back to SDL_Renderer.\n";
        SDL_DestroyGPUDevice(device);
      }
    }
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
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console
) {
#if LG_DUEL_HAS_SDL3
  const auto renderStart = RenderClock::now();
  if (gpuBackend_) {
    auto* depthTexture = static_cast<SDL_GPUTexture*>(gpuDepthTexture_);
    auto* staticWorld = static_cast<StaticWorldMesh*>(gpuStaticWorld_);
    if (!renderGpuFrame(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline3D_),
          static_cast<SDL_GPUGraphicsPipeline*>(
            gpuPipeline3DTranslucent_
          ),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedMesh_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedGlow_),
          static_cast<SDL_GPUBuffer*>(gpuVertexBuffer_),
          static_cast<SDL_GPUTransferBuffer*>(gpuTransferBuffer_),
          static_cast<GpuSimpleResources*>(gpuSimpleResources_),
          static_cast<SDL_GPUTexture*>(gpuFontTexture_),
          static_cast<SDL_GPUSampler*>(gpuFontSampler_),
          static_cast<TextureAtlas*>(gpuWorldTextureAtlas_),
          staticWorld,
          depthTexture,
          gpuDepthWidth_,
          gpuDepthHeight_,
          *static_cast<std::vector<GpuVertex>*>(gpuVertexScratch_),
          static_cast<SDL_Window*>(window_),
          arena,
          player,
          remotePlayers,
          localLightningGun,
          weaponFires,
          rocketExplosions,
          rockets,
          settings,
          hud,
          console,
          lastFrameDiagnostics_
        ) &&
        !gpuErrorReported_) {
      std::cerr << "SDL_GPU frame submission failed: " << SDL_GetError() << '\n';
      gpuErrorReported_ = true;
    }
    gpuStaticWorld_ = staticWorld;
    gpuDepthTexture_ = depthTexture;
    lastFrameDiagnostics_.totalRenderMilliseconds =
      millisecondsBetween(renderStart, RenderClock::now());
    return;
  }

  lastFrameDiagnostics_.swapchainAcquireMilliseconds = 0.0F;
  lastFrameDiagnostics_.sceneBuildMilliseconds = 0.0F;
  lastFrameDiagnostics_.gpuVertexUploadMilliseconds = 0.0F;
  lastFrameDiagnostics_.worldDrawIssueMilliseconds = 0.0F;
  lastFrameDiagnostics_.renderBuildUploadMilliseconds = 0.0F;
  lastFrameDiagnostics_.submitMilliseconds = 0.0F;
  lastFrameDiagnostics_.worldSourceTriangles = 0;
  lastFrameDiagnostics_.worldRenderedTriangles = 0;
  lastFrameDiagnostics_.worldVertexCount = 0;
  lastFrameDiagnostics_.worldDrawCalls = 0;
  lastFrameDiagnostics_.worldLoadedTextures = 0;
  lastFrameDiagnostics_.worldReferencedMaterials = 0;
  lastFrameDiagnostics_.dynamicOpaqueVertices = 0;
  lastFrameDiagnostics_.dynamicTranslucentVertices = 0;
  lastFrameDiagnostics_.totalUploadedVertices = 0;
  lastFrameDiagnostics_.dynamicTriangles = 0;
  lastFrameDiagnostics_.visibleRemotePlayers = 0;
  lastFrameDiagnostics_.remoteBodyModelsBuilt = 0;
  lastFrameDiagnostics_.remoteWeaponModelsBuilt = 0;
  lastFrameDiagnostics_.playerOutlinesBuilt = 0;
  lastFrameDiagnostics_.remoteCandidates = 0;
  lastFrameDiagnostics_.remoteFrustumVisible = 0;
  lastFrameDiagnostics_.remoteFrustumCulled = 0;
  lastFrameDiagnostics_.projectilesActive = 0;
  lastFrameDiagnostics_.projectilesFrustumCulled = 0;
  lastFrameDiagnostics_.projectilesRendered = 0;
  lastFrameDiagnostics_.projectileCoreInstances = 0;
  lastFrameDiagnostics_.projectileGlowInstances = 0;
  lastFrameDiagnostics_.projectileInstanceUploadBytes = 0;
  lastFrameDiagnostics_.projectileMeshDrawCalls = 0;
  lastFrameDiagnostics_.projectileGlowDrawCalls = 0;
  lastFrameDiagnostics_.legacyProjectileDynamicVertices = 0;
  auto* renderer = static_cast<SDL_Renderer*>(renderer_);
  if (renderer == nullptr) {
    lastFrameDiagnostics_.totalRenderMilliseconds =
      millisecondsBetween(renderStart, RenderClock::now());
    return;
  }

  int width = 0;
  int height = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

  SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
  SDL_RenderClear(renderer);

  const Scene3D perspectiveScene = buildPerspectiveScene(
    static_cast<float>(width) / static_cast<float>(std::max(1, height)),
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
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
  lastFrameDiagnostics_.projectilesActive =
    perspectiveScene.projectileStats.projectilesActive;
  lastFrameDiagnostics_.projectilesFrustumCulled =
    perspectiveScene.projectileStats.projectilesFrustumCulled;
  lastFrameDiagnostics_.projectilesRendered =
    perspectiveScene.projectileStats.projectilesRendered;
  lastFrameDiagnostics_.projectileCoreInstances =
    perspectiveScene.projectileStats.projectileCoreInstances;
  lastFrameDiagnostics_.projectileGlowInstances =
    perspectiveScene.projectileStats.projectileGlowInstances;
  lastFrameDiagnostics_.projectileInstanceUploadBytes =
    perspectiveScene.projectileStats.projectileInstanceUploadBytes;
  lastFrameDiagnostics_.projectileMeshDrawCalls =
    perspectiveScene.projectileStats.projectileMeshDrawCalls;
  lastFrameDiagnostics_.projectileGlowDrawCalls =
    perspectiveScene.projectileStats.projectileGlowDrawCalls;
  lastFrameDiagnostics_.legacyProjectileDynamicVertices =
    perspectiveScene.projectileStats.legacyProjectileDynamicVertices;
  lastFrameDiagnostics_.remoteBodyModelsBuilt =
    perspectiveScene.remoteBodyModelsBuilt;
  lastFrameDiagnostics_.remoteWeaponModelsBuilt =
    perspectiveScene.remoteWeaponModelsBuilt;
  lastFrameDiagnostics_.playerOutlinesBuilt =
    perspectiveScene.playerOutlinesBuilt;
  drawPerspectiveWorld(
    renderer,
    width,
    height,
    arena,
    player,
    remotePlayers,
    perspectiveScene.remoteRenderVisible,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  const PerspectiveCamera camera = playerPerspectiveCamera(
    player,
    static_cast<float>(width) / static_cast<float>(std::max(1, height)),
    settings.fieldOfView
  );
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
      firstVisibleRemote(remotePlayers),
      settings,
      hud,
      console
    )
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

const RendererFrameDiagnostics& Renderer::lastFrameDiagnostics() const {
  return lastFrameDiagnostics_;
}

void Renderer::shutdown() {
#if LG_DUEL_HAS_SDL3
  if (gpuDevice_ != nullptr) {
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
    if (gpuFontTexture_ != nullptr) {
      SDL_ReleaseGPUTexture(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUTexture*>(gpuFontTexture_)
      );
      gpuFontTexture_ = nullptr;
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
    if (gpuPipelineInstancedGlow_ != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline(
        static_cast<SDL_GPUDevice*>(gpuDevice_),
        static_cast<SDL_GPUGraphicsPipeline*>(gpuPipelineInstancedGlow_)
      );
      gpuPipelineInstancedGlow_ = nullptr;
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
  gpuBackend_ = false;
  gpuErrorReported_ = false;
}

} // namespace lg
