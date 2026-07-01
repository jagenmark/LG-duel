#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D outlineMask;

layout(set = 3, binding = 0, std140) uniform OutlineCompositeData {
  vec4 texelSizeAndWidths;
  vec4 enemyColor;
  vec4 teammateColor;
  vec4 reserved;
} outline;

const int kMaxOutlineRadiusPixels = 16;

int groupAt(vec2 uv) {
  float value = texture(outlineMask, uv).r;
  if (value > 0.75) {
    return 1;
  }
  if (value > 0.25) {
    return 2;
  }
  return 0;
}

void main() {
  int centerGroup = groupAt(texCoord);
  if (centerGroup != 0) {
    discard;
  }

  vec2 texelSize = outline.texelSizeAndWidths.xy;
  float enemyWidth = outline.texelSizeAndWidths.z;
  float teammateWidth = outline.texelSizeAndWidths.w;
  int maxRadius = int(ceil(max(enemyWidth, teammateWidth)));
  maxRadius = clamp(maxRadius, 0, kMaxOutlineRadiusPixels);

  int selectedGroup = 0;
  float selectedDistanceSquared = 999999.0;
  for (int y = -kMaxOutlineRadiusPixels; y <= kMaxOutlineRadiusPixels; ++y) {
    if (abs(y) > maxRadius) {
      continue;
    }
    for (int x = -kMaxOutlineRadiusPixels; x <= kMaxOutlineRadiusPixels; ++x) {
      if (abs(x) > maxRadius) {
        continue;
      }
      float distanceSquared = float(x * x + y * y);
      int group = groupAt(texCoord + vec2(float(x), float(y)) * texelSize);
      float width = group == 1 ? enemyWidth : group == 2 ? teammateWidth : 0.0;
      if (group != 0 && distanceSquared <= width * width &&
          distanceSquared < selectedDistanceSquared) {
        selectedGroup = group;
        selectedDistanceSquared = distanceSquared;
      }
    }
  }

  if (selectedGroup == 1) {
    outColor = outline.enemyColor;
  } else if (selectedGroup == 2) {
    outColor = outline.teammateColor;
  } else {
    discard;
  }
}
