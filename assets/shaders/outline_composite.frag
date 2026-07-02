#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D outlineMask;
layout(set = 2, binding = 1) uniform sampler2D outlineDilation;

layout(set = 3, binding = 0, std140) uniform OutlineCompositeData {
  vec4 texelSizeAndWidths;
  vec4 enemyColor;
  vec4 teammateColor;
  vec4 workRect;
} outline;

bool insideWorkRect(vec2 pixel) {
  return pixel.x >= outline.workRect.x &&
    pixel.y >= outline.workRect.y &&
    pixel.x < outline.workRect.z &&
    pixel.y < outline.workRect.w;
}

int groupAt(vec2 uv) {
  vec2 textureSizePixels = 1.0 / outline.texelSizeAndWidths.xy;
  if (!insideWorkRect(uv * textureSizePixels)) {
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

void main() {
  vec2 halfUv = texCoord;

  int centerGroup = groupAt(halfUv);
  if (centerGroup != 0) {
    discard;
  }

  vec4 dilated = texture(outlineDilation, halfUv);
  int selectedGroup = dilated.r > 0.75 ? 1 : dilated.r > 0.25 ? 2 : 0;
  float coverage = clamp(dilated.g, 0.0, 1.0);

  if (selectedGroup == 1) {
    outColor = vec4(outline.enemyColor.rgb, outline.enemyColor.a * coverage);
  } else if (selectedGroup == 2) {
    outColor = vec4(outline.teammateColor.rgb, outline.teammateColor.a * coverage);
  } else {
    discard;
  }
}
