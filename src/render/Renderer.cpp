#include "render/Renderer.hpp"
#include "render/BitmapFont.hpp"
#include "render/Scene3D.hpp"
#include "render/ScreenUi.hpp"
#include "render/TopDownScene.hpp"
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
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
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

[[nodiscard]] std::string shaderPath(std::string_view filename) {
  const char* basePath = SDL_GetBasePath();
  std::string path = basePath != nullptr ? basePath : "";
  path += "shaders/";
  path += filename;
  return path;
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
    SDL_GPU_SHADERSTAGE_FRAGMENT
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
  const std::array<SDL_GPUVertexAttribute, 2> vertexAttributes = {{
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

void appendScene3D(
  std::vector<GpuVertex>& vertices,
  const Scene3D& scene
) {
  for (const Vertex3D& vertex : scene.vertices) {
    vertices.push_back({
      vertex.position.x,
      vertex.position.y,
      vertex.position.z,
      vertex.color.red,
      vertex.color.green,
      vertex.color.blue,
      vertex.color.alpha,
      kSolidTextureU,
      kSolidTextureV,
    });
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
  SDL_GPUBuffer* vertexBuffer,
  SDL_GPUTransferBuffer* transferBuffer,
  SDL_GPUTexture* fontTexture,
  SDL_GPUSampler* fontSampler,
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
  diagnostics.renderBuildUploadMilliseconds = 0.0F;
  diagnostics.submitMilliseconds = 0.0F;
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

  if (swapchainTexture != nullptr && outputWidth > 0 && outputHeight > 0) {
    DrawList2D topDownScene;
    Scene3D perspectiveScene;
    vertices.clear();
    if (settings.renderMode == 1) {
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
      appendScene3D(vertices, perspectiveScene);
    } else {
      topDownScene = buildTopDownScene(
        static_cast<int>(outputWidth),
        static_cast<int>(outputHeight),
        arena,
        player,
        remotePlayers,
        localLightningGun,
        weaponFires,
        rocketExplosions,
        rockets,
        settings,
        hud
      );
      appendCommands(
        vertices,
        topDownScene.commands,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight)
      );
    }
    const Uint32 opaqueWorldVertexCount =
      static_cast<Uint32>(vertices.size());
    if (settings.renderMode == 1) {
      for (const Vertex3D& vertex : perspectiveScene.translucentVertices) {
        vertices.push_back({
          vertex.position.x,
          vertex.position.y,
          vertex.position.z,
          vertex.color.red,
          vertex.color.green,
          vertex.color.blue,
          vertex.color.alpha,
          kSolidTextureU,
          kSolidTextureV,
        });
      }
    }
    const Uint32 worldVertexCount = static_cast<Uint32>(vertices.size());
    if (settings.renderMode == 0) {
      appendCommands(
        vertices,
        topDownScene.overlayCommands,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight)
      );
    } else {
      const DrawList2D floatingHealthBars = buildFloatingHealthBars(
        static_cast<int>(outputWidth),
        static_cast<int>(outputHeight),
        perspectiveScene.camera,
        arena,
        remotePlayers,
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
    if (vertices.size() > kMaxGpuVertices) {
      (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
      SDL_SetError("SDL_GPU 2D vertex capacity exceeded");
      return false;
    }

    if (!vertices.empty()) {
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
    }

    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture = swapchainTexture;
    colorTarget.clear_color = {0.047F, 0.055F, 0.071F, 1.0F};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    if (settings.renderMode == 1) {
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
      if (worldVertexCount > 0) {
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
        const SDL_GPUBufferBinding binding = {vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(worldPass, 0, &binding, 1);
        if (opaqueWorldVertexCount > 0) {
          SDL_DrawGPUPrimitives(
            worldPass,
            opaqueWorldVertexCount,
            1,
            0,
            0
          );
        }
        const Uint32 translucentVertexCount =
          worldVertexCount - opaqueWorldVertexCount;
        if (translucentVertexCount > 0) {
          SDL_BindGPUGraphicsPipeline(
            worldPass,
            pipeline3DTranslucent
          );
          SDL_DrawGPUPrimitives(
            worldPass,
            translucentVertexCount,
            1,
            opaqueWorldVertexCount,
            0
          );
        }
      }
      SDL_EndGPURenderPass(worldPass);
      colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
    }

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

      if (settings.renderMode == 0 && worldVertexCount > 0) {
        const SDL_Rect worldScissor = {
          std::max(0, static_cast<int>(topDownScene.clip.x)),
          std::max(0, static_cast<int>(topDownScene.clip.y)),
          std::max(0, static_cast<int>(topDownScene.clip.width)),
          std::max(0, static_cast<int>(topDownScene.clip.height)),
        };
        SDL_SetGPUScissor(overlayPass, &worldScissor);
        SDL_DrawGPUPrimitives(
          overlayPass,
          worldVertexCount,
          1,
          0,
          0
        );
      }

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
    drawFilledQuad(renderer, floorPoints, SDL_FColor{0.26F, 0.62F, 0.30F, 1.0F});
  }

  SDL_SetRenderDrawColor(renderer, 109, 195, 105, 255);
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

  SDL_SetRenderDrawColor(renderer, 127, 202, 111, 255);
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
      SDL_FColor{0.49F, 0.34F, 0.20F, 1.0F}
    );
  }

  SDL_SetRenderDrawColor(renderer, 127, 202, 111, 255);
  for (std::size_t orderIndex = 0; orderIndex < arena.wallCount; ++orderIndex) {
    const ArenaWall& wall = arena.walls[wallDrawOrder[orderIndex]];
    drawWireBox(renderer, camera, width, height, wall.min, wall.max);
    SDL_SetRenderDrawColor(renderer, 171, 235, 145, 255);
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
    SDL_SetRenderDrawColor(renderer, 127, 202, 111, 255);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const RemotePlayerView& remote : remotePlayers) {
    if (!remote.visible) {
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
          gpuVertexBuffer_ = vertexBuffer;
          gpuTransferBuffer_ = transferBuffer;
          gpuFontTexture_ = fontTexture;
          gpuFontSampler_ = fontSampler;
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
        if (pipeline != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        }
        if (pipeline3D != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline3D);
        }
        if (pipeline3DTranslucent != nullptr) {
          SDL_ReleaseGPUGraphicsPipeline(device, pipeline3DTranslucent);
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
    if (!renderGpuFrame(
          static_cast<SDL_GPUDevice*>(gpuDevice_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline_),
          static_cast<SDL_GPUGraphicsPipeline*>(gpuPipeline3D_),
          static_cast<SDL_GPUGraphicsPipeline*>(
            gpuPipeline3DTranslucent_
          ),
          static_cast<SDL_GPUBuffer*>(gpuVertexBuffer_),
          static_cast<SDL_GPUTransferBuffer*>(gpuTransferBuffer_),
          static_cast<SDL_GPUTexture*>(gpuFontTexture_),
          static_cast<SDL_GPUSampler*>(gpuFontSampler_),
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
    gpuDepthTexture_ = depthTexture;
    lastFrameDiagnostics_.totalRenderMilliseconds =
      millisecondsBetween(renderStart, RenderClock::now());
    return;
  }

  lastFrameDiagnostics_.swapchainAcquireMilliseconds = 0.0F;
  lastFrameDiagnostics_.renderBuildUploadMilliseconds = 0.0F;
  lastFrameDiagnostics_.submitMilliseconds = 0.0F;
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

  if (settings.renderMode == 1) {
    drawPerspectiveWorld(
      renderer,
      width,
      height,
      arena,
      player,
      remotePlayers,
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
    return;
  }

  const DrawList2D topDownScene = buildTopDownScene(
    width,
    height,
    arena,
    player,
    remotePlayers,
    localLightningGun,
    weaponFires,
    rocketExplosions,
    rockets,
    settings,
    hud
  );
  drawCommandList(renderer, topDownScene);
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
