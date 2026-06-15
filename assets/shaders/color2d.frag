#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D fontAtlas;

void main() {
  float coverage = texture(fontAtlas, texCoord).r;
  outColor = vec4(vertexColor.rgb, vertexColor.a * coverage);
}
