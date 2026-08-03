#version 450
#extension GL_EXT_texture_shadow_lod : require

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in float viewDistance;
layout(location = 4) in vec3 worldNormal;
layout(location = 6) in vec3 viewDirection;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D worldAtlas;
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
layout(set = 3, binding = 1, std140) uniform WorldMaterialData {
  vec4 traits;
} worldMaterial;

#include "includes/direct_display.glsl"
#include "includes/sun_shadow.glsl"
#include "includes/point_shadow.glsl"
#include "includes/point_light_response.glsl"
#include "includes/atmosphere.glsl"

void main() {
  vec4 sampled = texture(worldAtlas, texCoord);
  vec3 albedo = pow(max(sampled.rgb, vec3(0.0)), vec3(2.2));
  int materialQuality = clamp(int(sceneLights.parameters.w + 0.5), 0, 2);

  vec3 bakedLight = max(vertexColor.rgb, vec3(0.00169355));
  vec3 sceneColor = albedo * bakedLight;
  float bakedPeak = max(
    max(vertexColor.r, vertexColor.g),
    max(vertexColor.b, 0.001)
  );
  vec3 authoredTint = clamp(vertexColor.rgb / bakedPeak, 0.0, 1.0);
  vec3 directAlbedo = albedo * authoredTint;
  vec3 n = normalize(worldNormal);
  vec3 sunDirection = normalize(-sceneLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  float sunVisibility = sunNDotL > 0.0
    ? sunShadowVisibility(worldPosition, n)
    : 1.0;
  vec3 sunRadiance = sceneLights.sunColor.rgb *
    max(sceneLights.sunDirectionIntensity.w, 0.0);
  sceneColor += directAlbedo * sunRadiance * sunNDotL * sunVisibility;

  vec3 v = vec3(0.0);
  float roughness = 1.0;
  float metallic = 0.0;
  float specularStrength = 0.0;
  if (materialQuality > 0) {
    v = normalize(viewDirection);
    roughness = clamp(worldMaterial.traits.x, 0.10, 1.0);
    metallic = clamp(worldMaterial.traits.y, 0.0, 1.0);
    specularStrength = clamp(worldMaterial.traits.z, 0.0, 1.0);
    if (sunNDotL > 0.0) {
      vec3 halfDirection = normalize(sunDirection + v);
      float exponent = mix(10.0, 72.0, 1.0 - roughness);
      float highlight = pow(max(dot(n, halfDirection), 0.0), exponent);
      float qualityScale = materialQuality == 1 ? 0.55 : 1.0;
      vec3 specularColor = mix(vec3(0.045), directAlbedo, metallic);
      sceneColor += specularColor * sunRadiance * highlight *
        sunNDotL * specularStrength * qualityScale * sunVisibility;
    }
  }

  // Point-light diffuse stays active at material quality zero. Lights without
  // a live world contribution are already represented by baked vertex color.
  int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 32);
  for (int index = 0; index < lightCount; ++index) {
    float liveWorldScale = sceneLights.lightParameters[index].y *
      sceneLights.lightParameters[index].w;
    if (liveWorldScale <= 0.0) {
      continue;
    }
    vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
    float radius = max(sceneLights.positionRadius[index].w, 0.001);
    float lightDistance = length(offset);
    if (lightDistance >= radius) {
      continue;
    }
    vec3 lightDirection = offset / max(lightDistance, 0.001);
    float sourceRadius = clamp(sceneLights.lightParameters[index].x, 0.0, radius);
    float attenuation = clamp(
      (radius - lightDistance) / max(radius - sourceRadius, 0.001),
      0.0,
      1.0
    );
    attenuation *= attenuation;
    float localNDotL = max(dot(n, lightDirection), 0.0);
    if (localNDotL <= 0.0) {
      continue;
    }
    float pointVisibility = pointShadowVisibility(index, worldPosition, n);
    vec3 radiance = sceneLights.colorIntensity[index].rgb *
      sceneLights.colorIntensity[index].w * attenuation * liveWorldScale *
      pointVisibility;
    sceneColor += pointLightDiffuseResponse(
      directAlbedo,
      radiance,
      localNDotL
    );
    if (materialQuality == 2) {
      vec3 halfDirection = normalize(lightDirection + v);
      float exponent = mix(10.0, 72.0, 1.0 - roughness);
      float highlight = pow(max(dot(n, halfDirection), 0.0), exponent);
      vec3 specularColor = mix(vec3(0.045), directAlbedo, metallic);
      sceneColor += specularColor * radiance * highlight *
        localNDotL * specularStrength;
    }
  }

  sceneColor += albedo * clamp(worldMaterial.traits.w, 0.0, 0.35);
  int atmosphereQuality = clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  sceneColor = applyAtmosphereHaze(sceneColor, atmosphereQuality, viewDistance);
  if (sceneLights.postParameters.x < 0.0) {
    outColor = vec4(directDisplay(sceneColor), 1.0);
  } else {
    // Keep this output linear for the same single scene-to-display curve.
    outColor = vec4(sceneColor, sampled.a * vertexColor.a);
  }
}
