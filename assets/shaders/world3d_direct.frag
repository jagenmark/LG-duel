#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;

#include "includes/direct_display.glsl"

void main() {
  vec3 color = pow(
    max((texture(worldAtlas, texCoord) * vertexColor).rgb, vec3(0.0)),
    vec3(2.2)
  );
  outColor = vec4(directDisplay(color), 1.0);
}
