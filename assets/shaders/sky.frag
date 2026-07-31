#version 450

layout(location = 0) in vec3 rayDirection;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform samplerCube skyTexture;
void main() {
  vec3 direction = normalize(rayDirection);
  vec3 sourceSrgb = texture(skyTexture, direction).rgb;
  outColor = vec4(pow(max(sourceSrgb, vec3(0.0)), vec3(2.2)), 1.0);
}
