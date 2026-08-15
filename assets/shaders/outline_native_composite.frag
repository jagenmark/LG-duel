#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D outlineMask;
layout(set = 2, binding = 1) uniform sampler2D outlineDilation;

layout(set = 3, binding = 0, std140) uniform OutlineCompositeData {
  vec4 texelSizeAndDebug;
  vec4 enemyColor;
  vec4 teammateColor;
  vec4 workRect;
} outline;

bool insideWorkRect(ivec2 pixel) {
  return pixel.x >= int(outline.workRect.x) &&
    pixel.y >= int(outline.workRect.y) &&
    pixel.x < int(outline.workRect.z) &&
    pixel.y < int(outline.workRect.w);
}

int groupFromValue(float value) {
  return value > 0.75 ? 1 : value > 0.25 ? 2 : 0;
}

void main() {
  ivec2 pixel = ivec2(texCoord * vec2(textureSize(outlineMask, 0)));
  float maskValue = insideWorkRect(pixel)
    ? texelFetch(outlineMask, pixel, 0).r
    : 0.0;
  if (outline.texelSizeAndDebug.z > 0.5) {
    outColor = vec4(maskValue, 0.0, 0.0, 1.0);
    return;
  }
  if (groupFromValue(maskValue) != 0) {
    discard;
  }

  vec4 dilated = texelFetch(outlineDilation, pixel, 0);
  int selectedGroup = groupFromValue(dilated.r);
  float coverage = clamp(dilated.g, 0.0, 1.0);
  float fadeAlpha = clamp(dilated.a, 0.0, 1.0);
  if (selectedGroup == 1) {
    outColor = vec4(
      outline.enemyColor.rgb,
      outline.enemyColor.a * coverage * fadeAlpha
    );
  } else if (selectedGroup == 2) {
    outColor = vec4(
      outline.teammateColor.rgb,
      outline.teammateColor.a * coverage * fadeAlpha
    );
  } else {
    discard;
  }
}
