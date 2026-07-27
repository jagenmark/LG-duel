#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 quadUv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0, std140) uniform GlowData {
  vec4 parameters;
} glow;

void main() {
  vec2 centered = quadUv * 2.0 - vec2(1.0);
  float radial = clamp(1.0 - length(centered), 0.0, 1.0);
  float core = radial * radial;
  float brightness = max(max(vertexColor.r, vertexColor.g), vertexColor.b) * 2.0;
  float hot = max(brightness - max(glow.parameters.y, 0.0), 0.0);
  float bloom = glow.parameters.x > 0.5
    ? hot * max(glow.parameters.z, 0.0) * radial
    : 0.0;
  float alpha = vertexColor.a * clamp(core + bloom, 0.0, 1.0);
  outColor = vec4(vertexColor.rgb, alpha);
}
