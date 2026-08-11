#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 3, binding = 0, std140) uniform CompositeData {
  // Exposure, grade quality, unused, display gamma.
  vec4 parameters;
} composite;

#include "includes/display_gamma.glsl"

vec3 acesToneMap(vec3 color) {
  return clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
}

vec3 gradeColor(vec3 color, int quality) {
  if (quality == 0) {
    return color;
  }
  vec3 gain = quality == 1
    ? vec3(1.018, 1.008, 0.992)
    : quality == 2
      ? vec3(1.035, 1.015, 0.985)
      : vec3(1.050, 1.025, 0.970);
  vec3 lift = quality == 1
    ? vec3(0.003, 0.004, 0.006)
    : quality == 2
      ? vec3(0.006, 0.008, 0.012)
      : vec3(0.010, 0.013, 0.019);
  float saturation =
    quality == 1 ? 1.02 : quality == 2 ? 1.04 : 1.07;
  vec3 graded = color * gain + lift;
  float luminance = dot(graded, vec3(0.2126, 0.7152, 0.0722));
  return clamp(mix(vec3(luminance), graded, saturation), 0.0, 1.0);
}

void main() {
  vec3 sceneColor = texture(sceneColorTexture, texCoord).rgb;
  vec3 displayColor = acesToneMap(
    sceneColor * max(composite.parameters.x, 0.01)
  );
  displayColor = gradeColor(
    displayColor,
    clamp(int(composite.parameters.y + 0.5), 0, 3)
  );
  outColor = vec4(displayEncode(displayColor, composite.parameters.w), 1.0);
}
