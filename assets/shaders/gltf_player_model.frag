#version 450
#extension GL_EXT_texture_shadow_lod : require

layout(location = 0) in vec4 baseColor;
layout(location = 1) in vec4 teamTint;
layout(location = 2) in float flatTintWeight;
layout(location = 3) in vec3 worldPosition;
layout(location = 4) in vec3 worldNormal;
layout(location = 5) in float viewDistance;
layout(location = 6) flat in uint rimQuality;
layout(location = 7) in vec3 viewDirection;
layout(location = 8) in vec2 materialTexCoord;
layout(location = 9) in float albedoTextureMode;
layout(location = 10) in vec2 ambientData;
layout(location = 0) out vec4 outColor;

// The atlas is an sRGB texture. Hardware converts it to linear when sampled.
// The packed mask is UNORM/linear and must never receive an sRGB conversion.
layout(set = 2, binding = 0) uniform sampler2D albedoAtlas;
layout(set = 2, binding = 1) uniform sampler2D packedMaterialMask;
layout(set = 2, binding = 2) uniform sampler2DShadow sunShadowMap;
layout(set = 2, binding = 3) uniform sampler2DArrayShadow pointShadowMap;
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
#include "includes/live_fill.glsl"
#include "includes/sun_shadow.glsl"
#include "includes/point_shadow.glsl"
#include "includes/point_light_response.glsl"
#include "includes/atmosphere.glsl"
#include "includes/team_tint.glsl"

vec3 restrainedSpecular(
  vec3 n,
  vec3 v,
  vec3 lightDirection,
  vec3 lightColor,
  vec3 albedo,
  float roughness,
  float metallic,
  float strength
) {
  vec3 halfDirection = normalize(lightDirection + v);
  float exponent = mix(9.0, 84.0, 1.0 - roughness);
  float lobe = pow(max(dot(n, halfDirection), 0.0), exponent);
  lobe *= 0.15 + 0.85 * (1.0 - roughness);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  return lightColor * f0 * lobe * strength;
}

void main() {
  // Vertex and team colours are authored/configured sRGB values, unlike the
  // sRGB albedo atlas which has already converted during sampling.
  vec3 factor = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  vec3 tint = pow(max(teamTint.rgb, vec3(0.0)), vec3(2.2));
  vec4 sampledAlbedo = texture(albedoAtlas, materialTexCoord);
  vec3 albedo = albedoTextureMode > 0.75
    ? sampledAlbedo.rgb
    : albedoTextureMode > 0.25
      ? factor * sampledAlbedo.rgb
      : factor;
  vec4 materialMask = texture(packedMaterialMask, materialTexCoord);
  albedo = applyTeamTint(
    albedo,
    tint,
    max(flatTintWeight, materialMask.r)
  );
  if (ambientData.y > 0.5) {
    vec3 debugColor = vec3(ambientData.x);
    outColor = sceneLights.postParameters.x < 0.0
      ? vec4(directDisplay(debugColor), 1.0)
      : vec4(debugColor, 1.0);
    return;
  }

  int materialQuality = clamp(int(sceneLights.parameters.w + 0.5), 0, 2);
  float roughness = clamp(materialMask.g, 0.08, 1.0);
  // Quality 1 has the roughness-aware highlight path. Metal stays a quality
  // 2 response so the default path remains restrained and easy to read.
  float metallic = materialQuality >= 2
    ? clamp(materialMask.b, 0.0, 1.0)
    : 0.0;
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
  vec3 diffuseAlbedo = albedo * (1.0 - metallic * 0.30);
  vec3 color = diffuseAlbedo * ambientData.x *
    correctedLiveFill(fillRadiance);
  color += diffuseAlbedo * sunRadiance * sunNDotL * shadow;

  if (materialQuality > 0 && sunNDotL > 0.0) {
    float specularStrength = materialQuality == 1 ? 0.26 : 0.42;
    color += restrainedSpecular(
      n, v, sunDirection, sunRadiance, albedo, roughness, metallic,
      specularStrength * sunNDotL * shadow
    );
  }

  // Diffuse point lights always remain active. The authored material path
  // adds only the restrained roughness/metallic highlight response.
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
    color += pointLightDiffuseResponse(albedo, radiance, nDotL);
    if (materialQuality == 2) {
      color += restrainedSpecular(
        n, v, lightDirection, radiance, albedo, roughness, metallic,
        0.42 * nDotL
      );
    } else if (materialQuality == 1) {
      color += restrainedSpecular(
        n, v, lightDirection, radiance, albedo, roughness, metallic,
        0.26 * nDotL
      );
    }
  }

  // Alpha is reserved for future use in the packed convention. This Worker
  // keeps it at zero, so no part gains an accidental glow.
  color += albedo * materialMask.a * 0.15;
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
    : vec4(color, baseColor.a * sampledAlbedo.a * teamTint.a);
}
