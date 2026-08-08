#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 4) in vec2 ambientData;
layout(location = 0) out vec4 outColor;

#include "includes/direct_display.glsl"

void main() {
  vec3 color = ambientData.y > 0.5
    ? vec3(ambientData.x)
    : pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2)) * ambientData.x;
  outColor = vec4(directDisplay(color), 1.0);
}
