#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in float viewDistance;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
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

#include "includes/direct_display.glsl"
#include "includes/atmosphere.glsl"

void main() {
  vec4 sampled = texture(worldAtlas, texCoord) * vertexColor;
  vec3 linearColor = pow(max(sampled.rgb, vec3(0.0)), vec3(2.2));
  int count = clamp(int(sceneLights.parameters.x + 0.5), 0, 32);
  for (int index = 0; index < count; ++index) {
    vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
    float radius = max(sceneLights.positionRadius[index].w, 0.001);
    float distanceToLight = length(offset);
    if (distanceToLight >= radius) {
      continue;
    }
    float sourceRadius = clamp(sceneLights.lightParameters[index].x, 0.0, radius);
    float attenuation = clamp(
      (radius - distanceToLight) / max(radius - sourceRadius, 0.001),
      0.0,
      1.0
    );
    attenuation *= attenuation;
    linearColor += sceneLights.colorIntensity[index].rgb *
      sceneLights.colorIntensity[index].w * attenuation *
      sceneLights.lightParameters[index].w;
  }
  int atmosphereQuality = clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  linearColor = applyAtmosphereHaze(
    linearColor,
    atmosphereQuality,
    viewDistance
  );
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(linearColor), 1.0)
    : vec4(linearColor, sampled.a);
}
