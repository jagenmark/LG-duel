#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D outlineMask;

layout(set = 3, binding = 0, std140) uniform OutlineDilationData {
  vec4 texelSizeAndWidths;
  vec4 workRect;
} outline;

const int kFixedRadius = 3;

bool insideWorkRect(vec2 pixel) {
  return pixel.x >= outline.workRect.x &&
    pixel.y >= outline.workRect.y &&
    pixel.x < outline.workRect.z &&
    pixel.y < outline.workRect.w;
}

int groupAt(vec2 uv) {
  vec2 textureSizePixels = 1.0 / outline.texelSizeAndWidths.xy;
  vec2 pixel = uv * textureSizePixels;
  if (!insideWorkRect(pixel)) {
    return 0;
  }
  float value = texture(outlineMask, uv).r;
  if (value > 0.75) {
    return 1;
  }
  if (value > 0.25) {
    return 2;
  }
  return 0;
}

float alphaAt(vec2 uv) {
  vec2 textureSizePixels = 1.0 / outline.texelSizeAndWidths.xy;
  vec2 pixel = uv * textureSizePixels;
  if (!insideWorkRect(pixel)) {
    return 0.0;
  }
  return texture(outlineMask, uv).a;
}

void main() {
  vec2 texelSize = outline.texelSizeAndWidths.xy;
  float enemyRadius = outline.texelSizeAndWidths.z;
  float teammateRadius = outline.texelSizeAndWidths.w;

  int selectedGroup = 0;
  float selectedDistanceSquared = 999999.0;
  float selectedCoverage = 0.0;
  float selectedAlpha = 0.0;
  for (int y = -kFixedRadius; y <= kFixedRadius; ++y) {
    for (int x = -kFixedRadius; x <= kFixedRadius; ++x) {
      float distanceSquared = float(x * x + y * y);
      int group = groupAt(texCoord + vec2(float(x), float(y)) * texelSize);
      float radius = group == 1 ? enemyRadius : group == 2 ? teammateRadius : 0.0;
      float distancePixels = sqrt(distanceSquared);
      float coverage = clamp(radius + 0.5 - distancePixels, 0.0, 1.0);
      if (group != 0 && coverage > 0.0 &&
          distanceSquared < selectedDistanceSquared) {
        selectedGroup = group;
        selectedDistanceSquared = distanceSquared;
        selectedCoverage = coverage;
        selectedAlpha = alphaAt(
          texCoord + vec2(float(x), float(y)) * texelSize
        );
      }
    }
  }

  if (selectedGroup == 1) {
    outColor = vec4(1.0, selectedCoverage, 0.0, selectedAlpha);
  } else if (selectedGroup == 2) {
    outColor = vec4(0.5, selectedCoverage, 0.0, selectedAlpha);
  } else {
    outColor = vec4(0.0);
  }
}
