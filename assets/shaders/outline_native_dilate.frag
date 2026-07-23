#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D outlineMask;

layout(set = 3, binding = 0, std140) uniform OutlineDilationData {
  vec4 texelSizeAndWidths;
  vec4 workRect;
} outline;

const int kFixedRadius = 3;

bool insideWorkRect(ivec2 pixel) {
  return pixel.x >= int(outline.workRect.x) &&
    pixel.y >= int(outline.workRect.y) &&
    pixel.x < int(outline.workRect.z) &&
    pixel.y < int(outline.workRect.w);
}

int groupAt(ivec2 pixel) {
  if (!insideWorkRect(pixel)) {
    return 0;
  }
  float value = texelFetch(outlineMask, pixel, 0).r;
  if (value > 0.75) {
    return 1;
  }
  if (value > 0.25) {
    return 2;
  }
  return 0;
}

void main() {
  ivec2 center = ivec2(texCoord * vec2(textureSize(outlineMask, 0)));
  int selectedGroup = 0;
  float selectedDistanceSquared = 999999.0;
  float selectedCoverage = 0.0;
  float maxActiveDistance = max(
    outline.texelSizeAndWidths.z,
    outline.texelSizeAndWidths.w
  ) + 0.5;
  float maxActiveDistanceSquared = maxActiveDistance * maxActiveDistance;
  for (int y = -kFixedRadius; y <= kFixedRadius; ++y) {
    for (int x = -kFixedRadius; x <= kFixedRadius; ++x) {
      float distanceSquared = float(x * x + y * y);
      if (distanceSquared >= maxActiveDistanceSquared) {
        continue;
      }
      int group = groupAt(center + ivec2(x, y));
      float radius = group == 1
        ? outline.texelSizeAndWidths.z
        : group == 2 ? outline.texelSizeAndWidths.w : 0.0;
      float coverage = clamp(radius + 0.5 - sqrt(distanceSquared), 0.0, 1.0);
      if (group != 0 && coverage > 0.0 &&
          distanceSquared < selectedDistanceSquared) {
        selectedGroup = group;
        selectedDistanceSquared = distanceSquared;
        selectedCoverage = coverage;
      }
    }
  }

  if (selectedGroup == 1) {
    outColor = vec4(1.0, selectedCoverage, 0.0, 1.0);
  } else if (selectedGroup == 2) {
    outColor = vec4(0.5, selectedCoverage, 0.0, 1.0);
  } else {
    outColor = vec4(0.0);
  }
}
