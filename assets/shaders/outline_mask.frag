#version 450

layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0, std140) uniform OutlineMaskData {
  vec4 group;
} mask;

void main() {
  outColor = vec4(mask.group.x, 0.0, 0.0, 1.0);
}
