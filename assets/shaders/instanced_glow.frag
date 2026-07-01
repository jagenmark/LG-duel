#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 quadUv;
layout(location = 0) out vec4 outColor;

void main() {
  vec2 centered = quadUv * 2.0 - vec2(1.0);
  float radial = clamp(1.0 - length(centered), 0.0, 1.0);
  float alpha = vertexColor.a * radial * radial;
  outColor = vec4(vertexColor.rgb, alpha);
}
