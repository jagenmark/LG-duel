#version 450
#extension GL_EXT_texture_shadow_lod : require

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in float viewDistance;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2DArrayShadow pointShadowMap;
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
#include "includes/point_shadow.glsl"
#include "includes/atmosphere.glsl"

void main() {
  vec3 albedo = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
  vec3 color = albedo;
  vec3 n = normalize(worldNormal);
  int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 32);
  for (int index = 0; index < lightCount; ++index) {
    vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
    float radius = max(sceneLights.positionRadius[index].w, 0.001);
    float lightDistance = length(offset);
    if (lightDistance >= radius) {
      continue;
    }
    float sourceRadius = clamp(sceneLights.lightParameters[index].x, 0.0, radius);
    float attenuation = clamp(
      (radius - lightDistance) / max(radius - sourceRadius, 0.001),
      0.0,
      1.0
    );
    attenuation *= attenuation;
    float nDotL = max(dot(n, offset / max(lightDistance, 0.001)), 0.0);
    if (nDotL <= 0.0) {
      continue;
    }
    vec3 radiance = sceneLights.colorIntensity[index].rgb *
      sceneLights.colorIntensity[index].w * attenuation *
      sceneLights.lightParameters[index].w *
      pointShadowVisibility(index, worldPosition, n);
    color += albedo * radiance * nDotL;
  }
  int atmosphereQuality = clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  if (sceneLights.postParameters.w < 0.5) {
    color = applyAtmosphereHaze(color, atmosphereQuality, viewDistance);
  }
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(color), 1.0)
    : vec4(color, vertexColor.a);
}
