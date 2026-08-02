#version 450
#extension GL_EXT_texture_shadow_lod : require

layout(location = 0) in vec4 baseColor;
layout(location = 1) in vec4 teamTint;
layout(location = 2) in float tintWeight;
layout(location = 3) in vec3 worldPosition;
layout(location = 4) in vec3 worldNormal;
layout(location = 5) in float viewDistance;
layout(location = 6) flat in uint rimQuality;
layout(location = 7) in vec3 viewDirection;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2DShadow sunShadowMap;
layout(set = 2, binding = 1) uniform sampler2DArrayShadow pointShadowMap;
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
#include "includes/sun_shadow.glsl"
#include "includes/point_shadow.glsl"
#include "includes/atmosphere.glsl"

void main() {
  vec3 base = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  vec3 tint = pow(max(teamTint.rgb, vec3(0.0)), vec3(2.2));
  vec3 albedo = mix(base, base * tint, clamp(tintWeight, 0.0, 1.0));
  vec3 n = normalize(worldNormal);
  vec3 v = normalize(viewDirection);
  vec3 sunDirection = normalize(-sceneLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  float shadow = sunNDotL > 0.0
    ? sunShadowVisibility(worldPosition, n)
    : 1.0;
  vec3 sunRadiance = sceneLights.sunColor.rgb *
    max(sceneLights.sunDirectionIntensity.w, 0.0);
  vec3 fillRadiance = sceneLights.fillColorIntensity.rgb *
    max(sceneLights.fillColorIntensity.w, 0.0);
  float skyFill = n.z * 0.5 + 0.5;
  vec3 color = albedo * (vec3(0.18) + fillRadiance * (0.35 + 0.65 * skyFill));
  color += albedo * sunRadiance * sunNDotL * shadow;

  int materialQuality = clamp(int(sceneLights.parameters.w + 0.5), 0, 2);
  if (materialQuality > 0 && sunNDotL > 0.0) {
    vec3 halfDirection = normalize(sunDirection + v);
    float highlight = pow(max(dot(n, halfDirection), 0.0), 28.0);
    color += mix(vec3(0.055), albedo, 0.18) * sunRadiance *
      highlight * sunNDotL * shadow *
      (materialQuality == 1 ? 0.30 : 0.50);
  }

  // Point-light diffuse stays active at material quality zero. The quality
  // value only gates the added highlight below.
  int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 32);
  for (int index = 0; index < lightCount; ++index) {
    vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
    float radius = max(sceneLights.positionRadius[index].w, 0.001);
    float distanceToLight = length(offset);
    if (distanceToLight >= radius) {
      continue;
    }
    vec3 lightDirection = offset / max(distanceToLight, 0.001);
    float sourceRadius = clamp(sceneLights.lightParameters[index].x, 0.0, radius);
    float attenuation = clamp(
      (radius - distanceToLight) / max(radius - sourceRadius, 0.001),
      0.0,
      1.0
    );
    attenuation *= attenuation;
    float nDotL = max(dot(n, lightDirection), 0.0);
    if (nDotL <= 0.0) {
      continue;
    }
    vec3 radiance = sceneLights.colorIntensity[index].rgb *
      sceneLights.colorIntensity[index].w * attenuation *
      sceneLights.lightParameters[index].w *
      pointShadowVisibility(index, worldPosition, n);
    color += albedo * radiance * nDotL;
    if (materialQuality == 2) {
      vec3 halfDirection = normalize(lightDirection + v);
      float highlight = pow(max(dot(n, halfDirection), 0.0), 24.0);
      color += radiance * highlight * nDotL * 0.12;
    }
  }

  if (rimQuality > 0u) {
    float rim = pow(
      1.0 - max(dot(n, v), 0.0),
      rimQuality == 1u ? 3.2 : 2.6
    );
    rim *= (0.16 + 0.84 * skyFill) * (rimQuality == 1u ? 0.62 : 1.0);
    vec3 rimColor = mix(albedo, vec3(0.58, 0.72, 0.92), 0.36);
    color += rimColor * rim * 0.24;
  }

  int atmosphereQuality = clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  color = applyAtmosphereHaze(color, atmosphereQuality, viewDistance);
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(color), 1.0)
    : vec4(color, baseColor.a * teamTint.a);
}
