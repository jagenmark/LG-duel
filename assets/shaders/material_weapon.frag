#version 450
#extension GL_EXT_texture_shadow_lod : require

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec3 viewDirection;
layout(location = 2) in vec4 baseColor;
layout(location = 3) in vec2 material;
layout(location = 4) in vec3 worldPosition;
layout(location = 5) in float viewDistance;
layout(location = 6) in vec2 ambientData;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform samplerCube weaponEnvironment;
layout(set = 2, binding = 1) uniform sampler2DShadow sunShadowMap;
layout(set = 2, binding = 2) uniform sampler2DArrayShadow pointShadowMap;
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

const float PI = 3.14159265359;

#include "includes/direct_display.glsl"
#include "includes/live_fill.glsl"
#include "includes/sun_shadow.glsl"
#include "includes/point_shadow.glsl"
#include "includes/point_light_response.glsl"
#include "includes/atmosphere.glsl"

vec3 directSpecular(
  vec3 n,
  vec3 v,
  vec3 l,
  vec3 albedo,
  float metallic,
  float roughness
) {
  vec3 h = normalize(v + l);
  float nDotL = max(dot(n, l), 0.0);
  float nDotV = max(dot(n, v), 0.001);
  float nDotH = max(dot(n, h), 0.0);
  float vDotH = max(dot(v, h), 0.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
  float distribution = alpha2 / max(PI * denominator * denominator, 0.001);
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float geometryV = nDotV / (nDotV * (1.0 - k) + k);
  float geometryL = nDotL / (nDotL * (1.0 - k) + k);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
  return distribution * geometryV * geometryL * fresnel /
    max(4.0 * nDotV * nDotL, 0.001);
}

void main() {
  vec3 n = normalize(worldNormal);
  float metallic = clamp(material.x, 0.0, 1.0);
  vec3 albedo = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  int materialQuality = clamp(int(sceneLights.parameters.w + 0.5), 0, 2);
  if (ambientData.y > 0.5) {
    vec3 debugColor = vec3(ambientData.x);
    outColor = sceneLights.postParameters.x < 0.0
      ? vec4(directDisplay(debugColor), 1.0)
      : vec4(debugColor, 1.0);
    return;
  }

  vec3 fillRadiance = sceneLights.fillColorIntensity.rgb *
    max(sceneLights.fillColorIntensity.w, 0.0);
  vec3 color = albedo * ambientData.x * correctedLiveFill(fillRadiance);

  vec3 sunDirection = normalize(-sceneLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  float sunVisibility = sunNDotL > 0.0
    ? sunShadowVisibility(worldPosition, n)
    : 1.0;
  vec3 sunRadiance = sceneLights.sunColor.rgb *
    max(sceneLights.sunDirectionIntensity.w, 0.0);
  color += albedo * sunRadiance * sunNDotL *
    (1.0 - metallic * 0.72) * sunVisibility;

  vec3 v = vec3(0.0);
  float roughness = 1.0;
  if (materialQuality > 0) {
    v = normalize(viewDirection);
    roughness = clamp(material.y, 0.10, 1.0);
    if (sunNDotL > 0.0) {
      float qualityScale = materialQuality == 1 ? 0.55 : 1.0;
      color += directSpecular(
        n, v, sunDirection, albedo, metallic, roughness
      ) * sunRadiance * sunNDotL * sunVisibility * qualityScale;
    }
  }

  // Point-light diffuse stays active at material quality zero. Specular and
  // environment terms remain inside the material-quality contract.
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
    color += pointLightDiffuseResponse(
      albedo * (1.0 - metallic * 0.72),
      radiance,
      nDotL
    );
    if (materialQuality == 2) {
      color += directSpecular(
        n, v, lightDirection, albedo, metallic, roughness
      ) * radiance * nDotL;
    }
  }

  if (materialQuality > 0) {
    vec3 reflected = reflect(-v, n);
    vec3 environment = textureLod(weaponEnvironment, reflected, roughness * 6.0).rgb;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    color += environment * f0 * mix(0.70, 0.28, roughness) *
      (materialQuality == 1 ? 0.60 : 1.0);
  }

  int atmosphereQuality = clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  if (sceneLights.postParameters.w < 0.5) {
    color = applyAtmosphereHaze(color, atmosphereQuality, viewDistance);
  }
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(color), 1.0)
    : vec4(color, baseColor.a);
}
