#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 3) in float viewDistance;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;

vec3 acesToneMap(vec3 color) {
  return clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
}

vec3 gradeWorld(vec3 bakedSurface) {
  // The static mesh already contains baked vertex lighting in the same display
  // range as the UNORM world atlas. Do not decode and tone-map that product a
  // second time: dark baked routes collapse to black when both curves apply.
  vec3 color = max(bakedSurface, vec3(0.0));

  // Keep the intended restrained cool grade, but give low world lighting a
  // small display-space floor so authored wall and floor detail stays visible.
  color = color * vec3(1.035, 1.015, 0.985) + vec3(0.014, 0.016, 0.020);
  float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
  return clamp(mix(vec3(luminance), color, 1.04), 0.0, 1.0);
}

void main() {
  vec4 surface = texture(worldAtlas, texCoord) * vertexColor;
  vec3 color = gradeWorld(surface.rgb);

  // Keep near combat crisp. Only the far half of large maps gets a restrained
  // blue-grey haze, capped so silhouettes stay readable.
  float haze = 1.0 - exp(-max(viewDistance - 28.0, 0.0) * 0.010);
  haze = min(haze, 0.32);
  const vec3 hazeColor = vec3(0.30, 0.34, 0.40);
  color = mix(color, hazeColor, haze);
  outColor = vec4(color, surface.a);
}
