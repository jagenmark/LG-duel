#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 0) out vec4 outColor;

#include "includes/direct_display.glsl"

void main() {
  vec3 color = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
  outColor = vec4(directDisplay(color), 1.0);
}
