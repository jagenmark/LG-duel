#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D sourceTexture;
layout(set = 3, binding = 0, std140) uniform BlurData {
  vec4 texelDirection;
} blur;

void main() {
  vec2 stepUv = blur.texelDirection.xy;
  vec3 color = texture(sourceTexture, texCoord).rgb * 0.227027;
  color += texture(sourceTexture, texCoord + stepUv * 1.384615).rgb * 0.316216;
  color += texture(sourceTexture, texCoord - stepUv * 1.384615).rgb * 0.316216;
  color += texture(sourceTexture, texCoord + stepUv * 3.230769).rgb * 0.070270;
  color += texture(sourceTexture, texCoord - stepUv * 3.230769).rgb * 0.070270;
  outColor = vec4(color, 1.0);
}
