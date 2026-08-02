#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 quadUv;
layout(location = 2) in float viewDistance;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0, std140) uniform SceneLightData {
  vec4 parameters;
  vec4 positionRadius[32];
  vec4 colorIntensity[32];
  vec4 lightParameters[32];
  vec4 pointShadowParameters;
  vec4 sunDirectionIntensity;
  vec4 sunColor;
  vec4 fillColorIntensity;
  vec4 shadowOrigin;
  vec4 shadowRight;
  vec4 shadowUp;
  vec4 shadowForward;
  vec4 shadowParameters;
  vec4 postParameters;
} sceneLights;

#include "includes/atmosphere.glsl"

void main() {
  vec2 centered = quadUv * 2.0 - vec2(1.0);
  float radial = clamp(1.0 - length(centered), 0.0, 1.0);
  float core = radial * radial;
  vec3 color = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
  int atmosphereQuality = clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  color = applyAtmosphereHaze(color, atmosphereQuality, viewDistance);
  outColor = vec4(color, vertexColor.a * core);
}
