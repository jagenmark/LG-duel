#version 450

layout(location = 0) in vec3 rayDirection;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform samplerCube skyTexture;
vec3 directDisplay(vec3 color) {
  vec3 mapped = clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
  return pow(mapped, vec3(1.0 / 2.2));
}

void main() {
  vec3 direction = normalize(rayDirection);
  vec3 sourceSrgb = texture(skyTexture, direction).rgb;
  vec3 linearColor = pow(max(sourceSrgb, vec3(0.0)), vec3(2.2));
  outColor = vec4(directDisplay(linearColor), 1.0);
}
