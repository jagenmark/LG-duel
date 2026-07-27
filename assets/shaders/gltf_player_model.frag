#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in float viewDistance;
layout(location = 0) out vec4 outColor;

vec3 acesToneMap(vec3 color) {
  return clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
}

void main() {
  vec3 linearColor = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
  linearColor = acesToneMap(linearColor * 1.16);
  float luminance = dot(linearColor, vec3(0.2126, 0.7152, 0.0722));
  linearColor = mix(vec3(luminance), linearColor, 1.04);
  vec3 color = pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));

  float haze = 1.0 - exp(-max(viewDistance - 32.0, 0.0) * 0.008);
  color = mix(color, vec3(0.30, 0.34, 0.40), min(haze, 0.20));
  outColor = vec4(color, vertexColor.a);
}
