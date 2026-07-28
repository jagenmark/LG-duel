#version 450

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
layout(set = 3, binding = 0, std140) uniform SceneLightData {
  vec4 parameters;
  vec4 positionRadius[8];
  vec4 colorIntensity[8];
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

vec3 directDisplay(vec3 color) {
  vec3 mapped = clamp(
    (color * (2.51 * color + 0.03)) /
      (color * (2.43 * color + 0.59) + 0.14),
    0.0,
    1.0
  );
  return pow(mapped, vec3(1.0 / 2.2));
}

float sunShadowVisibility(vec3 position, vec3 normal) {
  float mapSize = sceneLights.shadowParameters.z;
  if (mapSize < 1.0) {
    return 1.0;
  }
  vec3 receiver = position + normalize(normal) * sceneLights.shadowOrigin.w;
  vec3 offset = receiver - sceneLights.shadowOrigin.xyz;
  float extent = max(sceneLights.shadowParameters.x, 0.001);
  vec2 uv = vec2(
    dot(offset, sceneLights.shadowRight.xyz) / extent,
    -dot(offset, sceneLights.shadowUp.xyz) / extent
  ) * 0.5 + 0.5;
  float receiverDepth =
    dot(offset, sceneLights.shadowForward.xyz) /
      max(sceneLights.shadowParameters.y, 0.001) -
    sceneLights.shadowParameters.w;
  if (
    receiverDepth <= 0.0 || receiverDepth >= 1.0 ||
    any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))
  ) {
    return 1.0;
  }
  return texture(sunShadowMap, vec3(uv, receiverDepth));
}

void main() {
  vec3 base = pow(max(baseColor.rgb, vec3(0.0)), vec3(2.2));
  vec3 tint = pow(max(teamTint.rgb, vec3(0.0)), vec3(2.2));
  vec3 albedo = mix(base, base * tint, clamp(tintWeight, 0.0, 1.0));
  vec3 n = normalize(worldNormal);
  vec3 v = normalize(viewDirection);
  vec3 sunDirection = normalize(-sceneLights.sunDirectionIntensity.xyz);
  float sunNDotL = max(dot(n, sunDirection), 0.0);
  float shadow = sunShadowVisibility(worldPosition, n);
  vec3 sunRadiance = sceneLights.sunColor.rgb *
    max(sceneLights.sunDirectionIntensity.w, 0.0);
  vec3 fillRadiance = sceneLights.fillColorIntensity.rgb *
    max(sceneLights.fillColorIntensity.w, 0.0);
  float skyFill = n.z * 0.5 + 0.5;
  vec3 color = albedo * (vec3(0.18) + fillRadiance * (0.35 + 0.65 * skyFill));
  color += albedo * sunRadiance * sunNDotL * shadow;

  int materialQuality =
    clamp(int(sceneLights.parameters.w + 0.5), 0, 2);
  if (materialQuality > 0) {
    if (sunNDotL > 0.0) {
      vec3 halfDirection = normalize(sunDirection + v);
      float highlight = pow(max(dot(n, halfDirection), 0.0), 28.0);
      color += mix(vec3(0.055), albedo, 0.18) * sunRadiance *
        highlight * sunNDotL * shadow *
        (materialQuality == 1 ? 0.30 : 0.50);
    }

    int lightCount = clamp(int(sceneLights.parameters.x + 0.5), 0, 8);
    for (int index = 0; index < lightCount; ++index) {
      vec3 offset = sceneLights.positionRadius[index].xyz - worldPosition;
      float radius = max(sceneLights.positionRadius[index].w, 0.001);
      float distanceToLight = length(offset);
      vec3 lightDirection = offset / max(distanceToLight, 0.001);
      float attenuation = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
      attenuation *= attenuation;
      float nDotL = max(dot(n, lightDirection), 0.0);
      vec3 radiance = sceneLights.colorIntensity[index].rgb *
        sceneLights.colorIntensity[index].w * attenuation;
      color += albedo * radiance * nDotL;
      if (materialQuality == 2 && nDotL > 0.0) {
        vec3 halfDirection = normalize(lightDirection + v);
        float highlight = pow(max(dot(n, halfDirection), 0.0), 24.0);
        color += radiance * highlight * nDotL * 0.12;
      }
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

  int atmosphereQuality =
    clamp(int(sceneLights.parameters.z + 0.5), 0, 3);
  float hazeStart = atmosphereQuality == 1
    ? 32.0
    : atmosphereQuality == 2 ? 28.0 : 24.0;
  float hazeDensity = atmosphereQuality == 1
    ? 0.006
    : atmosphereQuality == 2 ? 0.010 : 0.014;
  float hazeCap = atmosphereQuality == 1
    ? 0.18
    : atmosphereQuality == 2 ? 0.32 : 0.42;
  float haze = atmosphereQuality == 0
    ? 0.0
    : 1.0 - exp(-max(viewDistance - hazeStart, 0.0) * hazeDensity);
  color = mix(color, vec3(0.0707, 0.0932, 0.1332), min(haze, hazeCap));
  outColor = sceneLights.postParameters.x < 0.0
    ? vec4(directDisplay(color), 1.0)
    : vec4(color, baseColor.a * teamTint.a);
}
